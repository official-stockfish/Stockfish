/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad

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
#include <cstring>   // For std::memset
#include <iostream>
#include <sstream>

#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "search.h"
#include "timeman.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

namespace Search {

  volatile SignalsType Signals;
  LimitsType Limits;
  RootMoveVector RootMoves;
  Position RootPos;
  TimePoint SearchTime;
  StateStackPtr SetupStates;
}

namespace Tablebases {

  int Cardinality;
  uint64_t Hits;
  bool RootInTB;
  bool UseRule50;
  Depth ProbeDepth;
  Value Score;
}

namespace TB = Tablebases;

using std::string;
using Eval::evaluate;
using namespace Search;

namespace {

  // Different node types, used as template parameter
  enum NodeType { Root, PV, NonPV };

  // Razoring and futility margin based on depth
  inline Value razor_margin(Depth d) { return Value(512 + 32 * d); }
  inline Value futility_margin(Depth d) { return Value(200 * d); }

  // Futility and reductions lookup tables, initialized at startup
  int FutilityMoveCounts[2][16];  // [improving][depth]
  Depth Reductions[2][2][64][64]; // [pv][improving][depth][moveNumber]

  template <bool PvNode> inline Depth reduction(bool i, Depth d, int mn) {
    return Reductions[PvNode][i][std::min(d, 63 * ONE_PLY)][std::min(mn, 63)];
  }

  // Skill struct is used to implement strength limiting
  struct Skill {
    Skill(int l) : level(l) {}
    bool enabled() const { return level < 20; }
    bool time_to_pick(Depth depth) const { return depth / ONE_PLY == 1 + level; }
    Move best_move(size_t multiPV) { return best ? best : pick_best(multiPV); }
    Move pick_best(size_t multiPV);

    int level;
    Move best = MOVE_NONE;
  };

  size_t PVIdx;
  TimeManager TimeMgr;
  double BestMoveChanges;
  Value DrawValue[COLOR_NB];
  HistoryStats History;
  CounterMovesHistoryStats CounterMovesHistory;
  GainsStats Gains;
  MovesStats Countermoves, Followupmoves;

  template <NodeType NT, bool SpNode>
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode);

  template <NodeType NT, bool InCheck>
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth);

  void id_loop(Position& pos);
  Value value_to_tt(Value v, int ply);
  Value value_from_tt(Value v, int ply);
  void update_pv(Move* pv, Move move, Move* childPv);
  void update_stats(const Position& pos, Stack* ss, Move move, Depth depth, Move* quiets, int quietsCnt);

} // namespace


/// Search::init() is called during startup to initialize various lookup tables

void Search::init() {

  const double K[][2] = {{ 0.83, 2.25 }, { 0.50, 3.00 }};

  for (int pv = 0; pv <= 1; ++pv)
      for (int imp = 0; imp <= 1; ++imp)
          for (int d = 1; d < 64; ++d)
              for (int mc = 1; mc < 64; ++mc)
              {
                  double r = K[pv][0] + log(d) * log(mc) / K[pv][1];

                  if (r >= 1.5)
                      Reductions[pv][imp][d][mc] = int(r) * ONE_PLY;

                  // Increase reduction when eval is not improving
                  if (!pv && !imp && Reductions[pv][imp][d][mc] >= 2 * ONE_PLY)
                      Reductions[pv][imp][d][mc] += ONE_PLY;
              }

  for (int d = 0; d < 16; ++d)
  {
      FutilityMoveCounts[0][d] = int(2.4 + 0.773 * pow(d + 0.00, 1.8));
      FutilityMoveCounts[1][d] = int(2.9 + 1.045 * pow(d + 0.49, 1.8));
  }
}


/// Search::perft() is our utility to verify move generation. All the leaf nodes
/// up to the given depth are generated and counted and the sum returned.
template<bool Root>
uint64_t Search::perft(Position& pos, Depth depth) {

  StateInfo st;
  uint64_t cnt, nodes = 0;
  CheckInfo ci(pos);
  const bool leaf = (depth == 2 * ONE_PLY);

  for (const auto& m : MoveList<LEGAL>(pos))
  {
      if (Root && depth <= ONE_PLY)
          cnt = 1, nodes++;
      else
      {
          pos.do_move(m, st, pos.gives_check(m, ci));
          cnt = leaf ? MoveList<LEGAL>(pos).size() : perft<false>(pos, depth - ONE_PLY);
          nodes += cnt;
          pos.undo_move(m);
      }
      if (Root)
          sync_cout << UCI::move(m, pos.is_chess960()) << ": " << cnt << sync_endl;
  }
  return nodes;
}

template uint64_t Search::perft<true>(Position& pos, Depth depth);


/// Search::think() is the external interface to Stockfish's search, and is
/// called by the main thread when the program receives the UCI 'go' command. It
/// searches from RootPos and at the end prints the "bestmove" to output.

void Search::think() {

  TimeMgr.init(Limits, RootPos.side_to_move(), RootPos.game_ply());

  int contempt = Options["Contempt"] * PawnValueEg / 100; // From centipawns
  DrawValue[ RootPos.side_to_move()] = VALUE_DRAW - Value(contempt);
  DrawValue[~RootPos.side_to_move()] = VALUE_DRAW + Value(contempt);

  TB::Hits = 0;
  TB::RootInTB = false;
  TB::UseRule50 = Options["Syzygy50MoveRule"];
  TB::ProbeDepth = Options["SyzygyProbeDepth"] * ONE_PLY;
  TB::Cardinality = Options["SyzygyProbeLimit"];

  // Skip TB probing when no TB found: !TBLargest -> !TB::Cardinality
  if (TB::Cardinality > TB::MaxCardinality)
  {
      TB::Cardinality = TB::MaxCardinality;
      TB::ProbeDepth = DEPTH_ZERO;
  }

  if (RootMoves.empty())
  {
      RootMoves.push_back(RootMove(MOVE_NONE));
      sync_cout << "info depth 0 score "
                << UCI::value(RootPos.checkers() ? -VALUE_MATE : VALUE_DRAW)
                << sync_endl;
  }
  else
  {
      if (TB::Cardinality >=  RootPos.count<ALL_PIECES>(WHITE)
                            + RootPos.count<ALL_PIECES>(BLACK))
      {
          // If the current root position is in the tablebases then RootMoves
          // contains only moves that preserve the draw or win.
          TB::RootInTB = Tablebases::root_probe(RootPos, RootMoves, TB::Score);

          if (TB::RootInTB)
              TB::Cardinality = 0; // Do not probe tablebases during the search

          else // If DTZ tables are missing, use WDL tables as a fallback
          {
              // Filter out moves that do not preserve a draw or win
              TB::RootInTB = Tablebases::root_probe_wdl(RootPos, RootMoves, TB::Score);

              // Only probe during search if winning
              if (TB::Score <= VALUE_DRAW)
                  TB::Cardinality = 0;
          }

          if (TB::RootInTB)
          {
              TB::Hits = RootMoves.size();

              if (!TB::UseRule50)
                  TB::Score =  TB::Score > VALUE_DRAW ?  VALUE_MATE - MAX_PLY - 1
                             : TB::Score < VALUE_DRAW ? -VALUE_MATE + MAX_PLY + 1
                                                      :  VALUE_DRAW;
          }
      }

      for (Thread* th : Threads)
          th->maxPly = 0;

      Threads.timer->run = true;
      Threads.timer->notify_one(); // Wake up the recurring timer

      id_loop(RootPos); // Let's start searching !

      Threads.timer->run = false;
  }

  // When we reach the maximum depth, we can arrive here without a raise of
  // Signals.stop. However, if we are pondering or in an infinite search,
  // the UCI protocol states that we shouldn't print the best move before the
  // GUI sends a "stop" or "ponderhit" command. We therefore simply wait here
  // until the GUI sends one of those commands (which also raises Signals.stop).
  if (!Signals.stop && (Limits.ponder || Limits.infinite))
  {
      Signals.stopOnPonderhit = true;
      RootPos.this_thread()->wait_for(Signals.stop);
  }

  sync_cout << "bestmove " << UCI::move(RootMoves[0].pv[0], RootPos.is_chess960());

  if (RootMoves[0].pv.size() > 1 || RootMoves[0].extract_ponder_from_tt(RootPos))
      std::cout << " ponder " << UCI::move(RootMoves[0].pv[1], RootPos.is_chess960());

  std::cout << sync_endl;
}


namespace {

  // id_loop() is the main iterative deepening loop. It calls search() repeatedly
  // with increasing depth until the allocated thinking time has been consumed,
  // user stops the search, or the maximum search depth is reached.

  void id_loop(Position& pos) {

    Stack stack[MAX_PLY+4], *ss = stack+2; // To allow referencing (ss-2) and (ss+2)
    Depth depth;
    Value bestValue, alpha, beta, delta;

    std::memset(ss-2, 0, 5 * sizeof(Stack));

    depth = DEPTH_ZERO;
    BestMoveChanges = 0;
    bestValue = delta = alpha = -VALUE_INFINITE;
    beta = VALUE_INFINITE;

    TT.new_search();
    History.clear();
    CounterMovesHistory.clear();
    Gains.clear();
    Countermoves.clear();
    Followupmoves.clear();

    size_t multiPV = Options["MultiPV"];
    Skill skill(Options["Skill Level"]);

    // When playing with strength handicap enable MultiPV search that we will
    // use behind the scenes to retrieve a set of possible moves.
    if (skill.enabled())
        multiPV = std::max(multiPV, (size_t)4);

    multiPV = std::min(multiPV, RootMoves.size());

    // Iterative deepening loop until requested to stop or target depth reached
    while (++depth < DEPTH_MAX && !Signals.stop && (!Limits.depth || depth <= Limits.depth))
    {
        // Age out PV variability metric
        BestMoveChanges *= 0.5;

        // Save the last iteration's scores before first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (RootMove& rm : RootMoves)
            rm.previousScore = rm.score;

        // MultiPV loop. We perform a full root search for each PV line
        for (PVIdx = 0; PVIdx < multiPV && !Signals.stop; ++PVIdx)
        {
            // Reset aspiration window starting size
            if (depth >= 5 * ONE_PLY)
            {
                delta = Value(16);
                alpha = std::max(RootMoves[PVIdx].previousScore - delta,-VALUE_INFINITE);
                beta  = std::min(RootMoves[PVIdx].previousScore + delta, VALUE_INFINITE);
            }

            // Start with a small aspiration window and, in the case of a fail
            // high/low, re-search with a bigger window until we're not failing
            // high/low anymore.
            while (true)
            {
                bestValue = search<Root, false>(pos, ss, alpha, beta, depth, false);

                // Bring the best move to the front. It is critical that sorting
                // is done with a stable algorithm because all the values but the
                // first and eventually the new best one are set to -VALUE_INFINITE
                // and we want to keep the same order for all the moves except the
                // new PV that goes to the front. Note that in case of MultiPV
                // search the already searched PV lines are preserved.
                std::stable_sort(RootMoves.begin() + PVIdx, RootMoves.end());

                // Write PV back to transposition table in case the relevant
                // entries have been overwritten during the search.
                for (size_t i = 0; i <= PVIdx; ++i)
                    RootMoves[i].insert_pv_in_tt(pos);

                // If search has been stopped break immediately. Sorting and
                // writing PV back to TT is safe because RootMoves is still
                // valid, although it refers to previous iteration.
                if (Signals.stop)
                    break;

                // When failing high/low give some update (without cluttering
                // the UI) before a re-search.
                if (   multiPV == 1
                    && (bestValue <= alpha || bestValue >= beta)
                    && now() - SearchTime > 3000)
                    sync_cout << UCI::pv(pos, depth, alpha, beta) << sync_endl;

                // In case of failing low/high increase aspiration window and
                // re-search, otherwise exit the loop.
                if (bestValue <= alpha)
                {
                    beta = (alpha + beta) / 2;
                    alpha = std::max(bestValue - delta, -VALUE_INFINITE);

                    Signals.failedLowAtRoot = true;
                    Signals.stopOnPonderhit = false;
                }
                else if (bestValue >= beta)
                {
                    alpha = (alpha + beta) / 2;
                    beta = std::min(bestValue + delta, VALUE_INFINITE);
                }
                else
                    break;

                delta += delta / 2;

                assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
            }

            // Sort the PV lines searched so far and update the GUI
            std::stable_sort(RootMoves.begin(), RootMoves.begin() + PVIdx + 1);

            if (Signals.stop)
                sync_cout << "info nodes " << RootPos.nodes_searched()
                          << " time " << now() - SearchTime << sync_endl;

            else if (PVIdx + 1 == multiPV || now() - SearchTime > 3000)
                sync_cout << UCI::pv(pos, depth, alpha, beta) << sync_endl;
        }

        // If skill level is enabled and time is up, pick a sub-optimal best move
        if (skill.enabled() && skill.time_to_pick(depth))
            skill.pick_best(multiPV);

        // Have we found a "mate in x"?
        if (   Limits.mate
            && bestValue >= VALUE_MATE_IN_MAX_PLY
            && VALUE_MATE - bestValue <= 2 * Limits.mate)
            Signals.stop = true;

        // Do we have time for the next iteration? Can we stop searching now?
        if (Limits.use_time_management() && !Signals.stop && !Signals.stopOnPonderhit)
        {
            // Take some extra time if the best move has changed
            if (depth > 4 * ONE_PLY && multiPV == 1)
                TimeMgr.pv_instability(BestMoveChanges);

            // Stop the search if only one legal move is available or all
            // of the available time has been used.
            if (   RootMoves.size() == 1
                || now() - SearchTime > TimeMgr.available_time())
            {
                // If we are allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (Limits.ponder)
                    Signals.stopOnPonderhit = true;
                else
                    Signals.stop = true;
            }
        }
    }

    // If skill level is enabled, swap best PV line with the sub-optimal one
    if (skill.enabled())
        std::swap(RootMoves[0], *std::find(RootMoves.begin(),
                  RootMoves.end(), skill.best_move(multiPV)));
  }


  // search<>() is the main search function for both PV and non-PV nodes and for
  // normal and SplitPoint nodes. When called just after a split point the search
  // is simpler because we have already probed the hash table, done a null move
  // search, and searched the first move before splitting, so we don't have to
  // repeat all this work again. We also don't need to store anything to the hash
  // table here: This is taken care of after we return from the split point.

  template <NodeType NT, bool SpNode>
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    const bool RootNode = NT == Root;
    const bool PvNode   = NT == PV || NT == Root;

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(depth > DEPTH_ZERO);

    Move pv[MAX_PLY+1], quietsSearched[64];
    StateInfo st;
    TTEntry* tte;
    SplitPoint* splitPoint;
    Key posKey;
    Move ttMove, move, excludedMove, bestMove;
    Depth extension, newDepth, predictedDepth;
    Value bestValue, value, ttValue, eval, nullValue, futilityValue;
    bool ttHit, inCheck, givesCheck, singularExtensionNode, improving;
    bool captureOrPromotion, dangerous, doFullDepthSearch;
    int moveCount, quietCount;

    // Step 1. Initialize node
    Thread* thisThread = pos.this_thread();
    inCheck = pos.checkers();

    if (SpNode)
    {
        splitPoint = ss->splitPoint;
        bestMove   = splitPoint->bestMove;
        bestValue  = splitPoint->bestValue;
        tte = nullptr;
        ttHit = false;
        ttMove = excludedMove = MOVE_NONE;
        ttValue = VALUE_NONE;

        assert(splitPoint->bestValue > -VALUE_INFINITE && splitPoint->moveCount > 0);

        goto moves_loop;
    }

    moveCount = quietCount = 0;
    bestValue = -VALUE_INFINITE;
    ss->ply = (ss-1)->ply + 1;

    // Used to send selDepth info to GUI
    if (PvNode && thisThread->maxPly < ss->ply)
        thisThread->maxPly = ss->ply;

    if (!RootNode)
    {
        // Step 2. Check for aborted search and immediate draw
        if (Signals.stop || pos.is_draw() || ss->ply >= MAX_PLY)
            return ss->ply >= MAX_PLY && !inCheck ? evaluate(pos) : DrawValue[pos.side_to_move()];

        // Step 3. Mate distance pruning. Even if we mate at the next move our score
        // would be at best mate_in(ss->ply+1), but if alpha is already bigger because
        // a shorter mate was found upward in the tree then there is no need to search
        // because we will never beat the current alpha. Same logic but with reversed
        // signs applies also in the opposite condition of being mated instead of giving
        // mate. In this case return a fail-high score.
        alpha = std::max(mated_in(ss->ply), alpha);
        beta = std::min(mate_in(ss->ply+1), beta);
        if (alpha >= beta)
            return alpha;
    }

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    ss->currentMove = ss->ttMove = (ss+1)->excludedMove = bestMove = MOVE_NONE;
    (ss+1)->skipEarlyPruning = false; (ss+1)->reduction = DEPTH_ZERO;
    (ss+2)->killers[0] = (ss+2)->killers[1] = MOVE_NONE;

    // Step 4. Transposition table lookup
    // We don't want the score of a partial search to overwrite a previous full search
    // TT value, so we use a different position key in case of an excluded move.
    excludedMove = ss->excludedMove;
    posKey = excludedMove ? pos.exclusion_key() : pos.key();
    tte = TT.probe(posKey, ttHit);
    ss->ttMove = ttMove = RootNode ? RootMoves[PVIdx].pv[0] : ttHit ? tte->move() : MOVE_NONE;
    ttValue = ttHit ? value_from_tt(tte->value(), ss->ply) : VALUE_NONE;

    // At non-PV nodes we check for a fail high/low. We don't probe at PV nodes
    if (  !PvNode
        && ttHit
        && tte->depth() >= depth
        && ttValue != VALUE_NONE // Only in case of TT access race
        && (ttValue >= beta ? (tte->bound() & BOUND_LOWER)
                            : (tte->bound() & BOUND_UPPER)))
    {
        ss->currentMove = ttMove; // Can be MOVE_NONE

        // If ttMove is quiet, update killers, history, counter move and followup move on TT hit
        if (ttValue >= beta && ttMove && !pos.capture_or_promotion(ttMove) && !inCheck)
            update_stats(pos, ss, ttMove, depth, nullptr, 0);

        return ttValue;
    }

    // Step 4a. Tablebase probe
    if (!RootNode && TB::Cardinality)
    {
        int piecesCnt = pos.count<ALL_PIECES>(WHITE) + pos.count<ALL_PIECES>(BLACK);

        if (    piecesCnt <= TB::Cardinality
            && (piecesCnt <  TB::Cardinality || depth >= TB::ProbeDepth)
            &&  pos.rule50_count() == 0)
        {
            int found, v = Tablebases::probe_wdl(pos, &found);

            if (found)
            {
                TB::Hits++;

                int drawScore = TB::UseRule50 ? 1 : 0;

                value =  v < -drawScore ? -VALUE_MATE + MAX_PLY + ss->ply
                       : v >  drawScore ?  VALUE_MATE - MAX_PLY - ss->ply
                                        :  VALUE_DRAW + 2 * v * drawScore;

                tte->save(posKey, value_to_tt(value, ss->ply), BOUND_EXACT,
                          std::min(DEPTH_MAX - ONE_PLY, depth + 6 * ONE_PLY),
                          MOVE_NONE, VALUE_NONE, TT.generation());

                return value;
            }
        }
    }

    // Step 5. Evaluate the position statically and update parent's gain statistics
    if (inCheck)
    {
        ss->staticEval = eval = VALUE_NONE;
        goto moves_loop;
    }

    else if (ttHit)
    {
        // Never assume anything on values stored in TT
        if ((ss->staticEval = eval = tte->eval()) == VALUE_NONE)
            eval = ss->staticEval = evaluate(pos);

        // Can ttValue be used as a better position evaluation?
        if (ttValue != VALUE_NONE)
            if (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER))
                eval = ttValue;
    }
    else
    {
        eval = ss->staticEval =
        (ss-1)->currentMove != MOVE_NULL ? evaluate(pos) : -(ss-1)->staticEval + 2 * Eval::Tempo;

        tte->save(posKey, VALUE_NONE, BOUND_NONE, DEPTH_NONE, MOVE_NONE, ss->staticEval, TT.generation());
    }

    if (ss->skipEarlyPruning)
        goto moves_loop;

    if (   !pos.captured_piece_type()
        &&  ss->staticEval != VALUE_NONE
        && (ss-1)->staticEval != VALUE_NONE
        && (move = (ss-1)->currentMove) != MOVE_NULL
        &&  move != MOVE_NONE
        &&  type_of(move) == NORMAL)
    {
        Square to = to_sq(move);
        Gains.update(pos.piece_on(to), to, -(ss-1)->staticEval - ss->staticEval);
    }

    // Step 6. Razoring (skipped when in check)
    if (   !PvNode
        &&  depth < 4 * ONE_PLY
        &&  eval + razor_margin(depth) <= alpha
        &&  ttMove == MOVE_NONE
        && !pos.pawn_on_7th(pos.side_to_move()))
    {
        if (   depth <= ONE_PLY
            && eval + razor_margin(3 * ONE_PLY) <= alpha)
            return qsearch<NonPV, false>(pos, ss, alpha, beta, DEPTH_ZERO);

        Value ralpha = alpha - razor_margin(depth);
        Value v = qsearch<NonPV, false>(pos, ss, ralpha, ralpha+1, DEPTH_ZERO);
        if (v <= ralpha)
            return v;
    }

    // Step 7. Futility pruning: child node (skipped when in check)
    if (   !RootNode
        &&  depth < 7 * ONE_PLY
        &&  eval - futility_margin(depth) >= beta
        &&  eval < VALUE_KNOWN_WIN  // Do not return unproven wins
        &&  pos.non_pawn_material(pos.side_to_move()))
        return eval - futility_margin(depth);

    // Step 8. Null move search with verification search (is omitted in PV nodes)
    if (   !PvNode
        &&  depth >= 2 * ONE_PLY
        &&  eval >= beta
        &&  pos.non_pawn_material(pos.side_to_move()))
    {
        ss->currentMove = MOVE_NULL;

        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth and value
        Depth R = ((823 + 67 * depth) / 256 + std::min((eval - beta) / PawnValueMg, 3)) * ONE_PLY;

        pos.do_null_move(st);
        (ss+1)->skipEarlyPruning = true;
        nullValue = depth-R < ONE_PLY ? -qsearch<NonPV, false>(pos, ss+1, -beta, -beta+1, DEPTH_ZERO)
                                      : - search<NonPV, false>(pos, ss+1, -beta, -beta+1, depth-R, !cutNode);
        (ss+1)->skipEarlyPruning = false;
        pos.undo_null_move();

        if (nullValue >= beta)
        {
            // Do not return unproven mate scores
            if (nullValue >= VALUE_MATE_IN_MAX_PLY)
                nullValue = beta;

            if (depth < 12 * ONE_PLY && abs(beta) < VALUE_KNOWN_WIN)
                return nullValue;

            // Do verification search at high depths
            ss->skipEarlyPruning = true;
            Value v = depth-R < ONE_PLY ? qsearch<NonPV, false>(pos, ss, beta-1, beta, DEPTH_ZERO)
                                        :  search<NonPV, false>(pos, ss, beta-1, beta, depth-R, false);
            ss->skipEarlyPruning = false;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 9. ProbCut (skipped when in check)
    // If we have a very good capture (i.e. SEE > seeValues[captured_piece_type])
    // and a reduced search returns a value much above beta, we can (almost) safely
    // prune the previous move.
    if (   !PvNode
        &&  depth >= 5 * ONE_PLY
        &&  abs(beta) < VALUE_MATE_IN_MAX_PLY)
    {
        Value rbeta = std::min(beta + 200, VALUE_INFINITE);
        Depth rdepth = depth - 4 * ONE_PLY;

        assert(rdepth >= ONE_PLY);
        assert((ss-1)->currentMove != MOVE_NONE);
        assert((ss-1)->currentMove != MOVE_NULL);

        MovePicker mp(pos, ttMove, History, CounterMovesHistory, pos.captured_piece_type());
        CheckInfo ci(pos);

        while ((move = mp.next_move<false>()) != MOVE_NONE)
            if (pos.legal(move, ci.pinned))
            {
                ss->currentMove = move;
                pos.do_move(move, st, pos.gives_check(move, ci));
                value = -search<NonPV, false>(pos, ss+1, -rbeta, -rbeta+1, rdepth, !cutNode);
                pos.undo_move(move);
                if (value >= rbeta)
                    return value;
            }
    }

    // Step 10. Internal iterative deepening (skipped when in check)
    if (    depth >= (PvNode ? 5 * ONE_PLY : 8 * ONE_PLY)
        && !ttMove
        && (PvNode || ss->staticEval + 256 >= beta))
    {
        Depth d = 2 * (depth - 2 * ONE_PLY) - (PvNode ? DEPTH_ZERO : depth / 2);
        ss->skipEarlyPruning = true;
        search<PvNode ? PV : NonPV, false>(pos, ss, alpha, beta, d / 2, true);
        ss->skipEarlyPruning = false;

        tte = TT.probe(posKey, ttHit);
        ttMove = ttHit ? tte->move() : MOVE_NONE;
    }

moves_loop: // When in check and at SpNode search starts from here

    Square prevMoveSq = to_sq((ss-1)->currentMove);
    Move countermoves[] = { Countermoves[pos.piece_on(prevMoveSq)][prevMoveSq].first,
                            Countermoves[pos.piece_on(prevMoveSq)][prevMoveSq].second };

    Square prevOwnMoveSq = to_sq((ss-2)->currentMove);
    Move followupmoves[] = { Followupmoves[pos.piece_on(prevOwnMoveSq)][prevOwnMoveSq].first,
                             Followupmoves[pos.piece_on(prevOwnMoveSq)][prevOwnMoveSq].second };

    MovePicker mp(pos, ttMove, depth, History, CounterMovesHistory, countermoves, followupmoves, ss);
    CheckInfo ci(pos);
    value = bestValue; // Workaround a bogus 'uninitialized' warning under gcc
    improving =   ss->staticEval >= (ss-2)->staticEval
               || ss->staticEval == VALUE_NONE
               ||(ss-2)->staticEval == VALUE_NONE;

    singularExtensionNode =   !RootNode
                           && !SpNode
                           &&  depth >= 8 * ONE_PLY
                           &&  ttMove != MOVE_NONE
                       /*  &&  ttValue != VALUE_NONE Already implicit in the next condition */
                           &&  abs(ttValue) < VALUE_KNOWN_WIN
                           && !excludedMove // Recursive singular search is not allowed
                           && (tte->bound() & BOUND_LOWER)
                           &&  tte->depth() >= depth - 3 * ONE_PLY;

    // Step 11. Loop through moves
    // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move<SpNode>()) != MOVE_NONE)
    {
      assert(is_ok(move));

      if (move == excludedMove)
          continue;

      // At root obey the "searchmoves" option and skip moves not listed in Root
      // Move List. As a consequence any illegal move is also skipped. In MultiPV
      // mode we also skip PV moves which have been already searched.
      if (RootNode && !std::count(RootMoves.begin() + PVIdx, RootMoves.end(), move))
          continue;

      if (SpNode)
      {
          // Shared counter cannot be decremented later if the move turns out to be illegal
          if (!pos.legal(move, ci.pinned))
              continue;

          moveCount = ++splitPoint->moveCount;
          splitPoint->mutex.unlock();
      }
      else
          ++moveCount;

      if (RootNode)
      {
          Signals.firstRootMove = (moveCount == 1);

          if (thisThread == Threads.main() && now() - SearchTime > 3000)
              sync_cout << "info depth " << depth / ONE_PLY
                        << " currmove " << UCI::move(move, pos.is_chess960())
                        << " currmovenumber " << moveCount + PVIdx << sync_endl;
      }

      if (PvNode)
          (ss+1)->pv = nullptr;

      extension = DEPTH_ZERO;
      captureOrPromotion = pos.capture_or_promotion(move);

      givesCheck =  type_of(move) == NORMAL && !ci.dcCandidates
                  ? ci.checkSq[type_of(pos.piece_on(from_sq(move)))] & to_sq(move)
                  : pos.gives_check(move, ci);

      dangerous =   givesCheck
                 || type_of(move) != NORMAL
                 || pos.advanced_pawn_push(move);

      // Step 12. Extend checks
      if (givesCheck && pos.see_sign(move) >= VALUE_ZERO)
          extension = ONE_PLY;

      // Singular extension search. If all moves but one fail low on a search of
      // (alpha-s, beta-s), and just one fails high on (alpha, beta), then that move
      // is singular and should be extended. To verify this we do a reduced search
      // on all the other moves but the ttMove and if the result is lower than
      // ttValue minus a margin then we extend the ttMove.
      if (    singularExtensionNode
          &&  move == ttMove
          && !extension
          &&  pos.legal(move, ci.pinned))
      {
          Value rBeta = ttValue - 2 * depth / ONE_PLY;
          ss->excludedMove = move;
          ss->skipEarlyPruning = true;
          value = search<NonPV, false>(pos, ss, rBeta - 1, rBeta, depth / 2, cutNode);
          ss->skipEarlyPruning = false;
          ss->excludedMove = MOVE_NONE;

          if (value < rBeta)
              extension = ONE_PLY;
      }

      // Update the current move (this must be done after singular extension search)
      newDepth = depth - ONE_PLY + extension;

      // Step 13. Pruning at shallow depth
      if (   !RootNode
          && !captureOrPromotion
          && !inCheck
          && !dangerous
          &&  bestValue > VALUE_MATED_IN_MAX_PLY)
      {
          // Move count based pruning
          if (   depth < 16 * ONE_PLY
              && moveCount >= FutilityMoveCounts[improving][depth])
          {
              if (SpNode)
                  splitPoint->mutex.lock();

              continue;
          }

          predictedDepth = newDepth - reduction<PvNode>(improving, depth, moveCount);

          // Futility pruning: parent node
          if (predictedDepth < 7 * ONE_PLY)
          {
              futilityValue =  ss->staticEval + futility_margin(predictedDepth)
                             + 128 + Gains[pos.moved_piece(move)][to_sq(move)];

              if (futilityValue <= alpha)
              {
                  bestValue = std::max(bestValue, futilityValue);

                  if (SpNode)
                  {
                      splitPoint->mutex.lock();
                      if (bestValue > splitPoint->bestValue)
                          splitPoint->bestValue = bestValue;
                  }
                  continue;
              }
          }

          // Prune moves with negative SEE at low depths
          if (predictedDepth < 4 * ONE_PLY && pos.see_sign(move) < VALUE_ZERO)
          {
              if (SpNode)
                  splitPoint->mutex.lock();

              continue;
          }
      }

      // Speculative prefetch as early as possible
      prefetch(TT.first_entry(pos.key_after(move)));

      // Check for legality just before making the move
      if (!RootNode && !SpNode && !pos.legal(move, ci.pinned))
      {
          moveCount--;
          continue;
      }

      ss->currentMove = move;
      if (!SpNode && !captureOrPromotion && quietCount < 64)
          quietsSearched[quietCount++] = move;

      // Step 14. Make the move
      pos.do_move(move, st, givesCheck);

      // Step 15. Reduced depth search (LMR). If the move fails high it will be
      // re-searched at full depth.
      if (    depth >= 3 * ONE_PLY
          &&  moveCount > 1
          && !captureOrPromotion
          &&  move != ss->killers[0]
          &&  move != ss->killers[1])
      {
          ss->reduction = reduction<PvNode>(improving, depth, moveCount);

          if (   (!PvNode && cutNode)
              ||  History[pos.piece_on(to_sq(move))][to_sq(move)] < VALUE_ZERO)
              ss->reduction += ONE_PLY;

          if (move == countermoves[0] || move == countermoves[1])
              ss->reduction = std::max(DEPTH_ZERO, ss->reduction - ONE_PLY);

          // Decrease reduction for moves that escape a capture
          if (   ss->reduction
              && type_of(move) == NORMAL
              && type_of(pos.piece_on(to_sq(move))) != PAWN
              && pos.see(make_move(to_sq(move), from_sq(move))) < VALUE_ZERO)
              ss->reduction = std::max(DEPTH_ZERO, ss->reduction - ONE_PLY);

          Depth d = std::max(newDepth - ss->reduction, ONE_PLY);
          if (SpNode)
              alpha = splitPoint->alpha;

          value = -search<NonPV, false>(pos, ss+1, -(alpha+1), -alpha, d, true);

          // Re-search at intermediate depth if reduction is very high
          if (value > alpha && ss->reduction >= 4 * ONE_PLY)
          {
              Depth d2 = std::max(newDepth - 2 * ONE_PLY, ONE_PLY);
              value = -search<NonPV, false>(pos, ss+1, -(alpha+1), -alpha, d2, true);
          }

          doFullDepthSearch = (value > alpha && ss->reduction != DEPTH_ZERO);
          ss->reduction = DEPTH_ZERO;
      }
      else
          doFullDepthSearch = !PvNode || moveCount > 1;

      // Step 16. Full depth search, when LMR is skipped or fails high
      if (doFullDepthSearch)
      {
          if (SpNode)
              alpha = splitPoint->alpha;

          value = newDepth <   ONE_PLY ?
                            givesCheck ? -qsearch<NonPV,  true>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
                                       : -qsearch<NonPV, false>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
                                       : - search<NonPV, false>(pos, ss+1, -(alpha+1), -alpha, newDepth, !cutNode);
      }

      // For PV nodes only, do a full PV search on the first move or after a fail
      // high (in the latter case search only if value < beta), otherwise let the
      // parent node fail low with value <= alpha and to try another move.
      if (PvNode && (moveCount == 1 || (value > alpha && (RootNode || value < beta))))
      {
          (ss+1)->pv = pv;
          (ss+1)->pv[0] = MOVE_NONE;

          value = newDepth <   ONE_PLY ?
                            givesCheck ? -qsearch<PV,  true>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                       : -qsearch<PV, false>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                       : - search<PV, false>(pos, ss+1, -beta, -alpha, newDepth, false);
      }

      // Step 17. Undo move
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Step 18. Check for new best move
      if (SpNode)
      {
          splitPoint->mutex.lock();
          bestValue = splitPoint->bestValue;
          alpha = splitPoint->alpha;
      }

      // Finished searching the move. If a stop or a cutoff occurred, the return
      // value of the search cannot be trusted, and we return immediately without
      // updating best move, PV and TT.
      if (Signals.stop || thisThread->cutoff_occurred())
          return VALUE_ZERO;

      if (RootNode)
      {
          RootMove& rm = *std::find(RootMoves.begin(), RootMoves.end(), move);

          // PV move or new best move ?
          if (moveCount == 1 || value > alpha)
          {
              rm.score = value;
              rm.pv.resize(1);

              assert((ss+1)->pv);

              for (Move* m = (ss+1)->pv; *m != MOVE_NONE; ++m)
                  rm.pv.push_back(*m);

              // We record how often the best move has been changed in each
              // iteration. This information is used for time management: When
              // the best move changes frequently, we allocate some more time.
              if (moveCount > 1)
                  ++BestMoveChanges;
          }
          else
              // All other moves but the PV are set to the lowest value: this is
              // not a problem when sorting because the sort is stable and the
              // move position in the list is preserved - just the PV is pushed up.
              rm.score = -VALUE_INFINITE;
      }

      if (value > bestValue)
      {
          bestValue = SpNode ? splitPoint->bestValue = value : value;

          if (value > alpha)
          {
              bestMove = SpNode ? splitPoint->bestMove = move : move;

              if (PvNode && !RootNode) // Update pv even in fail-high case
                  update_pv(SpNode ? splitPoint->ss->pv : ss->pv, move, (ss+1)->pv);

              if (PvNode && value < beta) // Update alpha! Always alpha < beta
                  alpha = SpNode ? splitPoint->alpha = value : value;
              else
              {
                  assert(value >= beta); // Fail high

                  if (SpNode)
                      splitPoint->cutoff = true;

                  break;
              }
          }
      }

      // Step 19. Check for splitting the search
      if (   !SpNode
          &&  Threads.size() >= 2
          &&  depth >= Threads.minimumSplitDepth
          &&  (   !thisThread->activeSplitPoint
               || !thisThread->activeSplitPoint->allSlavesSearching
               || (   Threads.size() > MAX_SLAVES_PER_SPLITPOINT
                   && thisThread->activeSplitPoint->slavesMask.count() == MAX_SLAVES_PER_SPLITPOINT))
          &&  thisThread->splitPointsSize < MAX_SPLITPOINTS_PER_THREAD)
      {
          assert(bestValue > -VALUE_INFINITE && bestValue < beta);

          thisThread->split(pos, ss, alpha, beta, &bestValue, &bestMove,
                            depth, moveCount, &mp, NT, cutNode);

          if (Signals.stop || thisThread->cutoff_occurred())
              return VALUE_ZERO;

          if (bestValue >= beta)
              break;
      }
    }

    if (SpNode)
        return bestValue;

    // Following condition would detect a stop or a cutoff set only after move
    // loop has been completed. But in this case bestValue is valid because we
    // have fully searched our subtree, and we can anyhow save the result in TT.
    /*
       if (Signals.stop || thisThread->cutoff_occurred())
        return VALUE_DRAW;
    */

    // Step 20. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it
    // must be mate or stalemate. If we are in a singular extension search then
    // return a fail low score.
    if (!moveCount)
        bestValue = excludedMove ? alpha
                   :     inCheck ? mated_in(ss->ply) : DrawValue[pos.side_to_move()];

    // Quiet best move: update killers, history, countermoves and followupmoves
    else if (bestValue >= beta && !pos.capture_or_promotion(bestMove) && !inCheck)
        update_stats(pos, ss, bestMove, depth, quietsSearched, quietCount - 1);

    tte->save(posKey, value_to_tt(bestValue, ss->ply),
              bestValue >= beta ? BOUND_LOWER :
              PvNode && bestMove ? BOUND_EXACT : BOUND_UPPER,
              depth, bestMove, ss->staticEval, TT.generation());

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }


  // qsearch() is the quiescence search function, which is called by the main
  // search function when the remaining depth is zero (or, to be more precise,
  // less than ONE_PLY).

  template <NodeType NT, bool InCheck>
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) {

    const bool PvNode = NT == PV;

    assert(NT == PV || NT == NonPV);
    assert(InCheck == !!pos.checkers());
    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(depth <= DEPTH_ZERO);

    Move pv[MAX_PLY+1];
    StateInfo st;
    TTEntry* tte;
    Key posKey;
    Move ttMove, move, bestMove;
    Value bestValue, value, ttValue, futilityValue, futilityBase, oldAlpha;
    bool ttHit, givesCheck, evasionPrunable;
    Depth ttDepth;

    if (PvNode)
    {
        oldAlpha = alpha; // To flag BOUND_EXACT when eval above alpha and no available moves
        (ss+1)->pv = pv;
        ss->pv[0] = MOVE_NONE;
    }

    ss->currentMove = bestMove = MOVE_NONE;
    ss->ply = (ss-1)->ply + 1;

    // Check for an instant draw or if the maximum ply has been reached
    if (pos.is_draw() || ss->ply >= MAX_PLY)
        return ss->ply >= MAX_PLY && !InCheck ? evaluate(pos) : DrawValue[pos.side_to_move()];

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Decide whether or not to include checks: this fixes also the type of
    // TT entry depth that we are going to use. Note that in qsearch we use
    // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
    ttDepth = InCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS
                                                  : DEPTH_QS_NO_CHECKS;

    // Transposition table lookup
    posKey = pos.key();
    tte = TT.probe(posKey, ttHit);
    ttMove = ttHit ? tte->move() : MOVE_NONE;
    ttValue = ttHit ? value_from_tt(tte->value(), ss->ply) : VALUE_NONE;

    if (  !PvNode
        && ttHit
        && tte->depth() >= ttDepth
        && ttValue != VALUE_NONE // Only in case of TT access race
        && (ttValue >= beta ? (tte->bound() &  BOUND_LOWER)
                            : (tte->bound() &  BOUND_UPPER)))
    {
        ss->currentMove = ttMove; // Can be MOVE_NONE
        return ttValue;
    }

    // Evaluate the position statically
    if (InCheck)
    {
        ss->staticEval = VALUE_NONE;
        bestValue = futilityBase = -VALUE_INFINITE;
    }
    else
    {
        if (ttHit)
        {
            // Never assume anything on values stored in TT
            if ((ss->staticEval = bestValue = tte->eval()) == VALUE_NONE)
                ss->staticEval = bestValue = evaluate(pos);

            // Can ttValue be used as a better position evaluation?
            if (ttValue != VALUE_NONE)
                if (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER))
                    bestValue = ttValue;
        }
        else
            ss->staticEval = bestValue =
            (ss-1)->currentMove != MOVE_NULL ? evaluate(pos) : -(ss-1)->staticEval + 2 * Eval::Tempo;

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (!ttHit)
                tte->save(pos.key(), value_to_tt(bestValue, ss->ply), BOUND_LOWER,
                          DEPTH_NONE, MOVE_NONE, ss->staticEval, TT.generation());

            return bestValue;
        }

        if (PvNode && bestValue > alpha)
            alpha = bestValue;

        futilityBase = bestValue + 128;
    }

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions and checks (only if depth >= DEPTH_QS_CHECKS) will
    // be generated.
    MovePicker mp(pos, ttMove, depth, History, CounterMovesHistory, to_sq((ss-1)->currentMove));
    CheckInfo ci(pos);

    // Loop through the moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move<false>()) != MOVE_NONE)
    {
      assert(is_ok(move));

      givesCheck =  type_of(move) == NORMAL && !ci.dcCandidates
                  ? ci.checkSq[type_of(pos.piece_on(from_sq(move)))] & to_sq(move)
                  : pos.gives_check(move, ci);

      // Futility pruning
      if (   !InCheck
          && !givesCheck
          &&  futilityBase > -VALUE_KNOWN_WIN
          && !pos.advanced_pawn_push(move))
      {
          assert(type_of(move) != ENPASSANT); // Due to !pos.advanced_pawn_push

          futilityValue = futilityBase + PieceValue[EG][pos.piece_on(to_sq(move))];

          if (futilityValue <= alpha)
          {
              bestValue = std::max(bestValue, futilityValue);
              continue;
          }

          if (futilityBase <= alpha && pos.see(move) <= VALUE_ZERO)
          {
              bestValue = std::max(bestValue, futilityBase);
              continue;
          }
      }

      // Detect non-capture evasions that are candidates to be pruned
      evasionPrunable =    InCheck
                       &&  bestValue > VALUE_MATED_IN_MAX_PLY
                       && !pos.capture(move)
                       && !pos.can_castle(pos.side_to_move());

      // Don't search moves with negative SEE values
      if (  (!InCheck || evasionPrunable)
          &&  type_of(move) != PROMOTION
          &&  pos.see_sign(move) < VALUE_ZERO)
          continue;

      // Speculative prefetch as early as possible
      prefetch(TT.first_entry(pos.key_after(move)));

      // Check for legality just before making the move
      if (!pos.legal(move, ci.pinned))
          continue;

      ss->currentMove = move;

      // Make and search the move
      pos.do_move(move, st, givesCheck);
      value = givesCheck ? -qsearch<NT,  true>(pos, ss+1, -beta, -alpha, depth - ONE_PLY)
                         : -qsearch<NT, false>(pos, ss+1, -beta, -alpha, depth - ONE_PLY);
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Check for new best move
      if (value > bestValue)
      {
          bestValue = value;

          if (value > alpha)
          {
              if (PvNode) // Update pv even in fail-high case
                  update_pv(ss->pv, move, (ss+1)->pv);

              if (PvNode && value < beta) // Update alpha here! Always alpha < beta
              {
                  alpha = value;
                  bestMove = move;
              }
              else // Fail high
              {
                  tte->save(posKey, value_to_tt(value, ss->ply), BOUND_LOWER,
                            ttDepth, move, ss->staticEval, TT.generation());

                  return value;
              }
          }
       }
    }

    // All legal moves have been searched. A special case: If we're in check
    // and no legal moves were found, it is checkmate.
    if (InCheck && bestValue == -VALUE_INFINITE)
        return mated_in(ss->ply); // Plies to mate from the root

    tte->save(posKey, value_to_tt(bestValue, ss->ply),
              PvNode && bestValue > oldAlpha ? BOUND_EXACT : BOUND_UPPER,
              ttDepth, bestMove, ss->staticEval, TT.generation());

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }


  // value_to_tt() adjusts a mate score from "plies to mate from the root" to
  // "plies to mate from the current position". Non-mate scores are unchanged.
  // The function is called before storing a value in the transposition table.

  Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);

    return  v >= VALUE_MATE_IN_MAX_PLY  ? v + ply
          : v <= VALUE_MATED_IN_MAX_PLY ? v - ply : v;
  }


  // value_from_tt() is the inverse of value_to_tt(): It adjusts a mate score
  // from the transposition table (which refers to the plies to mate/be mated
  // from current position) to "plies to mate/be mated from the root".

  Value value_from_tt(Value v, int ply) {

    return  v == VALUE_NONE             ? VALUE_NONE
          : v >= VALUE_MATE_IN_MAX_PLY  ? v - ply
          : v <= VALUE_MATED_IN_MAX_PLY ? v + ply : v;
  }


  // update_pv() adds current move and appends child pv[]

  void update_pv(Move* pv, Move move, Move* childPv) {

    for (*pv++ = move; childPv && *childPv != MOVE_NONE; )
        *pv++ = *childPv++;
    *pv = MOVE_NONE;
  }

  // update_stats() updates killers, history, countermoves and followupmoves stats after a fail-high
  // of a quiet move.

  void update_stats(const Position& pos, Stack* ss, Move move, Depth depth, Move* quiets, int quietsCnt) {

    if (ss->killers[0] != move)
    {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = move;
    }

    // Increase history value of the cut-off move and decrease all the other
    // played quiet moves.
    Value bonus = Value((depth / ONE_PLY) * (depth / ONE_PLY));
    History.update(pos.moved_piece(move), to_sq(move), bonus);

    for (int i = 0; i < quietsCnt; ++i)
    {
        Move m = quiets[i];
        History.update(pos.moved_piece(m), to_sq(m), -bonus);
    }

    if (is_ok((ss-1)->currentMove))
    {
        Square prevMoveSq = to_sq((ss-1)->currentMove);
        Piece prevMovePiece = pos.piece_on(prevMoveSq);
        Countermoves.update(prevMovePiece, prevMoveSq, move);

        HistoryStats& cmh = CounterMovesHistory[prevMovePiece][prevMoveSq];
        cmh.update(pos.moved_piece(move), to_sq(move), bonus);
        for (int i = 0; i < quietsCnt; ++i)
        {
            Move m = quiets[i];
            cmh.update(pos.moved_piece(m), to_sq(m), -bonus);
        }
    }

    if (is_ok((ss-2)->currentMove) && (ss-1)->currentMove == (ss-1)->ttMove)
    {
        Square prevOwnMoveSq = to_sq((ss-2)->currentMove);
        Followupmoves.update(pos.piece_on(prevOwnMoveSq), prevOwnMoveSq, move);
    }
  }


  // When playing with strength handicap, choose best move among a set of RootMoves
  // using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.

  Move Skill::pick_best(size_t multiPV) {

    // PRNG sequence should be non-deterministic, so we seed it with the time at init
    static PRNG rng(now());

    // RootMoves are already sorted by score in descending order
    int variance = std::min(RootMoves[0].score - RootMoves[multiPV - 1].score, PawnValueMg);
    int weakness = 120 - 2 * level;
    int maxScore = -VALUE_INFINITE;

    // Choose best move. For each move score we add two terms both dependent on
    // weakness. One deterministic and bigger for weaker levels, and one random,
    // then we choose the move with the resulting highest score.
    for (size_t i = 0; i < multiPV; ++i)
    {
        // This is our magic formula
        int push = (  weakness * int(RootMoves[0].score - RootMoves[i].score)
                    + variance * (rng.rand<unsigned>() % weakness)) / 128;

        if (RootMoves[i].score + push > maxScore)
        {
            maxScore = RootMoves[i].score + push;
            best = RootMoves[i].pv[0];
        }
    }
    return best;
  }

} // namespace


/// UCI::pv() formats PV information according to the UCI protocol. UCI requires
/// that all (if any) unsearched PV lines are sent using a previous search score.

string UCI::pv(const Position& pos, Depth depth, Value alpha, Value beta) {

  std::stringstream ss;
  TimePoint elapsed = now() - SearchTime + 1;
  size_t multiPV = std::min((size_t)Options["MultiPV"], RootMoves.size());
  int selDepth = 0;

  for (Thread* th : Threads)
      if (th->maxPly > selDepth)
          selDepth = th->maxPly;

  for (size_t i = 0; i < multiPV; ++i)
  {
      bool updated = (i <= PVIdx);

      if (depth == ONE_PLY && !updated)
          continue;

      Depth d = updated ? depth : depth - ONE_PLY;
      Value v = updated ? RootMoves[i].score : RootMoves[i].previousScore;

      bool tb = TB::RootInTB && abs(v) < VALUE_MATE - MAX_PLY;
      v = tb ? TB::Score : v;

      if (ss.rdbuf()->in_avail()) // Not at first line
          ss << "\n";

      ss << "info"
         << " depth "    << d / ONE_PLY
         << " seldepth " << selDepth
         << " multipv "  << i + 1
         << " score "    << UCI::value(v);

      if (!tb && i == PVIdx)
          ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

      ss << " nodes "    << pos.nodes_searched()
         << " nps "      << pos.nodes_searched() * 1000 / elapsed;

      if (elapsed > 1000) // Earlier makes little sense
          ss << " hashfull " << TT.hashfull();

      ss << " tbhits "   << TB::Hits
         << " time "     << elapsed
         << " pv";

      for (Move m : RootMoves[i].pv)
          ss << " " << UCI::move(m, pos.is_chess960());
  }

  return ss.str();
}


/// RootMove::insert_pv_in_tt() is called at the end of a search iteration, and
/// inserts the PV back into the TT. This makes sure the old PV moves are searched
/// first, even if the old TT entries have been overwritten.

void RootMove::insert_pv_in_tt(Position& pos) {

  StateInfo state[MAX_PLY], *st = state;
  bool ttHit;

  for (Move m : pv)
  {
      assert(MoveList<LEGAL>(pos).contains(m));

      TTEntry* tte = TT.probe(pos.key(), ttHit);

      if (!ttHit || tte->move() != m) // Don't overwrite correct entries
          tte->save(pos.key(), VALUE_NONE, BOUND_NONE, DEPTH_NONE, m, VALUE_NONE, TT.generation());

      pos.do_move(m, *st++, pos.gives_check(m, CheckInfo(pos)));
  }

  for (size_t i = pv.size(); i > 0; )
      pos.undo_move(pv[--i]);
}


/// RootMove::extract_ponder_from_tt() is called in case we have no ponder move before
/// exiting the search, for instance in case we stop the search during a fail high at
/// root. We try hard to have a ponder move to return to the GUI, otherwise in case of
/// 'ponder on' we have nothing to think on.

bool RootMove::extract_ponder_from_tt(Position& pos)
{
    StateInfo st;
    bool ttHit;

    assert(pv.size() == 1);

    pos.do_move(pv[0], st, pos.gives_check(pv[0], CheckInfo(pos)));
    TTEntry* tte = TT.probe(pos.key(), ttHit);
    pos.undo_move(pv[0]);

    if (ttHit)
    {
        Move m = tte->move(); // Local copy to be SMP safe
        if (MoveList<LEGAL>(pos).contains(m))
           return pv.push_back(m), true;
    }

    return false;
}


/// Thread::idle_loop() is where the thread is parked when it has no work to do

void Thread::idle_loop() {

  // Pointer 'this_sp' is not null only if we are called from split(), and not
  // at the thread creation. This means we are the split point's master.
  SplitPoint* this_sp = activeSplitPoint;

  assert(!this_sp || (this_sp->master == this && searching));

  while (!exit)
  {
      // If this thread has been assigned work, launch a search
      while (searching)
      {
          mutex.lock();

          assert(activeSplitPoint);
          SplitPoint* sp = activeSplitPoint;

          mutex.unlock();

          Stack stack[MAX_PLY+4], *ss = stack+2; // To allow referencing (ss-2) and (ss+2)
          Position pos(*sp->pos, this);

          std::memcpy(ss-2, sp->ss-2, 5 * sizeof(Stack));
          ss->splitPoint = sp;

          sp->mutex.lock();

          assert(activePosition == nullptr);

          activePosition = &pos;

          if (sp->nodeType == NonPV)
              search<NonPV, true>(pos, ss, sp->alpha, sp->beta, sp->depth, sp->cutNode);

          else if (sp->nodeType == PV)
              search<PV, true>(pos, ss, sp->alpha, sp->beta, sp->depth, sp->cutNode);

          else if (sp->nodeType == Root)
              search<Root, true>(pos, ss, sp->alpha, sp->beta, sp->depth, sp->cutNode);

          else
              assert(false);

          assert(searching);

          searching = false;
          activePosition = nullptr;
          sp->slavesMask.reset(idx);
          sp->allSlavesSearching = false;
          sp->nodes += pos.nodes_searched();

          // Wake up the master thread so to allow it to return from the idle
          // loop in case we are the last slave of the split point.
          if (this != sp->master && sp->slavesMask.none())
          {
              assert(!sp->master->searching);

              sp->master->notify_one();
          }

          // After releasing the lock we can't access any SplitPoint related data
          // in a safe way because it could have been released under our feet by
          // the sp master.
          sp->mutex.unlock();

          // Try to late join to another split point if none of its slaves has
          // already finished.
          SplitPoint* bestSp = NULL;
          int minLevel = INT_MAX;

          for (Thread* th : Threads)
          {
              const size_t size = th->splitPointsSize; // Local copy
              sp = size ? &th->splitPoints[size - 1] : nullptr;

              if (   sp
                  && sp->allSlavesSearching
                  && sp->slavesMask.count() < MAX_SLAVES_PER_SPLITPOINT
                  && can_join(sp))
              {
                  assert(this != th);
                  assert(!(this_sp && this_sp->slavesMask.none()));
                  assert(Threads.size() > 2);

                  // Prefer to join to SP with few parents to reduce the probability
                  // that a cut-off occurs above us, and hence we waste our work.
                  int level = 0;
                  for (SplitPoint* p = th->activeSplitPoint; p; p = p->parentSplitPoint)
                      level++;

                  if (level < minLevel)
                  {
                      bestSp = sp;
                      minLevel = level;
                  }
              }
          }

          if (bestSp)
          {
              sp = bestSp;

              // Recheck the conditions under lock protection
              sp->mutex.lock();

              if (   sp->allSlavesSearching
                  && sp->slavesMask.count() < MAX_SLAVES_PER_SPLITPOINT)
              {
                  mutex.lock();

                  if (can_join(sp))
                  {
                      sp->slavesMask.set(idx);
                      activeSplitPoint = sp;
                      searching = true;
                  }

                  mutex.unlock();
              }

              sp->mutex.unlock();
          }
      }

      // Avoid races with notify_one() fired from last slave of the split point
      std::unique_lock<Mutex> lk(mutex);

      // If we are master and all slaves have finished then exit idle_loop
      if (this_sp && this_sp->slavesMask.none())
      {
          assert(!searching);
          break;
      }

      // If we are not searching, wait for a condition to be signaled instead of
      // wasting CPU time polling for work.
      if (!searching && !exit)
          sleepCondition.wait(lk);
  }
}


/// check_time() is called by the timer thread when the timer triggers. It is
/// used to print debug info and, more importantly, to detect when we are out of
/// available time and thus stop the search.

void check_time() {

  static TimePoint lastInfoTime = now();
  TimePoint elapsed = now() - SearchTime;

  if (now() - lastInfoTime >= 1000)
  {
      lastInfoTime = now();
      dbg_print();
  }

  // An engine may not stop pondering until told so by the GUI
  if (Limits.ponder)
      return;

  if (Limits.use_time_management())
  {
      bool stillAtFirstMove =    Signals.firstRootMove
                             && !Signals.failedLowAtRoot
                             &&  elapsed > TimeMgr.available_time() * 75 / 100;

      if (   stillAtFirstMove
          || elapsed > TimeMgr.maximum_time() - 2 * TimerThread::Resolution)
          Signals.stop = true;
  }
  else if (Limits.movetime && elapsed >= Limits.movetime)
      Signals.stop = true;

  else if (Limits.nodes)
  {
      int64_t nodes = RootPos.nodes_searched();

      // Loop across all split points and sum accumulated SplitPoint nodes plus
      // all the currently active positions nodes.
      // FIXME: Racy...
      for (Thread* th : Threads)
          for (size_t i = 0; i < th->splitPointsSize; ++i)
          {
              SplitPoint& sp = th->splitPoints[i];

              sp.mutex.lock();

              nodes += sp.nodes;

              for (size_t idx = 0; idx < Threads.size(); ++idx)
                  if (sp.slavesMask.test(idx) && Threads[idx]->activePosition)
                      nodes += Threads[idx]->activePosition->nodes_searched();

              sp.mutex.unlock();
          }

      if (nodes >= Limits.nodes)
          Signals.stop = true;
  }
}
