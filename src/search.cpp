/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "book.h"
#include "evaluate.h"
#include "history.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "search.h"
#include "timeman.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"

namespace Search {

  volatile SignalsType Signals;
  LimitsType Limits;
  std::vector<RootMove> RootMoves;
  Position RootPosition;
}

using std::string;
using std::cout;
using std::endl;
using Eval::evaluate;
using namespace Search;

namespace {

  // Set to true to force running with one thread. Used for debugging
  const bool FakeSplit = false;

  // Different node types, used as template parameter
  enum NodeType { Root, PV, NonPV, SplitPointRoot, SplitPointPV, SplitPointNonPV };

  // Lookup table to check if a Piece is a slider and its access function
  const bool Slidings[18] = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1 };
  inline bool piece_is_slider(Piece p) { return Slidings[p]; }

  // Maximum depth for razoring
  const Depth RazorDepth = 4 * ONE_PLY;

  // Dynamic razoring margin based on depth
  inline Value razor_margin(Depth d) { return Value(0x200 + 0x10 * int(d)); }

  // Maximum depth for use of dynamic threat detection when null move fails low
  const Depth ThreatDepth = 5 * ONE_PLY;

  // Minimum depth for use of internal iterative deepening
  const Depth IIDDepth[] = { 8 * ONE_PLY, 5 * ONE_PLY };

  // At Non-PV nodes we do an internal iterative deepening search
  // when the static evaluation is bigger then beta - IIDMargin.
  const Value IIDMargin = Value(0x100);

  // Minimum depth for use of singular extension
  const Depth SingularExtensionDepth[] = { 8 * ONE_PLY, 6 * ONE_PLY };

  // Futility margin for quiescence search
  const Value FutilityMarginQS = Value(0x80);

  // Futility lookup tables (initialized at startup) and their access functions
  Value FutilityMargins[16][64]; // [depth][moveNumber]
  int FutilityMoveCounts[32];    // [depth]

  inline Value futility_margin(Depth d, int mn) {

    return d < 7 * ONE_PLY ? FutilityMargins[std::max(int(d), 1)][std::min(mn, 63)]
                           : 2 * VALUE_INFINITE;
  }

  inline int futility_move_count(Depth d) {

    return d < 16 * ONE_PLY ? FutilityMoveCounts[d] : MAX_MOVES;
  }

  // Reduction lookup tables (initialized at startup) and their access function
  int8_t Reductions[2][64][64]; // [pv][depth][moveNumber]

  template <bool PvNode> inline Depth reduction(Depth d, int mn) {

    return (Depth) Reductions[PvNode][std::min(int(d) / ONE_PLY, 63)][std::min(mn, 63)];
  }

  // Easy move margin. An easy move candidate must be at least this much better
  // than the second best move.
  const Value EasyMoveMargin = Value(0x150);

  // This is the minimum interval in msec between two check_time() calls
  const int TimerResolution = 5;


  size_t MultiPV, UCIMultiPV, PVIdx;
  TimeManager TimeMgr;
  Time SearchTime;
  int BestMoveChanges;
  int SkillLevel;
  bool SkillLevelEnabled, Chess960;
  History H;


  template <NodeType NT>
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth);

  template <NodeType NT>
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth);

  void id_loop(Position& pos);
  bool check_is_dangerous(Position &pos, Move move, Value futilityBase, Value beta);
  bool connected_moves(const Position& pos, Move m1, Move m2);
  Value value_to_tt(Value v, int ply);
  Value value_from_tt(Value v, int ply);
  bool can_return_tt(const TTEntry* tte, Depth depth, Value ttValue, Value beta);
  bool connected_threat(const Position& pos, Move m, Move threat);
  Value refine_eval(const TTEntry* tte, Value ttValue, Value defaultEval);
  Move do_skill_level();
  string score_to_uci(Value v, Value alpha = -VALUE_INFINITE, Value beta = VALUE_INFINITE);
  void pv_info_to_log(Position& pos, int depth, Value score, int time, Move pv[]);
  void pv_info_to_uci(const Position& pos, int depth, Value alpha, Value beta);

  // MovePickerExt class template extends MovePicker and allows to choose at
  // compile time the proper moves source according to the type of node. In the
  // default case we simply create and use a standard MovePicker object.
  template<bool SpNode> struct MovePickerExt : public MovePicker {

    MovePickerExt(const Position& p, Move ttm, Depth d, const History& h, Stack* ss, Value b)
                  : MovePicker(p, ttm, d, h, ss, b) {}
  };

  // In case of a SpNode we use split point's shared MovePicker object as moves source
  template<> struct MovePickerExt<true> : public MovePicker {

    MovePickerExt(const Position& p, Move ttm, Depth d, const History& h, Stack* ss, Value b)
                  : MovePicker(p, ttm, d, h, ss, b), mp(ss->sp->mp) {}

    Move next_move() { return mp->next_move(); }
    MovePicker* mp;
  };

  // is_dangerous() checks whether a move belongs to some classes of known
  // 'dangerous' moves so that we avoid to prune it.
  FORCE_INLINE bool is_dangerous(const Position& pos, Move m, bool captureOrPromotion) {

    // Test for a pawn pushed to 7th or a passed pawn move
    if (type_of(pos.piece_moved(m)) == PAWN)
    {
        Color c = pos.side_to_move();
        if (   relative_rank(c, to_sq(m)) == RANK_7
            || pos.pawn_is_passed(c, to_sq(m)))
            return true;
    }

    // Test for a capture that triggers a pawn endgame
    if (   captureOrPromotion
        && type_of(pos.piece_on(to_sq(m))) != PAWN
        && (  pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK)
            - PieceValueMidgame[pos.piece_on(to_sq(m))] == VALUE_ZERO)
        && !is_special(m))
        return true;

    return false;
  }

} // namespace


/// Search::init() is called during startup to initialize various lookup tables

void Search::init() {

  int d;  // depth (ONE_PLY == 2)
  int hd; // half depth (ONE_PLY == 1)
  int mc; // moveCount

  // Init reductions array
  for (hd = 1; hd < 64; hd++) for (mc = 1; mc < 64; mc++)
  {
      double    pvRed = log(double(hd)) * log(double(mc)) / 3.0;
      double nonPVRed = 0.33 + log(double(hd)) * log(double(mc)) / 2.25;
      Reductions[1][hd][mc] = (int8_t) (   pvRed >= 1.0 ? floor(   pvRed * int(ONE_PLY)) : 0);
      Reductions[0][hd][mc] = (int8_t) (nonPVRed >= 1.0 ? floor(nonPVRed * int(ONE_PLY)) : 0);
  }

  // Init futility margins array
  for (d = 1; d < 16; d++) for (mc = 0; mc < 64; mc++)
      FutilityMargins[d][mc] = Value(112 * int(log(double(d * d) / 2) / log(2.0) + 1.001) - 8 * mc + 45);

  // Init futility move count array
  for (d = 0; d < 32; d++)
      FutilityMoveCounts[d] = int(3.001 + 0.25 * pow(d, 2.0));
}


/// Search::perft() is our utility to verify move generation. All the leaf nodes
/// up to the given depth are generated and counted and the sum returned.

int64_t Search::perft(Position& pos, Depth depth) {

  StateInfo st;
  int64_t cnt = 0;

  MoveList<MV_LEGAL> ml(pos);

  // At the last ply just return the number of moves (leaf nodes)
  if (depth == ONE_PLY)
      return ml.size();

  CheckInfo ci(pos);
  for ( ; !ml.end(); ++ml)
  {
      pos.do_move(ml.move(), st, ci, pos.move_gives_check(ml.move(), ci));
      cnt += perft(pos, depth - ONE_PLY);
      pos.undo_move(ml.move());
  }
  return cnt;
}


/// Search::think() is the external interface to Stockfish's search, and is
/// called by the main thread when the program receives the UCI 'go' command. It
/// searches from RootPosition and at the end prints the "bestmove" to output.

void Search::think() {

  static Book book; // Defined static to initialize the PRNG only once

  Position& pos = RootPosition;
  Chess960 = pos.is_chess960();
  Eval::RootColor = pos.side_to_move();
  SearchTime.restart();
  TimeMgr.init(Limits, pos.startpos_ply_counter(), pos.side_to_move());
  TT.new_search();
  H.clear();

  if (RootMoves.empty())
  {
      cout << "info depth 0 score "
           << score_to_uci(pos.in_check() ? -VALUE_MATE : VALUE_DRAW) << endl;

      RootMoves.push_back(MOVE_NONE);
      goto finalize;
  }

  if (Options["OwnBook"])
  {
      Move bookMove = book.probe(pos, Options["Book File"], Options["Best Book Move"]);

      if (bookMove && count(RootMoves.begin(), RootMoves.end(), bookMove))
      {
          std::swap(RootMoves[0], *find(RootMoves.begin(), RootMoves.end(), bookMove));
          goto finalize;
      }
  }

  UCIMultiPV = Options["MultiPV"];
  SkillLevel = Options["Skill Level"];

  // Do we have to play with skill handicap? In this case enable MultiPV that
  // we will use behind the scenes to retrieve a set of possible moves.
  SkillLevelEnabled = (SkillLevel < 20);
  MultiPV = (SkillLevelEnabled ? std::max(UCIMultiPV, (size_t)4) : UCIMultiPV);

  if (Options["Use Search Log"])
  {
      Log log(Options["Search Log Filename"]);
      log << "\nSearching: "  << pos.to_fen()
          << "\ninfinite: "   << Limits.infinite
          << " ponder: "      << Limits.ponder
          << " time: "        << Limits.times[pos.side_to_move()]
          << " increment: "   << Limits.incs[pos.side_to_move()]
          << " moves to go: " << Limits.movestogo
          << endl;
  }

  Threads.wake_up();

  // Set best timer interval to avoid lagging under time pressure. Timer is
  // used to check for remaining available thinking time.
  if (Limits.use_time_management())
      Threads.set_timer(std::min(100, std::max(TimeMgr.available_time() / 16, TimerResolution)));
  else
      Threads.set_timer(100);

  // We're ready to start searching. Call the iterative deepening loop function
  id_loop(pos);

  Threads.set_timer(0); // Stop timer
  Threads.sleep();

  if (Options["Use Search Log"])
  {
      int e = SearchTime.elapsed();

      Log log(Options["Search Log Filename"]);
      log << "Nodes: "          << pos.nodes_searched()
          << "\nNodes/second: " << (e > 0 ? pos.nodes_searched() * 1000 / e : 0)
          << "\nBest move: "    << move_to_san(pos, RootMoves[0].pv[0]);

      StateInfo st;
      pos.do_move(RootMoves[0].pv[0], st);
      log << "\nPonder move: " << move_to_san(pos, RootMoves[0].pv[1]) << endl;
      pos.undo_move(RootMoves[0].pv[0]);
  }

finalize:

  // When we reach max depth we arrive here even without Signals.stop is raised,
  // but if we are pondering or in infinite search, we shouldn't print the best
  // move before we are told to do so.
  if (!Signals.stop && (Limits.ponder || Limits.infinite))
      Threads[pos.thread()].wait_for_stop_or_ponderhit();

  // Best move could be MOVE_NONE when searching on a stalemate position
  cout << "bestmove " << move_to_uci(RootMoves[0].pv[0], Chess960)
       << " ponder "  << move_to_uci(RootMoves[0].pv[1], Chess960) << endl;
}


namespace {

  // id_loop() is the main iterative deepening loop. It calls search() repeatedly
  // with increasing depth until the allocated thinking time has been consumed,
  // user stops the search, or the maximum search depth is reached.

  void id_loop(Position& pos) {

    Stack ss[MAX_PLY_PLUS_2];
    int depth, prevBestMoveChanges;
    Value bestValue, alpha, beta, delta;
    bool bestMoveNeverChanged = true;
    Move skillBest = MOVE_NONE;

    memset(ss, 0, 4 * sizeof(Stack));
    depth = BestMoveChanges = 0;
    bestValue = delta = -VALUE_INFINITE;
    ss->currentMove = MOVE_NULL; // Hack to skip update gains

    // Iterative deepening loop until requested to stop or target depth reached
    while (!Signals.stop && ++depth <= MAX_PLY && (!Limits.depth || depth <= Limits.depth))
    {
        // Save last iteration's scores before first PV line is searched and all
        // the move scores but the (new) PV are set to -VALUE_INFINITE.
        for (size_t i = 0; i < RootMoves.size(); i++)
            RootMoves[i].prevScore = RootMoves[i].score;

        prevBestMoveChanges = BestMoveChanges;
        BestMoveChanges = 0;

        // MultiPV loop. We perform a full root search for each PV line
        for (PVIdx = 0; PVIdx < std::min(MultiPV, RootMoves.size()); PVIdx++)
        {
            // Set aspiration window default width
            if (depth >= 5 && abs(RootMoves[PVIdx].prevScore) < VALUE_KNOWN_WIN)
            {
                delta = Value(16);
                alpha = RootMoves[PVIdx].prevScore - delta;
                beta  = RootMoves[PVIdx].prevScore + delta;
            }
            else
            {
                alpha = -VALUE_INFINITE;
                beta  =  VALUE_INFINITE;
            }

            // Start with a small aspiration window and, in case of fail high/low,
            // research with bigger window until not failing high/low anymore.
            do {
                // Search starts from ss+1 to allow referencing (ss-1). This is
                // needed by update gains and ss copy when splitting at Root.
                bestValue = search<Root>(pos, ss+1, alpha, beta, depth * ONE_PLY);

                // Bring to front the best move. It is critical that sorting is
                // done with a stable algorithm because all the values but the first
                // and eventually the new best one are set to -VALUE_INFINITE and
                // we want to keep the same order for all the moves but the new
                // PV that goes to the front. Note that in case of MultiPV search
                // the already searched PV lines are preserved.
                sort<RootMove>(RootMoves.begin() + PVIdx, RootMoves.end());

                // In case we have found an exact score and we are going to leave
                // the fail high/low loop then reorder the PV moves, otherwise
                // leave the last PV move in its position so to be searched again.
                // Of course this is needed only in MultiPV search.
                if (PVIdx && bestValue > alpha && bestValue < beta)
                    sort<RootMove>(RootMoves.begin(), RootMoves.begin() + PVIdx);

                // Write PV back to transposition table in case the relevant
                // entries have been overwritten during the search.
                for (size_t i = 0; i <= PVIdx; i++)
                    RootMoves[i].insert_pv_in_tt(pos);

                // If search has been stopped exit the aspiration window loop.
                // Sorting and writing PV back to TT is safe becuase RootMoves
                // is still valid, although refers to previous iteration.
                if (Signals.stop)
                    break;

                // Send full PV info to GUI if we are going to leave the loop or
                // if we have a fail high/low and we are deep in the search.
                if ((bestValue > alpha && bestValue < beta) || SearchTime.elapsed() > 2000)
                    pv_info_to_uci(pos, depth, alpha, beta);

                // In case of failing high/low increase aspiration window and
                // research, otherwise exit the fail high/low loop.
                if (bestValue >= beta)
                {
                    beta += delta;
                    delta += delta / 2;
                }
                else if (bestValue <= alpha)
                {
                    Signals.failedLowAtRoot = true;
                    Signals.stopOnPonderhit = false;

                    alpha -= delta;
                    delta += delta / 2;
                }
                else
                    break;

                assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);

            } while (abs(bestValue) < VALUE_KNOWN_WIN);
        }

        // Skills: Do we need to pick now the best move ?
        if (SkillLevelEnabled && depth == 1 + SkillLevel)
            skillBest = do_skill_level();

        if (!Signals.stop && Options["Use Search Log"])
             pv_info_to_log(pos, depth, bestValue, SearchTime.elapsed(), &RootMoves[0].pv[0]);

        // Filter out startup noise when monitoring best move stability
        if (depth > 2 && BestMoveChanges)
            bestMoveNeverChanged = false;

        // Do we have time for the next iteration? Can we stop searching now?
        if (!Signals.stop && !Signals.stopOnPonderhit && Limits.use_time_management())
        {
            bool stop = false; // Local variable, not the volatile Signals.stop

            // Take in account some extra time if the best move has changed
            if (depth > 4 && depth < 50)
                TimeMgr.pv_instability(BestMoveChanges, prevBestMoveChanges);

            // Stop search if most of available time is already consumed. We
            // probably don't have enough time to search the first move at the
            // next iteration anyway.
            if (SearchTime.elapsed() > (TimeMgr.available_time() * 62) / 100)
                stop = true;

            // Stop search early if one move seems to be much better than others
            if (    depth >= 12
                && !stop
                && (   (bestMoveNeverChanged &&  pos.captured_piece_type())
                    || SearchTime.elapsed() > (TimeMgr.available_time() * 40) / 100))
            {
                Value rBeta = bestValue - EasyMoveMargin;
                (ss+1)->excludedMove = RootMoves[0].pv[0];
                (ss+1)->skipNullMove = true;
                Value v = search<NonPV>(pos, ss+1, rBeta - 1, rBeta, (depth - 3) * ONE_PLY);
                (ss+1)->skipNullMove = false;
                (ss+1)->excludedMove = MOVE_NONE;

                if (v < rBeta)
                    stop = true;
            }

            if (stop)
            {
                // If we are allowed to ponder do not stop the search now but
                // keep pondering until GUI sends "ponderhit" or "stop".
                if (Limits.ponder)
                    Signals.stopOnPonderhit = true;
                else
                    Signals.stop = true;
            }
        }
    }

    // When using skills swap best PV line with the sub-optimal one
    if (SkillLevelEnabled)
    {
        if (skillBest == MOVE_NONE) // Still unassigned ?
            skillBest = do_skill_level();

        std::swap(RootMoves[0], *find(RootMoves.begin(), RootMoves.end(), skillBest));
    }
  }


  // search<>() is the main search function for both PV and non-PV nodes and for
  // normal and SplitPoint nodes. When called just after a split point the search
  // is simpler because we have already probed the hash table, done a null move
  // search, and searched the first move before splitting, we don't have to repeat
  // all this work again. We also don't need to store anything to the hash table
  // here: This is taken care of after we return from the split point.

  template <NodeType NT>
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) {

    const bool PvNode   = (NT == PV || NT == Root || NT == SplitPointPV || NT == SplitPointRoot);
    const bool SpNode   = (NT == SplitPointPV || NT == SplitPointNonPV || NT == SplitPointRoot);
    const bool RootNode = (NT == Root || NT == SplitPointRoot);

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert((alpha == beta - 1) || PvNode);
    assert(depth > DEPTH_ZERO);
    assert(pos.thread() >= 0 && pos.thread() < Threads.size());

    Move movesSearched[MAX_MOVES];
    StateInfo st;
    const TTEntry *tte;
    Key posKey;
    Move ttMove, move, excludedMove, bestMove, threatMove;
    Depth ext, newDepth;
    Bound bt;
    Value bestValue, value, oldAlpha, ttValue;
    Value refinedValue, nullValue, futilityBase, futilityValue;
    bool isPvMove, inCheck, singularExtensionNode, givesCheck;
    bool captureOrPromotion, dangerous, doFullDepthSearch;
    int moveCount = 0, playedMoveCount = 0;
    Thread& thread = Threads[pos.thread()];
    SplitPoint* sp = NULL;

    refinedValue = bestValue = value = -VALUE_INFINITE;
    oldAlpha = alpha;
    inCheck = pos.in_check();
    ss->ply = (ss-1)->ply + 1;

    // Used to send selDepth info to GUI
    if (PvNode && thread.maxPly < ss->ply)
        thread.maxPly = ss->ply;

    // Step 1. Initialize node
    if (SpNode)
    {
        tte = NULL;
        ttMove = excludedMove = MOVE_NONE;
        ttValue = VALUE_ZERO;
        sp = ss->sp;
        bestMove = sp->bestMove;
        threatMove = sp->threatMove;
        bestValue = sp->bestValue;
        moveCount = sp->moveCount; // Lock must be held here

        assert(bestValue > -VALUE_INFINITE && moveCount > 0);

        goto split_point_start;
    }
    else
    {
        ss->currentMove = threatMove = (ss+1)->excludedMove = bestMove = MOVE_NONE;
        (ss+1)->skipNullMove = false; (ss+1)->reduction = DEPTH_ZERO;
        (ss+2)->killers[0] = (ss+2)->killers[1] = MOVE_NONE;

    }

    // Step 2. Check for aborted search and immediate draw
    // Enforce node limit here. FIXME: This only works with 1 search thread.
    if (Limits.nodes && pos.nodes_searched() >= Limits.nodes)
        Signals.stop = true;

    if ((   Signals.stop
         || pos.is_draw<false>()
         || ss->ply > MAX_PLY) && !RootNode)
        return VALUE_DRAW;

    // Step 3. Mate distance pruning. Even if we mate at the next move our score
    // would be at best mate_in(ss->ply+1), but if alpha is already bigger because
    // a shorter mate was found upward in the tree then there is no need to search
    // further, we will never beat current alpha. Same logic but with reversed signs
    // applies also in the opposite condition of being mated instead of giving mate,
    // in this case return a fail-high score.
    if (!RootNode)
    {
        alpha = std::max(mated_in(ss->ply), alpha);
        beta = std::min(mate_in(ss->ply+1), beta);
        if (alpha >= beta)
            return alpha;
    }

    // Step 4. Transposition table lookup
    // We don't want the score of a partial search to overwrite a previous full search
    // TT value, so we use a different position key in case of an excluded move.
    excludedMove = ss->excludedMove;
    posKey = excludedMove ? pos.exclusion_key() : pos.key();
    tte = TT.probe(posKey);
    ttMove = RootNode ? RootMoves[PVIdx].pv[0] : tte ? tte->move() : MOVE_NONE;
    ttValue = tte ? value_from_tt(tte->value(), ss->ply) : VALUE_ZERO;

    // At PV nodes we check for exact scores, while at non-PV nodes we check for
    // a fail high/low. Biggest advantage at probing at PV nodes is to have a
    // smooth experience in analysis mode. We don't probe at Root nodes otherwise
    // we should also update RootMoveList to avoid bogus output.
    if (!RootNode && tte && (PvNode ? tte->depth() >= depth && tte->type() == BOUND_EXACT
                                    : can_return_tt(tte, depth, ttValue, beta)))
    {
        TT.refresh(tte);
        ss->currentMove = ttMove; // Can be MOVE_NONE

        if (   ttValue >= beta
            && ttMove
            && !pos.is_capture_or_promotion(ttMove)
            && ttMove != ss->killers[0])
        {
            ss->killers[1] = ss->killers[0];
            ss->killers[0] = ttMove;
        }
        return ttValue;
    }

    // Step 5. Evaluate the position statically and update parent's gain statistics
    if (inCheck)
        ss->eval = ss->evalMargin = VALUE_NONE;
    else if (tte)
    {
        assert(tte->static_value() != VALUE_NONE);

        ss->eval = tte->static_value();
        ss->evalMargin = tte->static_value_margin();
        refinedValue = refine_eval(tte, ttValue, ss->eval);
    }
    else
    {
        refinedValue = ss->eval = evaluate(pos, ss->evalMargin);
        TT.store(posKey, VALUE_NONE, BOUND_NONE, DEPTH_NONE, MOVE_NONE, ss->eval, ss->evalMargin);
    }

    // Update gain for the parent non-capture move given the static position
    // evaluation before and after the move.
    if (   (move = (ss-1)->currentMove) != MOVE_NULL
        && (ss-1)->eval != VALUE_NONE
        && ss->eval != VALUE_NONE
        && !pos.captured_piece_type()
        && !is_special(move))
    {
        Square to = to_sq(move);
        H.update_gain(pos.piece_on(to), to, -(ss-1)->eval - ss->eval);
    }

    // Step 6. Razoring (is omitted in PV nodes)
    if (   !PvNode
        &&  depth < RazorDepth
        && !inCheck
        &&  refinedValue + razor_margin(depth) < beta
        &&  ttMove == MOVE_NONE
        &&  abs(beta) < VALUE_MATE_IN_MAX_PLY
        && !pos.has_pawn_on_7th(pos.side_to_move()))
    {
        Value rbeta = beta - razor_margin(depth);
        Value v = qsearch<NonPV>(pos, ss, rbeta-1, rbeta, DEPTH_ZERO);
        if (v < rbeta)
            // Logically we should return (v + razor_margin(depth)), but
            // surprisingly this did slightly weaker in tests.
            return v;
    }

    // Step 7. Static null move pruning (is omitted in PV nodes)
    // We're betting that the opponent doesn't have a move that will reduce
    // the score by more than futility_margin(depth) if we do a null move.
    if (   !PvNode
        && !ss->skipNullMove
        &&  depth < RazorDepth
        && !inCheck
        &&  refinedValue - futility_margin(depth, 0) >= beta
        &&  abs(beta) < VALUE_MATE_IN_MAX_PLY
        &&  pos.non_pawn_material(pos.side_to_move()))
        return refinedValue - futility_margin(depth, 0);

    // Step 8. Null move search with verification search (is omitted in PV nodes)
    if (   !PvNode
        && !ss->skipNullMove
        &&  depth > ONE_PLY
        && !inCheck
        &&  refinedValue >= beta
        &&  abs(beta) < VALUE_MATE_IN_MAX_PLY
        &&  pos.non_pawn_material(pos.side_to_move()))
    {
        ss->currentMove = MOVE_NULL;

        // Null move dynamic reduction based on depth
        int R = 3 + (depth >= 5 * ONE_PLY ? depth / 8 : 0);

        // Null move dynamic reduction based on value
        if (refinedValue - PawnValueMidgame > beta)
            R++;

        pos.do_null_move<true>(st);
        (ss+1)->skipNullMove = true;
        nullValue = depth-R*ONE_PLY < ONE_PLY ? -qsearch<NonPV>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                              : - search<NonPV>(pos, ss+1, -beta, -alpha, depth-R*ONE_PLY);
        (ss+1)->skipNullMove = false;
        pos.do_null_move<false>(st);

        if (nullValue >= beta)
        {
            // Do not return unproven mate scores
            if (nullValue >= VALUE_MATE_IN_MAX_PLY)
                nullValue = beta;

            if (depth < 6 * ONE_PLY)
                return nullValue;

            // Do verification search at high depths
            ss->skipNullMove = true;
            Value v = search<NonPV>(pos, ss, alpha, beta, depth-R*ONE_PLY);
            ss->skipNullMove = false;

            if (v >= beta)
                return nullValue;
        }
        else
        {
            // The null move failed low, which means that we may be faced with
            // some kind of threat. If the previous move was reduced, check if
            // the move that refuted the null move was somehow connected to the
            // move which was reduced. If a connection is found, return a fail
            // low score (which will cause the reduced move to fail high in the
            // parent node, which will trigger a re-search with full depth).
            threatMove = (ss+1)->currentMove;

            if (   depth < ThreatDepth
                && (ss-1)->reduction
                && threatMove != MOVE_NONE
                && connected_moves(pos, (ss-1)->currentMove, threatMove))
                return beta - 1;
        }
    }

    // Step 9. ProbCut (is omitted in PV nodes)
    // If we have a very good capture (i.e. SEE > seeValues[captured_piece_type])
    // and a reduced search returns a value much above beta, we can (almost) safely
    // prune the previous move.
    if (   !PvNode
        &&  depth >= RazorDepth + ONE_PLY
        && !inCheck
        && !ss->skipNullMove
        &&  excludedMove == MOVE_NONE
        &&  abs(beta) < VALUE_MATE_IN_MAX_PLY)
    {
        Value rbeta = beta + 200;
        Depth rdepth = depth - ONE_PLY - 3 * ONE_PLY;

        assert(rdepth >= ONE_PLY);
        assert((ss-1)->currentMove != MOVE_NONE);
        assert((ss-1)->currentMove != MOVE_NULL);

        MovePicker mp(pos, ttMove, H, pos.captured_piece_type());
        CheckInfo ci(pos);

        while ((move = mp.next_move()) != MOVE_NONE)
            if (pos.pl_move_is_legal(move, ci.pinned))
            {
                ss->currentMove = move;
                pos.do_move(move, st, ci, pos.move_gives_check(move, ci));
                value = -search<NonPV>(pos, ss+1, -rbeta, -rbeta+1, rdepth);
                pos.undo_move(move);
                if (value >= rbeta)
                    return value;
            }
    }

    // Step 10. Internal iterative deepening
    if (   depth >= IIDDepth[PvNode]
        && ttMove == MOVE_NONE
        && (PvNode || (!inCheck && ss->eval + IIDMargin >= beta)))
    {
        Depth d = (PvNode ? depth - 2 * ONE_PLY : depth / 2);

        ss->skipNullMove = true;
        search<PvNode ? PV : NonPV>(pos, ss, alpha, beta, d);
        ss->skipNullMove = false;

        tte = TT.probe(posKey);
        ttMove = tte ? tte->move() : MOVE_NONE;
    }

split_point_start: // At split points actual search starts from here

    MovePickerExt<SpNode> mp(pos, ttMove, depth, H, ss, PvNode ? -VALUE_INFINITE : beta);
    CheckInfo ci(pos);
    futilityBase = ss->eval + ss->evalMargin;
    singularExtensionNode =   !RootNode
                           && !SpNode
                           && depth >= SingularExtensionDepth[PvNode]
                           && ttMove != MOVE_NONE
                           && !excludedMove // Recursive singular search is not allowed
                           && (tte->type() & BOUND_LOWER)
                           && tte->depth() >= depth - 3 * ONE_PLY;

    // Step 11. Loop through moves
    // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
    while (   bestValue < beta
           && (move = mp.next_move()) != MOVE_NONE
           && !thread.cutoff_occurred()
           && !Signals.stop)
    {
      assert(is_ok(move));

      if (move == excludedMove)
          continue;

      // At root obey the "searchmoves" option and skip moves not listed in Root
      // Move List, as a consequence any illegal move is also skipped. In MultiPV
      // mode we also skip PV moves which have been already searched.
      if (RootNode && !count(RootMoves.begin() + PVIdx, RootMoves.end(), move))
          continue;

      // At PV and SpNode nodes we want all moves to be legal since the beginning
      if ((PvNode || SpNode) && !pos.pl_move_is_legal(move, ci.pinned))
          continue;

      if (SpNode)
      {
          moveCount = ++sp->moveCount;
          lock_release(sp->lock);
      }
      else
          moveCount++;

      if (RootNode)
      {
          Signals.firstRootMove = (moveCount == 1);

          if (pos.thread() == 0 && SearchTime.elapsed() > 2000)
              cout << "info depth " << depth / ONE_PLY
                   << " currmove " << move_to_uci(move, Chess960)
                   << " currmovenumber " << moveCount + PVIdx << endl;
      }

      isPvMove = (PvNode && moveCount <= 1);
      captureOrPromotion = pos.is_capture_or_promotion(move);
      givesCheck = pos.move_gives_check(move, ci);
      dangerous = givesCheck || is_dangerous(pos, move, captureOrPromotion);
      ext = DEPTH_ZERO;

      // Step 12. Extend checks and, in PV nodes, also dangerous moves
      if (PvNode && dangerous)
          ext = ONE_PLY;

      else if (givesCheck && pos.see_sign(move) >= 0)
          ext = PvNode ? ONE_PLY : ONE_PLY / 2;

      // Singular extension search. If all moves but one fail low on a search of
      // (alpha-s, beta-s), and just one fails high on (alpha, beta), then that move
      // is singular and should be extended. To verify this we do a reduced search
      // on all the other moves but the ttMove, if result is lower than ttValue minus
      // a margin then we extend ttMove.
      if (   singularExtensionNode
          && !ext
          && move == ttMove
          && pos.pl_move_is_legal(move, ci.pinned))
      {
          if (abs(ttValue) < VALUE_KNOWN_WIN)
          {
              Value rBeta = ttValue - int(depth);
              ss->excludedMove = move;
              ss->skipNullMove = true;
              value = search<NonPV>(pos, ss, rBeta - 1, rBeta, depth / 2);
              ss->skipNullMove = false;
              ss->excludedMove = MOVE_NONE;
              if (value < rBeta)
                  ext = ONE_PLY;
          }
      }

      // Update current move (this must be done after singular extension search)
      newDepth = depth - ONE_PLY + ext;

      // Step 13. Futility pruning (is omitted in PV nodes)
      if (   !PvNode
          && !captureOrPromotion
          && !inCheck
          && !dangerous
          &&  move != ttMove
          && !is_castle(move)
          && (bestValue > VALUE_MATED_IN_MAX_PLY || bestValue == -VALUE_INFINITE))
      {
          // Move count based pruning
          if (   moveCount >= futility_move_count(depth)
              && (!threatMove || !connected_threat(pos, move, threatMove)))
          {
              if (SpNode)
                  lock_grab(sp->lock);

              continue;
          }

          // Value based pruning
          // We illogically ignore reduction condition depth >= 3*ONE_PLY for predicted depth,
          // but fixing this made program slightly weaker.
          Depth predictedDepth = newDepth - reduction<PvNode>(depth, moveCount);
          futilityValue =  futilityBase + futility_margin(predictedDepth, moveCount)
                         + H.gain(pos.piece_moved(move), to_sq(move));

          if (futilityValue < beta)
          {
              if (SpNode)
                  lock_grab(sp->lock);

              continue;
          }

          // Prune moves with negative SEE at low depths
          if (   predictedDepth < 2 * ONE_PLY
              && pos.see_sign(move) < 0)
          {
              if (SpNode)
                  lock_grab(sp->lock);

              continue;
          }
      }

      // Check for legality only before to do the move
      if (!pos.pl_move_is_legal(move, ci.pinned))
      {
          moveCount--;
          continue;
      }

      ss->currentMove = move;
      if (!SpNode && !captureOrPromotion)
          movesSearched[playedMoveCount++] = move;

      // Step 14. Make the move
      pos.do_move(move, st, ci, givesCheck);

      // Step 15. Reduced depth search (LMR). If the move fails high will be
      // re-searched at full depth.
      if (   depth > 3 * ONE_PLY
          && !isPvMove
          && !captureOrPromotion
          && !dangerous
          && !is_castle(move)
          &&  ss->killers[0] != move
          &&  ss->killers[1] != move)
      {
          ss->reduction = reduction<PvNode>(depth, moveCount);
          Depth d = std::max(newDepth - ss->reduction, ONE_PLY);
          alpha = SpNode ? sp->alpha : alpha;

          value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, d);

          doFullDepthSearch = (value > alpha && ss->reduction != DEPTH_ZERO);
          ss->reduction = DEPTH_ZERO;
      }
      else
          doFullDepthSearch = !isPvMove;

      // Step 16. Full depth search, when LMR is skipped or fails high
      if (doFullDepthSearch)
      {
          alpha = SpNode ? sp->alpha : alpha;
          value = newDepth < ONE_PLY ? -qsearch<NonPV>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
                                     : - search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth);
      }

      // Only for PV nodes do a full PV search on the first move or after a fail
      // high, in the latter case search only if value < beta, otherwise let the
      // parent node to fail low with value <= alpha and to try another move.
      if (PvNode && (isPvMove || (value > alpha && (RootNode || value < beta))))
          value = newDepth < ONE_PLY ? -qsearch<PV>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                     : - search<PV>(pos, ss+1, -beta, -alpha, newDepth);

      // Step 17. Undo move
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Step 18. Check for new best move
      if (SpNode)
      {
          lock_grab(sp->lock);
          bestValue = sp->bestValue;
          alpha = sp->alpha;
      }

      // Finished searching the move. If Signals.stop is true, the search
      // was aborted because the user interrupted the search or because we
      // ran out of time. In this case, the return value of the search cannot
      // be trusted, and we don't update the best move and/or PV.
      if (RootNode && !Signals.stop)
      {
          RootMove& rm = *find(RootMoves.begin(), RootMoves.end(), move);

          // PV move or new best move ?
          if (isPvMove || value > alpha)
          {
              rm.score = value;
              rm.extract_pv_from_tt(pos);

              // We record how often the best move has been changed in each
              // iteration. This information is used for time management: When
              // the best move changes frequently, we allocate some more time.
              if (!isPvMove && MultiPV == 1)
                  BestMoveChanges++;
          }
          else
              // All other moves but the PV are set to the lowest value, this
              // is not a problem when sorting becuase sort is stable and move
              // position in the list is preserved, just the PV is pushed up.
              rm.score = -VALUE_INFINITE;

      }

      if (value > bestValue)
      {
          bestValue = value;
          bestMove = move;

          if (   PvNode
              && value > alpha
              && value < beta) // We want always alpha < beta
              alpha = value;

          if (SpNode && !thread.cutoff_occurred())
          {
              sp->bestValue = value;
              sp->bestMove = move;
              sp->alpha = alpha;

              if (value >= beta)
                  sp->cutoff = true;
          }
      }

      // Step 19. Check for split
      if (   !SpNode
          && depth >= Threads.min_split_depth()
          && bestValue < beta
          && Threads.available_slave_exists(pos.thread())
          && !Signals.stop
          && !thread.cutoff_occurred())
          bestValue = Threads.split<FakeSplit>(pos, ss, alpha, beta, bestValue, &bestMove,
                                               depth, threatMove, moveCount, &mp, NT);
    }

    // Step 20. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it
    // must be mate or stalemate. Note that we can have a false positive in
    // case of Signals.stop or thread.cutoff_occurred() are set, but this is
    // harmless because return value is discarded anyhow in the parent nodes.
    // If we are in a singular extension search then return a fail low score.
    if (!moveCount)
        return excludedMove ? oldAlpha : inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If we have pruned all the moves without searching return a fail-low score
    if (bestValue == -VALUE_INFINITE)
    {
        assert(!playedMoveCount);

        bestValue = oldAlpha;
    }

    // Step 21. Update tables
    // Update transposition table entry, killers and history
    if (!SpNode && !Signals.stop && !thread.cutoff_occurred())
    {
        move = bestValue <= oldAlpha ? MOVE_NONE : bestMove;
        bt   = bestValue <= oldAlpha ? BOUND_UPPER
             : bestValue >= beta ? BOUND_LOWER : BOUND_EXACT;

        TT.store(posKey, value_to_tt(bestValue, ss->ply), bt, depth, move, ss->eval, ss->evalMargin);

        // Update killers and history for non capture cut-off moves
        if (    bestValue >= beta
            && !pos.is_capture_or_promotion(move)
            && !inCheck)
        {
            if (move != ss->killers[0])
            {
                ss->killers[1] = ss->killers[0];
                ss->killers[0] = move;
            }

            // Increase history value of the cut-off move
            Value bonus = Value(int(depth) * int(depth));
            H.add(pos.piece_moved(move), to_sq(move), bonus);

            // Decrease history of all the other played non-capture moves
            for (int i = 0; i < playedMoveCount - 1; i++)
            {
                Move m = movesSearched[i];
                H.add(pos.piece_moved(m), to_sq(m), -bonus);
            }
        }
    }

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }


  // qsearch() is the quiescence search function, which is called by the main
  // search function when the remaining depth is zero (or, to be more precise,
  // less than ONE_PLY).

  template <NodeType NT>
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) {

    const bool PvNode = (NT == PV);

    assert(NT == PV || NT == NonPV);
    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert((alpha == beta - 1) || PvNode);
    assert(depth <= DEPTH_ZERO);
    assert(pos.thread() >= 0 && pos.thread() < Threads.size());

    StateInfo st;
    Move ttMove, move, bestMove;
    Value ttValue, bestValue, value, evalMargin, futilityValue, futilityBase;
    bool inCheck, enoughMaterial, givesCheck, evasionPrunable;
    const TTEntry* tte;
    Depth ttDepth;
    Bound bt;
    Value oldAlpha = alpha;

    ss->currentMove = bestMove = MOVE_NONE;
    ss->ply = (ss-1)->ply + 1;

    // Check for an instant draw or maximum ply reached
    if (pos.is_draw<true>() || ss->ply > MAX_PLY)
        return VALUE_DRAW;

    // Decide whether or not to include checks, this fixes also the type of
    // TT entry depth that we are going to use. Note that in qsearch we use
    // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
    inCheck = pos.in_check();
    ttDepth = (inCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS);

    // Transposition table lookup. At PV nodes, we don't use the TT for
    // pruning, but only for move ordering.
    tte = TT.probe(pos.key());
    ttMove = (tte ? tte->move() : MOVE_NONE);
    ttValue = tte ? value_from_tt(tte->value(),ss->ply) : VALUE_ZERO;

    if (!PvNode && tte && can_return_tt(tte, ttDepth, ttValue, beta))
    {
        ss->currentMove = ttMove; // Can be MOVE_NONE
        return ttValue;
    }

    // Evaluate the position statically
    if (inCheck)
    {
        bestValue = futilityBase = -VALUE_INFINITE;
        ss->eval = evalMargin = VALUE_NONE;
        enoughMaterial = false;
    }
    else
    {
        if (tte)
        {
            assert(tte->static_value() != VALUE_NONE);

            evalMargin = tte->static_value_margin();
            ss->eval = bestValue = tte->static_value();
        }
        else
            ss->eval = bestValue = evaluate(pos, evalMargin);

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (!tte)
                TT.store(pos.key(), value_to_tt(bestValue, ss->ply), BOUND_LOWER, DEPTH_NONE, MOVE_NONE, ss->eval, evalMargin);

            return bestValue;
        }

        if (PvNode && bestValue > alpha)
            alpha = bestValue;

        futilityBase = ss->eval + evalMargin + FutilityMarginQS;
        enoughMaterial = pos.non_pawn_material(pos.side_to_move()) > RookValueMidgame;
    }

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions and checks (only if depth >= DEPTH_QS_CHECKS) will
    // be generated.
    MovePicker mp(pos, ttMove, depth, H, to_sq((ss-1)->currentMove));
    CheckInfo ci(pos);

    // Loop through the moves until no moves remain or a beta cutoff occurs
    while (   bestValue < beta
           && (move = mp.next_move()) != MOVE_NONE)
    {
      assert(is_ok(move));

      givesCheck = pos.move_gives_check(move, ci);

      // Futility pruning
      if (   !PvNode
          && !inCheck
          && !givesCheck
          &&  move != ttMove
          &&  enoughMaterial
          && !is_promotion(move)
          && !pos.is_passed_pawn_push(move))
      {
          futilityValue =  futilityBase
                         + PieceValueEndgame[pos.piece_on(to_sq(move))]
                         + (is_enpassant(move) ? PawnValueEndgame : VALUE_ZERO);

          if (futilityValue < beta)
          {
              if (futilityValue > bestValue)
                  bestValue = futilityValue;

              continue;
          }

          // Prune moves with negative or equal SEE
          if (   futilityBase < beta
              && depth < DEPTH_ZERO
              && pos.see(move) <= 0)
              continue;
      }

      // Detect non-capture evasions that are candidate to be pruned
      evasionPrunable =   !PvNode
                       && inCheck
                       && bestValue > VALUE_MATED_IN_MAX_PLY
                       && !pos.is_capture(move)
                       && !pos.can_castle(pos.side_to_move());

      // Don't search moves with negative SEE values
      if (   !PvNode
          && (!inCheck || evasionPrunable)
          &&  move != ttMove
          && !is_promotion(move)
          &&  pos.see_sign(move) < 0)
          continue;

      // Don't search useless checks
      if (   !PvNode
          && !inCheck
          &&  givesCheck
          &&  move != ttMove
          && !pos.is_capture_or_promotion(move)
          &&  ss->eval + PawnValueMidgame / 4 < beta
          && !check_is_dangerous(pos, move, futilityBase, beta))
          continue;

      // Check for legality only before to do the move
      if (!pos.pl_move_is_legal(move, ci.pinned))
          continue;

      ss->currentMove = move;

      // Make and search the move
      pos.do_move(move, st, ci, givesCheck);
      value = -qsearch<NT>(pos, ss+1, -beta, -alpha, depth-ONE_PLY);
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // New best move?
      if (value > bestValue)
      {
          bestValue = value;
          bestMove = move;

          if (   PvNode
              && value > alpha
              && value < beta) // We want always alpha < beta
              alpha = value;
       }
    }

    // All legal moves have been searched. A special case: If we're in check
    // and no legal moves were found, it is checkmate.
    if (inCheck && bestValue == -VALUE_INFINITE)
        return mated_in(ss->ply); // Plies to mate from the root

    // Update transposition table
    move = bestValue <= oldAlpha ? MOVE_NONE : bestMove;
    bt   = bestValue <= oldAlpha ? BOUND_UPPER
         : bestValue >= beta ? BOUND_LOWER : BOUND_EXACT;

    TT.store(pos.key(), value_to_tt(bestValue, ss->ply), bt, ttDepth, move, ss->eval, evalMargin);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }


  // check_is_dangerous() tests if a checking move can be pruned in qsearch().
  // bestValue is updated only when returning false because in that case move
  // will be pruned.

  bool check_is_dangerous(Position &pos, Move move, Value futilityBase, Value beta)
  {
    Bitboard b, occ, oldAtt, newAtt, kingAtt;
    Square from, to, ksq;
    Piece pc;
    Color them;

    from = from_sq(move);
    to = to_sq(move);
    them = ~pos.side_to_move();
    ksq = pos.king_square(them);
    kingAtt = pos.attacks_from<KING>(ksq);
    pc = pos.piece_moved(move);

    occ = pos.pieces() ^ from ^ ksq;
    oldAtt = pos.attacks_from(pc, from, occ);
    newAtt = pos.attacks_from(pc,   to, occ);

    // Rule 1. Checks which give opponent's king at most one escape square are dangerous
    b = kingAtt & ~pos.pieces(them) & ~newAtt & ~(1ULL << to);

    if (single_bit(b)) // Catches also !b
        return true;

    // Rule 2. Queen contact check is very dangerous
    if (type_of(pc) == QUEEN && (kingAtt & to))
        return true;

    // Rule 3. Creating new double threats with checks
    b = pos.pieces(them) & newAtt & ~oldAtt & ~(1ULL << ksq);
    while (b)
    {
        // Note that here we generate illegal "double move"!
        if (futilityBase + PieceValueEndgame[pos.piece_on(pop_1st_bit(&b))] >= beta)
            return true;
    }

    return false;
  }


  // connected_moves() tests whether two moves are 'connected' in the sense
  // that the first move somehow made the second move possible (for instance
  // if the moving piece is the same in both moves). The first move is assumed
  // to be the move that was made to reach the current position, while the
  // second move is assumed to be a move from the current position.

  bool connected_moves(const Position& pos, Move m1, Move m2) {

    Square f1, t1, f2, t2;
    Piece p1, p2;
    Square ksq;

    assert(is_ok(m1));
    assert(is_ok(m2));

    // Case 1: The moving piece is the same in both moves
    f2 = from_sq(m2);
    t1 = to_sq(m1);
    if (f2 == t1)
        return true;

    // Case 2: The destination square for m2 was vacated by m1
    t2 = to_sq(m2);
    f1 = from_sq(m1);
    if (t2 == f1)
        return true;

    // Case 3: Moving through the vacated square
    p2 = pos.piece_on(f2);
    if (piece_is_slider(p2) && (squares_between(f2, t2) & f1))
      return true;

    // Case 4: The destination square for m2 is defended by the moving piece in m1
    p1 = pos.piece_on(t1);
    if (pos.attacks_from(p1, t1) & t2)
        return true;

    // Case 5: Discovered check, checking piece is the piece moved in m1
    ksq = pos.king_square(pos.side_to_move());
    if (    piece_is_slider(p1)
        && (squares_between(t1, ksq) & f2)
        && (pos.attacks_from(p1, t1, pos.pieces() ^ f2) & ksq))
        return true;

    return false;
  }


  // value_to_tt() adjusts a mate score from "plies to mate from the root" to
  // "plies to mate from the current position". Non-mate scores are unchanged.
  // The function is called before storing a value to the transposition table.

  Value value_to_tt(Value v, int ply) {

    if (v >= VALUE_MATE_IN_MAX_PLY)
      return v + ply;

    if (v <= VALUE_MATED_IN_MAX_PLY)
      return v - ply;

    return v;
  }


  // value_from_tt() is the inverse of value_to_tt(): It adjusts a mate score
  // from the transposition table (where refers to the plies to mate/be mated
  // from current position) to "plies to mate/be mated from the root".

  Value value_from_tt(Value v, int ply) {

    if (v >= VALUE_MATE_IN_MAX_PLY)
      return v - ply;

    if (v <= VALUE_MATED_IN_MAX_PLY)
      return v + ply;

    return v;
  }


  // connected_threat() tests whether it is safe to forward prune a move or if
  // is somehow connected to the threat move returned by null search.

  bool connected_threat(const Position& pos, Move m, Move threat) {

    assert(is_ok(m));
    assert(is_ok(threat));
    assert(!pos.is_capture_or_promotion(m));
    assert(!pos.is_passed_pawn_push(m));

    Square mfrom, mto, tfrom, tto;

    mfrom = from_sq(m);
    mto = to_sq(m);
    tfrom = from_sq(threat);
    tto = to_sq(threat);

    // Case 1: Don't prune moves which move the threatened piece
    if (mfrom == tto)
        return true;

    // Case 2: If the threatened piece has value less than or equal to the
    // value of the threatening piece, don't prune moves which defend it.
    if (   pos.is_capture(threat)
        && (   PieceValueMidgame[pos.piece_on(tfrom)] >= PieceValueMidgame[pos.piece_on(tto)]
            || type_of(pos.piece_on(tfrom)) == KING)
        && pos.move_attacks_square(m, tto))
        return true;

    // Case 3: If the moving piece in the threatened move is a slider, don't
    // prune safe moves which block its ray.
    if (    piece_is_slider(pos.piece_on(tfrom))
        && (squares_between(tfrom, tto) & mto)
        &&  pos.see_sign(m) >= 0)
        return true;

    return false;
  }


  // can_return_tt() returns true if a transposition table score can be used to
  // cut-off at a given point in search.

  bool can_return_tt(const TTEntry* tte, Depth depth, Value v, Value beta) {

    return   (   tte->depth() >= depth
              || v >= std::max(VALUE_MATE_IN_MAX_PLY, beta)
              || v < std::min(VALUE_MATED_IN_MAX_PLY, beta))

          && (   ((tte->type() & BOUND_LOWER) && v >= beta)
              || ((tte->type() & BOUND_UPPER) && v < beta));
  }


  // refine_eval() returns the transposition table score if possible, otherwise
  // falls back on static position evaluation.

  Value refine_eval(const TTEntry* tte, Value v, Value defaultEval) {

      assert(tte);

      if (   ((tte->type() & BOUND_LOWER) && v >= defaultEval)
          || ((tte->type() & BOUND_UPPER) && v < defaultEval))
          return v;

      return defaultEval;
  }


  // score_to_uci() converts a value to a string suitable for use with the UCI
  // protocol specifications:
  //
  // cp <x>     The score from the engine's point of view in centipawns.
  // mate <y>   Mate in y moves, not plies. If the engine is getting mated
  //            use negative values for y.

  string score_to_uci(Value v, Value alpha, Value beta) {

    std::stringstream s;

    if (abs(v) < VALUE_MATE_IN_MAX_PLY)
        s << "cp " << v * 100 / int(PawnValueMidgame);
    else
        s << "mate " << (v > 0 ? VALUE_MATE - v + 1 : -VALUE_MATE - v) / 2;

    s << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

    return s.str();
  }


  // pv_info_to_uci() sends search info to GUI. UCI protocol requires to send all
  // the PV lines also if are still to be searched and so refer to the previous
  // search score.

  void pv_info_to_uci(const Position& pos, int depth, Value alpha, Value beta) {

    int t = SearchTime.elapsed();
    int selDepth = 0;

    for (int i = 0; i < Threads.size(); i++)
        if (Threads[i].maxPly > selDepth)
            selDepth = Threads[i].maxPly;

    for (size_t i = 0; i < std::min(UCIMultiPV, RootMoves.size()); i++)
    {
        bool updated = (i <= PVIdx);

        if (depth == 1 && !updated)
            continue;

        int d = (updated ? depth : depth - 1);
        Value v = (updated ? RootMoves[i].score : RootMoves[i].prevScore);
        std::stringstream s;

        for (int j = 0; RootMoves[i].pv[j] != MOVE_NONE; j++)
            s <<  " " << move_to_uci(RootMoves[i].pv[j], Chess960);

        cout << "info depth " << d
             << " seldepth " << selDepth
             << " score " << (i == PVIdx ? score_to_uci(v, alpha, beta) : score_to_uci(v))
             << " nodes " << pos.nodes_searched()
             << " nps " << (t > 0 ? pos.nodes_searched() * 1000 / t : 0)
             << " time " << t
             << " multipv " << i + 1
             << " pv" << s.str() << endl;
    }
  }


  // pv_info_to_log() writes human-readable search information to the log file
  // (which is created when the UCI parameter "Use Search Log" is "true"). It
  // uses the two below helpers to pretty format time and score respectively.

  string time_to_string(int millisecs) {

    const int MSecMinute = 1000 * 60;
    const int MSecHour   = 1000 * 60 * 60;

    int hours = millisecs / MSecHour;
    int minutes =  (millisecs % MSecHour) / MSecMinute;
    int seconds = ((millisecs % MSecHour) % MSecMinute) / 1000;

    std::stringstream s;

    if (hours)
        s << hours << ':';

    s << std::setfill('0') << std::setw(2) << minutes << ':'
                           << std::setw(2) << seconds;
    return s.str();
  }

  string score_to_string(Value v) {

    std::stringstream s;

    if (v >= VALUE_MATE_IN_MAX_PLY)
        s << "#" << (VALUE_MATE - v + 1) / 2;
    else if (v <= VALUE_MATED_IN_MAX_PLY)
        s << "-#" << (VALUE_MATE + v) / 2;
    else
        s << std::setprecision(2) << std::fixed << std::showpos
          << float(v) / PawnValueMidgame;

    return s.str();
  }

  void pv_info_to_log(Position& pos, int depth, Value value, int time, Move pv[]) {

    const int64_t K = 1000;
    const int64_t M = 1000000;

    StateInfo state[MAX_PLY_PLUS_2], *st = state;
    Move* m = pv;
    string san, padding;
    size_t length;
    std::stringstream s;

    s << std::setw(2) << depth
      << std::setw(8) << score_to_string(value)
      << std::setw(8) << time_to_string(time);

    if (pos.nodes_searched() < M)
        s << std::setw(8) << pos.nodes_searched() / 1 << "  ";

    else if (pos.nodes_searched() < K * M)
        s << std::setw(7) << pos.nodes_searched() / K << "K  ";

    else
        s << std::setw(7) << pos.nodes_searched() / M << "M  ";

    padding = string(s.str().length(), ' ');
    length = padding.length();

    while (*m != MOVE_NONE)
    {
        san = move_to_san(pos, *m);

        if (length + san.length() > 80)
        {
            s << "\n" + padding;
            length = padding.length();
        }

        s << san << ' ';
        length += san.length() + 1;

        pos.do_move(*m++, *st++);
    }

    while (m != pv)
        pos.undo_move(*--m);

    Log l(Options["Search Log Filename"]);
    l << s.str() << endl;
  }


  // When playing with strength handicap choose best move among the MultiPV set
  // using a statistical rule dependent on SkillLevel. Idea by Heinz van Saanen.

  Move do_skill_level() {

    assert(MultiPV > 1);

    static RKISS rk;

    // PRNG sequence should be not deterministic
    for (int i = Time::current_time().msec() % 50; i > 0; i--)
        rk.rand<unsigned>();

    // RootMoves are already sorted by score in descending order
    size_t size = std::min(MultiPV, RootMoves.size());
    int variance = std::min(RootMoves[0].score - RootMoves[size - 1].score, PawnValueMidgame);
    int weakness = 120 - 2 * SkillLevel;
    int max_s = -VALUE_INFINITE;
    Move best = MOVE_NONE;

    // Choose best move. For each move score we add two terms both dependent on
    // weakness, one deterministic and bigger for weaker moves, and one random,
    // then we choose the move with the resulting highest score.
    for (size_t i = 0; i < size; i++)
    {
        int s = RootMoves[i].score;

        // Don't allow crazy blunders even at very low skills
        if (i > 0 && RootMoves[i-1].score > s + EasyMoveMargin)
            break;

        // This is our magic formula
        s += (  weakness * int(RootMoves[0].score - s)
              + variance * (rk.rand<unsigned>() % weakness)) / 128;

        if (s > max_s)
        {
            max_s = s;
            best = RootMoves[i].pv[0];
        }
    }
    return best;
  }

} // namespace


/// RootMove::extract_pv_from_tt() builds a PV by adding moves from the TT table.
/// We consider also failing high nodes and not only BOUND_EXACT nodes so to
/// allow to always have a ponder move even when we fail high at root, and a
/// long PV to print that is important for position analysis.

void RootMove::extract_pv_from_tt(Position& pos) {

  StateInfo state[MAX_PLY_PLUS_2], *st = state;
  TTEntry* tte;
  int ply = 1;
  Move m = pv[0];

  assert(m != MOVE_NONE && pos.is_pseudo_legal(m));

  pv.clear();
  pv.push_back(m);
  pos.do_move(m, *st++);

  while (   (tte = TT.probe(pos.key())) != NULL
         && (m = tte->move()) != MOVE_NONE // Local copy, TT entry could change
         && pos.is_pseudo_legal(m)
         && pos.pl_move_is_legal(m, pos.pinned_pieces())
         && ply < MAX_PLY
         && (!pos.is_draw<false>() || ply < 2))
  {
      pv.push_back(m);
      pos.do_move(m, *st++);
      ply++;
  }
  pv.push_back(MOVE_NONE);

  do pos.undo_move(pv[--ply]); while (ply);
}


/// RootMove::insert_pv_in_tt() is called at the end of a search iteration, and
/// inserts the PV back into the TT. This makes sure the old PV moves are searched
/// first, even if the old TT entries have been overwritten.

void RootMove::insert_pv_in_tt(Position& pos) {

  StateInfo state[MAX_PLY_PLUS_2], *st = state;
  TTEntry* tte;
  Key k;
  Value v, m = VALUE_NONE;
  int ply = 0;

  assert(pv[ply] != MOVE_NONE && pos.is_pseudo_legal(pv[ply]));

  do {
      k = pos.key();
      tte = TT.probe(k);

      // Don't overwrite existing correct entries
      if (!tte || tte->move() != pv[ply])
      {
          v = (pos.in_check() ? VALUE_NONE : evaluate(pos, m));
          TT.store(k, VALUE_NONE, BOUND_NONE, DEPTH_NONE, pv[ply], v, m);
      }
      pos.do_move(pv[ply], *st++);

  } while (pv[++ply] != MOVE_NONE);

  do pos.undo_move(pv[--ply]); while (ply);
}


/// Thread::idle_loop() is where the thread is parked when it has no work to do.
/// The parameter 'master_sp', if non-NULL, is a pointer to an active SplitPoint
/// object for which the thread is the master.

void Thread::idle_loop(SplitPoint* sp_master) {

  // If this thread is the master of a split point and all slaves have
  // finished their work at this split point, return from the idle loop.
  while (!sp_master || sp_master->slavesMask)
  {
      // If we are not searching, wait for a condition to be signaled
      // instead of wasting CPU time polling for work.
      while (   do_sleep
             || do_exit
             || (!is_searching && Threads.use_sleeping_threads()))
      {
          if (do_exit)
          {
              assert(!sp_master);
              return;
          }

          // Grab the lock to avoid races with Thread::wake_up()
          lock_grab(sleepLock);

          // If we are master and all slaves have finished don't go to sleep
          if (sp_master && !sp_master->slavesMask)
          {
              lock_release(sleepLock);
              break;
          }

          // Do sleep after retesting sleep conditions under lock protection, in
          // particular we need to avoid a deadlock in case a master thread has,
          // in the meanwhile, allocated us and sent the wake_up() call before we
          // had the chance to grab the lock.
          if (do_sleep || !is_searching)
              cond_wait(sleepCond, sleepLock);

          lock_release(sleepLock);
      }

      // If this thread has been assigned work, launch a search
      if (is_searching)
      {
          assert(!do_sleep && !do_exit);

          lock_grab(Threads.splitLock);

          assert(is_searching);
          SplitPoint* sp = curSplitPoint;

          lock_release(Threads.splitLock);

          Stack ss[MAX_PLY_PLUS_2];
          Position pos(*sp->pos, threadID);
          int master = sp->master;

          memcpy(ss, sp->ss - 1, 4 * sizeof(Stack));
          (ss+1)->sp = sp;

          lock_grab(sp->lock);

          if (sp->nodeType == Root)
              search<SplitPointRoot>(pos, ss+1, sp->alpha, sp->beta, sp->depth);
          else if (sp->nodeType == PV)
              search<SplitPointPV>(pos, ss+1, sp->alpha, sp->beta, sp->depth);
          else if (sp->nodeType == NonPV)
              search<SplitPointNonPV>(pos, ss+1, sp->alpha, sp->beta, sp->depth);
          else
              assert(false);

          assert(is_searching);

          is_searching = false;
          sp->slavesMask &= ~(1ULL << threadID);
          sp->nodes += pos.nodes_searched();

          // After releasing the lock we cannot access anymore any SplitPoint
          // related data in a reliably way becuase it could have been released
          // under our feet by the sp master.
          lock_release(sp->lock);

          // Wake up master thread so to allow it to return from the idle loop in
          // case we are the last slave of the split point.
          if (   Threads.use_sleeping_threads()
              && threadID != master
              && !Threads[master].is_searching)
              Threads[master].wake_up();
      }
  }
}


/// check_time() is called by the timer thread when the timer triggers. It is
/// used to print debug info and, more important, to detect when we are out of
/// available time and so stop the search.

void check_time() {

  static Time lastInfoTime = Time::current_time();

  if (lastInfoTime.elapsed() >= 1000)
  {
      lastInfoTime.restart();
      dbg_print();
  }

  if (Limits.ponder)
      return;

  int e = SearchTime.elapsed();
  bool stillAtFirstMove =    Signals.firstRootMove
                         && !Signals.failedLowAtRoot
                         &&  e > TimeMgr.available_time();

  bool noMoreTime =   e > TimeMgr.maximum_time() - 2 * TimerResolution
                   || stillAtFirstMove;

  if (   (Limits.use_time_management() && noMoreTime)
      || (Limits.movetime && e >= Limits.movetime))
      Signals.stop = true;
}
