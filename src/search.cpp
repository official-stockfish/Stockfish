/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

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

#include "nnue/evaluate_nnue.h"

#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

namespace Stockfish {

namespace Search {

  LimitsType Limits;
}

using std::string;
using Eval::evaluate;
using namespace Search;
using namespace std;

bool Search::prune_at_shallow_depth = true;

namespace {

  // Different node types, used as a template parameter
  enum NodeType { NonPV, PV, Root };

  // Futility margin
  Value futility_margin(Depth d, bool improving) {
    return Value(168 * (d - improving));
  }

  // Reductions lookup table, initialized at startup
  int Reductions[MAX_MOVES]; // [depth or moveNumber]

  Depth reduction(bool i, Depth d, int mn, Value delta, Value rootDelta) {
    int r = Reductions[d] * Reductions[mn];
    return (r + 1463 - int(delta) * 1024 / int(rootDelta)) / 1024 + (!i && r > 1010);
  }

  constexpr int futility_move_count(bool improving, Depth depth) {
    return improving ? (3 + depth * depth)
                     : (3 + depth * depth) / 2;
  }

  // History and stats update bonus, based on depth
  int stat_bonus(Depth d) {
    return std::min((9 * d + 270) * d - 311 , 2145);
  }

  // Add a small random component to draw evaluations to avoid 3-fold blindness
  Value value_draw(Thread* thisThread) {
    return VALUE_DRAW + Value(2 * (thisThread->nodes & 1) - 1);
  }

  // Skill structure is used to implement strength limit. If we have an uci_elo then
  // we convert it to a suitable fractional skill level using anchoring to CCRL Elo
  // (goldfish 1.13 = 2000) and a fit through Ordo derived Elo for match (TC 60+0.6)
  // results spanning a wide range of k values.
  struct Skill {
    Skill(int skill_level, int uci_elo) {
        if (uci_elo)
            level = std::clamp(std::pow((uci_elo - 1346.6) / 143.4, 1 / 0.806), 0.0, 20.0);
        else
            level = double(skill_level);
    }
    bool enabled() const { return level < 20.0; }
    bool time_to_pick(Depth depth) const { return depth == 1 + int(level); }
    Move pick_best(size_t multiPV);

    double level;
    Move best = MOVE_NONE;
  };

  template <NodeType nodeType>
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode);

  template <NodeType nodeType>
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth = 0);

  Value value_to_tt(Value v, int ply);
  Value value_from_tt(Value v, int ply, int r50c);
  void update_pv(Move* pv, Move move, Move* childPv);
  void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus);
  void update_quiet_stats(const Position& pos, Stack* ss, Move move, int bonus);
  void update_all_stats(const Position& pos, Stack* ss, Move bestMove, Value bestValue, Value beta, Square prevSq,
                        Move* quietsSearched, int quietCount, Move* capturesSearched, int captureCount, Depth depth);

  // perft() is our utility to verify move generation. All the leaf nodes up
  // to the given depth are generated and counted, and the sum is returned.
  template<bool Root>
  uint64_t perft(Position& pos, Depth depth) {

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    uint64_t cnt, nodes = 0;
    const bool leaf = (depth == 2);

    for (const auto& m : MoveList<LEGAL>(pos))
    {
        if (Root && depth <= 1)
            cnt = 1, nodes++;
        else
        {
            pos.do_move(m, st);
            cnt = leaf ? MoveList<LEGAL>(pos).size() : perft<false>(pos, depth - 1);
            nodes += cnt;
            pos.undo_move(m);
        }
        if (Root)
            sync_cout << UCI::move(m, pos.is_chess960()) << ": " << cnt << sync_endl;
    }
    return nodes;
  }

} // namespace


/// Search::init() is called at startup to initialize various lookup tables

void Search::init() {

  for (int i = 1; i < MAX_MOVES; ++i)
      Reductions[i] = int((20.81 + std::log(Threads.size()) / 2) * std::log(i));
}


/// Search::clear() resets search state to its initial value

void Search::clear() {

  Threads.main()->wait_for_search_finished();

  Time.availableNodes = 0;
  TT.clear();
  Threads.clear();
  Tablebases::init(Options["SyzygyPath"]); // Free mapped files
}


/// MainThread::search() is started when the program receives the UCI 'go'
/// command. It searches from the root position and outputs the "bestmove".

void MainThread::search() {

  if (Limits.perft)
  {
      nodes = perft<true>(rootPos, Limits.perft);
      sync_cout << "\nNodes searched: " << nodes << "\n" << sync_endl;
      return;
  }

  Color us = rootPos.side_to_move();
  Time.init(Limits, us, rootPos.game_ply());
  TT.new_search();

  Eval::NNUE::verify();

  if (rootMoves.empty())
  {
      rootMoves.emplace_back(MOVE_NONE);
      sync_cout << "info depth 0 score "
                << UCI::value(rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW)
                << sync_endl;
  }
  else
  {
      Threads.start_searching(); // start non-main threads
      Thread::search();          // main thread start searching
  }

  // When we reach the maximum depth, we can arrive here without a raise of
  // Threads.stop. However, if we are pondering or in an infinite search,
  // the UCI protocol states that we shouldn't print the best move before the
  // GUI sends a "stop" or "ponderhit" command. We therefore simply wait here
  // until the GUI sends one of those commands.

  while (!Threads.stop && (ponder || Limits.infinite))
  {} // Busy wait for a stop or a ponder reset

  // Stop the threads if not already stopped (also raise the stop if
  // "ponderhit" just reset Threads.ponder).
  Threads.stop = true;

  // Wait until all threads have finished
  Threads.wait_for_search_finished();

  // When playing in 'nodes as time' mode, subtract the searched nodes from
  // the available ones before exiting.
  if (Limits.npmsec)
      Time.availableNodes += Limits.inc[us] - Threads.nodes_searched();

  Thread* bestThread = this;
  Skill skill = Skill(Options["Skill Level"], Options["UCI_LimitStrength"] ? int(Options["UCI_Elo"]) : 0);

  if (   int(Options["MultiPV"]) == 1
      && !Limits.depth
      && !skill.enabled()
      && rootMoves[0].pv[0] != MOVE_NONE)
      bestThread = Threads.get_best_thread();

  bestPreviousScore = bestThread->rootMoves[0].score;
  bestPreviousAverageScore = bestThread->rootMoves[0].averageScore;

  for (Thread* th : Threads)
    th->previousDepth = bestThread->completedDepth;

  // Send again PV info if we have a new best thread
  if (bestThread != this)
      sync_cout << UCI::pv(bestThread->rootPos, bestThread->completedDepth, -VALUE_INFINITE, VALUE_INFINITE) << sync_endl;

  sync_cout << "bestmove " << UCI::move(bestThread->rootMoves[0].pv[0], rootPos.is_chess960());

  if (bestThread->rootMoves[0].pv.size() > 1 || bestThread->rootMoves[0].extract_ponder_from_tt(rootPos))
      std::cout << " ponder " << UCI::move(bestThread->rootMoves[0].pv[1], rootPos.is_chess960());

  std::cout << sync_endl;
}


/// Thread::search() is the main iterative deepening loop. It calls search()
/// repeatedly with increasing depth until the allocated thinking time has been
/// consumed, the user stops the search, or the maximum search depth is reached.

void Thread::search() {

  // To allow access to (ss-7) up to (ss+2), the stack must be oversized.
  // The former is needed to allow update_continuation_histories(ss-1, ...),
  // which accesses its argument at ss-6, also near the root.
  // The latter is needed for statScore and killer initialization.
  Stack stack[MAX_PLY+10], *ss = stack+7;
  Move  pv[MAX_PLY+1];
  Value alpha, beta, delta;
  Move  lastBestMove = MOVE_NONE;
  Depth lastBestMoveDepth = 0;
  MainThread* mainThread = (this == Threads.main() ? Threads.main() : nullptr);
  double timeReduction = 1, totBestMoveChanges = 0;
  Color us = rootPos.side_to_move();
  int iterIdx = 0;

  std::memset(ss-7, 0, 10 * sizeof(Stack));
  for (int i = 7; i > 0; i--)
      (ss-i)->continuationHistory = &this->continuationHistory[0][0][NO_PIECE][0]; // Use as a sentinel

  for (int i = 0; i <= MAX_PLY + 2; ++i)
      (ss+i)->ply = i;

  ss->pv = pv;

  bestValue = delta = alpha = -VALUE_INFINITE;
  beta = VALUE_INFINITE;

  if (!this->rootMoves.empty())
    Tablebases::rank_root_moves(this->rootPos, this->rootMoves);

  if (mainThread)
  {
      if (mainThread->bestPreviousScore == VALUE_INFINITE)
          for (int i = 0; i < 4; ++i)
              mainThread->iterValue[i] = VALUE_ZERO;
      else
          for (int i = 0; i < 4; ++i)
              mainThread->iterValue[i] = mainThread->bestPreviousScore;
  }

  size_t multiPV = size_t(Options["MultiPV"]);
  Skill skill(Options["Skill Level"], Options["UCI_LimitStrength"] ? int(Options["UCI_Elo"]) : 0);

  // When playing with strength handicap enable MultiPV search that we will
  // use behind the scenes to retrieve a set of possible moves.
  if (skill.enabled())
      multiPV = std::max(multiPV, (size_t)4);

  multiPV = std::min(multiPV, rootMoves.size());

  complexityAverage.set(202, 1);

  trend         = SCORE_ZERO;
  optimism[ us] = Value(39);
  optimism[~us] = -optimism[us];

  int searchAgainCounter = 0;

  // Iterative deepening loop until requested to stop or the target depth is reached
  while (   ++rootDepth < MAX_PLY
         && !Threads.stop
         && !(Limits.depth && mainThread && rootDepth > Limits.depth))
  {
      // Age out PV variability metric
      if (mainThread)
          totBestMoveChanges /= 2;

      // Save the last iteration's scores before first PV line is searched and
      // all the move scores except the (new) PV are set to -VALUE_INFINITE.
      for (RootMove& rm : rootMoves)
          rm.previousScore = rm.score;

      size_t pvFirst = 0;
      pvLast = 0;

      if (!Threads.increaseDepth)
         searchAgainCounter++;

      // MultiPV loop. We perform a full root search for each PV line
      for (pvIdx = 0; pvIdx < multiPV && !Threads.stop; ++pvIdx)
      {
          if (pvIdx == pvLast)
          {
              pvFirst = pvLast;
              for (pvLast++; pvLast < rootMoves.size(); pvLast++)
                  if (rootMoves[pvLast].tbRank != rootMoves[pvFirst].tbRank)
                      break;
          }

          // Reset UCI info selDepth for each depth and each PV line
          selDepth = 0;

          // Reset aspiration window starting size
          if (rootDepth >= 4)
          {
              Value prev = rootMoves[pvIdx].averageScore;
              delta = Value(16) + int(prev) * prev / 19178;
              alpha = std::max(prev - delta,-VALUE_INFINITE);
              beta  = std::min(prev + delta, VALUE_INFINITE);

              // Adjust trend and optimism based on root move's previousScore
              int tr = sigmoid(prev, 3, 8, 90, 125, 1);
              trend = (us == WHITE ?  make_score(tr, tr / 2)
                                   : -make_score(tr, tr / 2));

              int opt = sigmoid(prev, 8, 17, 144, 13966, 183);
              optimism[ us] = Value(opt);
              optimism[~us] = -optimism[us];
          }

          // Start with a small aspiration window and, in the case of a fail
          // high/low, re-search with a bigger window until we don't fail
          // high/low anymore.
          failedHighCnt = 0;
          while (true)
          {
              Depth adjustedDepth = std::max(1, rootDepth - failedHighCnt - searchAgainCounter);
              bestValue = Stockfish::search<Root>(rootPos, ss, alpha, beta, adjustedDepth, false);

              // Bring the best move to the front. It is critical that sorting
              // is done with a stable algorithm because all the values but the
              // first and eventually the new best one are set to -VALUE_INFINITE
              // and we want to keep the same order for all the moves except the
              // new PV that goes to the front. Note that in case of MultiPV
              // search the already searched PV lines are preserved.
              std::stable_sort(rootMoves.begin() + pvIdx, rootMoves.begin() + pvLast);

              // If search has been stopped, we break immediately. Sorting is
              // safe because RootMoves is still valid, although it refers to
              // the previous iteration.
              if (Threads.stop)
                  break;

              // When failing high/low give some update (without cluttering
              // the UI) before a re-search.
              if (   mainThread
                  && multiPV == 1
                  && (bestValue <= alpha || bestValue >= beta)
                  && Time.elapsed() > 3000)
                  sync_cout << UCI::pv(rootPos, rootDepth, alpha, beta) << sync_endl;

              // In case of failing low/high increase aspiration window and
              // re-search, otherwise exit the loop.
              if (bestValue <= alpha)
              {
                  beta = (alpha + beta) / 2;
                  alpha = std::max(bestValue - delta, -VALUE_INFINITE);

                  failedHighCnt = 0;
                  if (mainThread)
                      mainThread->stopOnPonderhit = false;
              }
              else if (bestValue >= beta)
              {
                  beta = std::min(bestValue + delta, VALUE_INFINITE);
                  ++failedHighCnt;
              }
              else
                  break;

              delta += delta / 4 + 2;

              assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
          }

          // Sort the PV lines searched so far and update the GUI
          std::stable_sort(rootMoves.begin() + pvFirst, rootMoves.begin() + pvIdx + 1);

          if (    mainThread
              && (Threads.stop || pvIdx + 1 == multiPV || Time.elapsed() > 3000))
              sync_cout << UCI::pv(rootPos, rootDepth, alpha, beta) << sync_endl;
      }

      if (!Threads.stop)
          completedDepth = rootDepth;

      if (rootMoves[0].pv[0] != lastBestMove) {
         lastBestMove = rootMoves[0].pv[0];
         lastBestMoveDepth = rootDepth;
      }

      // Have we found a "mate in x"?
      if (   Limits.mate
          && bestValue >= VALUE_MATE_IN_MAX_PLY
          && VALUE_MATE - bestValue <= 2 * Limits.mate)
          Threads.stop = true;

      if (!mainThread)
          continue;

      // If skill level is enabled and time is up, pick a sub-optimal best move
      if (skill.enabled() && skill.time_to_pick(rootDepth))
          skill.pick_best(multiPV);

      // Use part of the gained time from a previous stable move for the current move
      for (Thread* th : Threads)
      {
          totBestMoveChanges += th->bestMoveChanges;
          th->bestMoveChanges = 0;
      }

      // Do we have time for the next iteration? Can we stop searching now?
      if (    Limits.use_time_management()
          && !Threads.stop
          && !mainThread->stopOnPonderhit)
      {
          double fallingEval = (69 + 12 * (mainThread->bestPreviousAverageScore - bestValue)
                                    +  6 * (mainThread->iterValue[iterIdx] - bestValue)) / 781.4;
          fallingEval = std::clamp(fallingEval, 0.5, 1.5);

          // If the bestMove is stable over several iterations, reduce time accordingly
          timeReduction = lastBestMoveDepth + 10 < completedDepth ? 1.63 : 0.73;
          double reduction = (1.56 + mainThread->previousTimeReduction) / (2.20 * timeReduction);
          double bestMoveInstability = 1 + 1.7 * totBestMoveChanges / Threads.size();
          int complexity = mainThread->complexityAverage.value();
          double complexPosition = std::clamp(1.0 + (complexity - 326) / 1618.1, 0.5, 1.5);

          double totalTime = Time.optimum() * fallingEval * reduction * bestMoveInstability * complexPosition;

          // Cap used time in case of a single legal move for a better viewer experience in tournaments
          // yielding correct scores and sufficiently fast moves.
          if (rootMoves.size() == 1)
              totalTime = std::min(500.0, totalTime);

          // Stop the search if we have exceeded the totalTime
          if (Time.elapsed() > totalTime)
          {
              // If we are allowed to ponder do not stop the search now but
              // keep pondering until the GUI sends "ponderhit" or "stop".
              if (mainThread->ponder)
                  mainThread->stopOnPonderhit = true;
              else
                  Threads.stop = true;
          }
          else if (   Threads.increaseDepth
                   && !mainThread->ponder
                   && Time.elapsed() > totalTime * 0.43)
                   Threads.increaseDepth = false;
          else
                   Threads.increaseDepth = true;
      }

      mainThread->iterValue[iterIdx] = bestValue;
      iterIdx = (iterIdx + 1) & 3;
  }

  if (!mainThread)
      return;

  mainThread->previousTimeReduction = timeReduction;

  // If skill level is enabled, swap best PV line with the sub-optimal one
  if (skill.enabled())
      std::swap(rootMoves[0], *std::find(rootMoves.begin(), rootMoves.end(),
                skill.best ? skill.best : skill.pick_best(multiPV)));
}


namespace {

  // search<>() is the main search function for both PV and non-PV nodes

  template <NodeType nodeType>
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    constexpr bool PvNode = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;
    const Depth maxNextDepth = rootNode ? depth : depth + 1;

    // Check if we have an upcoming move which draws by repetition, or
    // if the opponent had an alternative move earlier to this position.
    if (   !rootNode
        && pos.rule50_count() >= 3
        && alpha < VALUE_DRAW
        && pos.has_game_cycle(ss->ply))
    {
        alpha = value_draw(pos.this_thread());
        if (alpha >= beta)
            return alpha;
    }

    // Dive into quiescence search when the depth reaches zero
    if (depth <= 0)
        return qsearch<PvNode ? PV : NonPV>(pos, ss, alpha, beta);

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(0 < depth && depth < MAX_PLY);
    assert(!(PvNode && cutNode));

    Move pv[MAX_PLY+1], capturesSearched[32], quietsSearched[64];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry* tte;
    Key posKey;
    Move ttMove, move, excludedMove, bestMove;
    Depth extension, newDepth;
    Value bestValue, value, ttValue, eval, maxValue, probCutBeta;
    bool givesCheck, improving, didLMR, priorCapture;
    bool capture, doFullDepthSearch, moveCountPruning, ttCapture;
    Piece movedPiece;
    int moveCount, captureCount, quietCount, improvement, complexity;

    // Step 1. Initialize node
    Thread* thisThread = pos.this_thread();
    thisThread->depth  = depth;
    ss->inCheck        = pos.checkers();
    priorCapture       = pos.captured_piece();
    Color us           = pos.side_to_move();
    moveCount          = captureCount = quietCount = ss->moveCount = 0;
    bestValue          = -VALUE_INFINITE;
    maxValue           = VALUE_INFINITE;

    // Check for the available remaining time
    if (thisThread == Threads.main())
        static_cast<MainThread*>(thisThread)->check_time();

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && thisThread->selDepth < ss->ply + 1)
        thisThread->selDepth = ss->ply + 1;

    if (!rootNode)
    {
        // Step 2. Check for aborted search and immediate draw
        if (   Threads.stop.load(std::memory_order_relaxed)
            || (thisThread->maxNodes && thisThread->nodes.load(std::memory_order_relaxed) >= thisThread->maxNodes)
            || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return (ss->ply >= MAX_PLY && !ss->inCheck) ? evaluate(pos)
                                                        : value_draw(pos.this_thread());

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
    else
        thisThread->rootDelta = beta - alpha;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    (ss+1)->ttPv         = false;
    (ss+1)->excludedMove = bestMove = MOVE_NONE;
    (ss+2)->killers[0]   = (ss+2)->killers[1] = MOVE_NONE;
    (ss+2)->cutoffCnt    = 0;
    ss->doubleExtensions = (ss-1)->doubleExtensions;
    Square prevSq        = to_sq((ss-1)->currentMove);

    // Initialize statScore to zero for the grandchildren of the current position.
    // So statScore is shared between all grandchildren and only the first grandchild
    // starts with statScore = 0. Later grandchildren start with the last calculated
    // statScore of the previous grandchild. This influences the reduction rules in
    // LMR which are based on the statScore of parent position.
    if (!rootNode)
        (ss+2)->statScore = 0;

    // Step 4. Transposition table lookup. We don't want the score of a partial
    // search to overwrite a previous full search TT value, so we use a different
    // position key in case of an excluded move.
    excludedMove = ss->excludedMove;
    posKey = excludedMove == MOVE_NONE ? pos.key() : pos.key() ^ make_key(excludedMove);
    tte = TT.probe(posKey, ss->ttHit);
    ttValue = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove =  rootNode ? thisThread->rootMoves[thisThread->pvIdx].pv[0]
            : ss->ttHit    ? tte->move() : MOVE_NONE;
    ttCapture = ttMove && pos.capture(ttMove);
    if (!excludedMove)
        ss->ttPv = PvNode || (ss->ttHit && tte->is_pv());

    // At non-PV nodes we check for an early TT cutoff
    if (  !PvNode
        && ss->ttHit
        && tte->depth() > depth - (thisThread->id() % 2 == 1)
        && ttValue != VALUE_NONE // Possible in case of TT access race
        && (ttValue >= beta ? (tte->bound() & BOUND_LOWER)
                            : (tte->bound() & BOUND_UPPER)))
    {
        // If ttMove is quiet, update move sorting heuristics on TT hit (~1 Elo)
        if (ttMove)
        {
            if (ttValue >= beta)
            {
                // Bonus for a quiet ttMove that fails high (~3 Elo)
                if (!ttCapture)
                    update_quiet_stats(pos, ss, ttMove, stat_bonus(depth));

                // Extra penalty for early quiet moves of the previous ply (~0 Elo)
                if ((ss-1)->moveCount <= 2 && !priorCapture)
                    update_continuation_histories(ss-1, pos.piece_on(prevSq), prevSq, -stat_bonus(depth + 1));
            }
            // Penalty for a quiet ttMove that fails low (~1 Elo)
            else if (!ttCapture)
            {
                int penalty = -stat_bonus(depth);
                thisThread->mainHistory[us][from_to(ttMove)] << penalty;
                update_continuation_histories(ss, pos.moved_piece(ttMove), to_sq(ttMove), penalty);
            }
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < 90)
            return ttValue;
    }

    // Step 5. Tablebases probe
    if (!rootNode && thisThread->Cardinality)
    {
        int piecesCount = pos.count<ALL_PIECES>();

        if (    piecesCount <= thisThread->Cardinality
            && (piecesCount <  thisThread->Cardinality || depth >= thisThread->ProbeDepth)
            &&  pos.rule50_count() == 0
            && !pos.can_castle(ANY_CASTLING))
        {
            Tablebases::ProbeState err;
            Tablebases::WDLScore wdl = Tablebases::probe_wdl(pos, &err);

            // Force check of time on the next occasion
            if (thisThread == Threads.main())
                static_cast<MainThread*>(thisThread)->callsCnt = 0;

            if (err != Tablebases::ProbeState::FAIL)
            {
                thisThread->tbHits.fetch_add(1, std::memory_order_relaxed);

                int drawScore = thisThread->UseRule50 ? 1 : 0;

                // use the range VALUE_MATE_IN_MAX_PLY to VALUE_TB_WIN_IN_MAX_PLY to score
                value =  wdl < -drawScore ? VALUE_MATED_IN_MAX_PLY + ss->ply + 1
                       : wdl >  drawScore ? VALUE_MATE_IN_MAX_PLY - ss->ply - 1
                                          : VALUE_DRAW + 2 * wdl * drawScore;

                Bound b =  wdl < -drawScore ? BOUND_UPPER
                         : wdl >  drawScore ? BOUND_LOWER : BOUND_EXACT;

                if (    b == BOUND_EXACT
                    || (b == BOUND_LOWER ? value >= beta : value <= alpha))
                {
                    tte->save(posKey, value_to_tt(value, ss->ply), ss->ttPv, b,
                              std::min(MAX_PLY - 1, depth + 6),
                              MOVE_NONE, VALUE_NONE);

                    return value;
                }

                if (PvNode)
                {
                    if (b == BOUND_LOWER)
                        bestValue = value, alpha = std::max(alpha, bestValue);
                    else
                        maxValue = value;
                }
            }
        }
    }

    CapturePieceToHistory& captureHistory = thisThread->captureHistory;

    // Step 6. Static evaluation of the position
    if (ss->inCheck)
    {
        // Skip early pruning when in check
        ss->staticEval = eval = VALUE_NONE;
        improving = false;
        improvement = 0;
        complexity = 0;
        goto moves_loop;
    }
    else if (ss->ttHit)
    {
        // Never assume anything about values stored in TT
        ss->staticEval = eval = tte->eval();
        if (eval == VALUE_NONE)
            ss->staticEval = eval = evaluate(pos);

        // Randomize draw evaluation
        if (eval == VALUE_DRAW)
            eval = value_draw(thisThread);

        // ttValue can be used as a better position evaluation (~4 Elo)
        if (    ttValue != VALUE_NONE
            && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    }
    else
    {
        ss->staticEval = eval = evaluate(pos);

        // Save static evaluation into transposition table
        if (!excludedMove)
            tte->save(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_NONE, MOVE_NONE, eval);
    }

    // Use static evaluation difference to improve quiet move ordering (~3 Elo)
    if (is_ok((ss-1)->currentMove) && !(ss-1)->inCheck && !priorCapture)
    {
        int bonus = std::clamp(-16 * int((ss-1)->staticEval + ss->staticEval), -2000, 2000);
        thisThread->mainHistory[~us][from_to((ss-1)->currentMove)] << bonus;
    }

    // Set up the improvement variable, which is the difference between the current
    // static evaluation and the previous static evaluation at our turn (if we were
    // in check at our previous move we look at the move prior to it). The improvement
    // margin and the improving flag are used in various pruning heuristics.
    improvement =   (ss-2)->staticEval != VALUE_NONE ? ss->staticEval - (ss-2)->staticEval
                  : (ss-4)->staticEval != VALUE_NONE ? ss->staticEval - (ss-4)->staticEval
                  :                                    175;

    improving = improvement > 0;
    complexity = abs(ss->staticEval - (us == WHITE ? eg_value(pos.psq_score()) : -eg_value(pos.psq_score())));

    thisThread->complexityAverage.update(complexity);

    // Step 7. Razoring.
    // If eval is really low check with qsearch if it can exceed alpha, if it can't,
    // return a fail low.
    if (   !PvNode
        && depth <= 7
        && eval < alpha - 348 - 258 * depth * depth)
    {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha)
            return value;
    }

    // Step 8. Futility pruning: child node (~25 Elo).
    // The depth condition is important for mate finding.
    if (   !ss->ttPv
        &&  depth < 8
        &&  eval - futility_margin(depth, improving) - (ss-1)->statScore / 256 >= beta
        &&  eval >= beta
        &&  eval < 26305) // larger than VALUE_KNOWN_WIN, but smaller than TB wins.
        return eval;

    // Step 9. Null move search with verification search (~22 Elo)
    if (   !PvNode
        && (ss-1)->currentMove != MOVE_NULL
        && (ss-1)->statScore < 14695
        &&  eval >= beta
        &&  eval >= ss->staticEval
        &&  ss->staticEval >= beta - 15 * depth - improvement / 15 + 198 + complexity / 28
        && !excludedMove
        &&  pos.non_pawn_material(us)
        && (ss->ply >= thisThread->nmpMinPly || us != thisThread->nmpColor))
    {
        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth, eval and complexity of position
        Depth R = std::min(int(eval - beta) / 147, 5) + depth / 3 + 4 - (complexity > 753);

        ss->currentMove = MOVE_NULL;
        ss->continuationHistory = &thisThread->continuationHistory[0][0][NO_PIECE][0];

        pos.do_null_move(st);

        Value nullValue = -search<NonPV>(pos, ss+1, -beta, -beta+1, depth-R, !cutNode);

        pos.undo_null_move();

        if (nullValue >= beta)
        {
            // Do not return unproven mate or TB scores
            if (nullValue >= VALUE_TB_WIN_IN_MAX_PLY)
                nullValue = beta;

            if (thisThread->nmpMinPly || (abs(beta) < VALUE_KNOWN_WIN && depth < 14))
                return nullValue;

            assert(!thisThread->nmpMinPly); // Recursive verification is not allowed

            // Do verification search at high depths, with null move pruning disabled
            // for us, until ply exceeds nmpMinPly.
            thisThread->nmpMinPly = ss->ply + 3 * (depth-R) / 4;
            thisThread->nmpColor = us;

            Value v = search<NonPV>(pos, ss, beta-1, beta, depth-R, false);

            thisThread->nmpMinPly = 0;

            if (v >= beta)
                return nullValue;
        }
    }

    probCutBeta = beta + 179 - 46 * improving;

    // Step 10. ProbCut (~4 Elo)
    // If we have a good enough capture and a reduced search returns a value
    // much above beta, we can (almost) safely prune the previous move.
    if (   !PvNode
        &&  depth > 4
        &&  abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
        // if value from transposition table is lower than probCutBeta, don't attempt probCut
        // there and in further interactions with transposition table cutoff depth is set to depth - 3
        // because probCut search has depth set to depth - 4 but we also do a move before it
        // so effective depth is equal to depth - 3
        && !(   ss->ttHit
             && tte->depth() >= depth - 3
             && ttValue != VALUE_NONE
             && ttValue < probCutBeta))
    {
        assert(probCutBeta < VALUE_INFINITE);

        MovePicker mp(pos, ttMove, probCutBeta - ss->staticEval, depth - 3, &captureHistory);
        bool ttPv = ss->ttPv;
        ss->ttPv = false;

        while ((move = mp.next_move()) != MOVE_NONE)
            if (move != excludedMove && pos.legal(move))
            {
                assert(pos.capture(move) || promotion_type(move) == QUEEN);

                ss->currentMove = move;
                ss->continuationHistory = &thisThread->continuationHistory[ss->inCheck]
                                                                          [true]
                                                                          [pos.moved_piece(move)]
                                                                          [to_sq(move)];

                pos.do_move(move, st);

                // Perform a preliminary qsearch to verify that the move holds
                value = -qsearch<NonPV>(pos, ss+1, -probCutBeta, -probCutBeta+1);

                // If the qsearch held, perform the regular search
                if (value >= probCutBeta)
                    value = -search<NonPV>(pos, ss+1, -probCutBeta, -probCutBeta+1, depth - 4, !cutNode);

                pos.undo_move(move);

                if (value >= probCutBeta)
                {
                    // if transposition table doesn't have equal or more deep info write probCut data into it
                    if ( !(ss->ttHit
                       && tte->depth() >= depth - 3
                       && ttValue != VALUE_NONE))
                        tte->save(posKey, value_to_tt(value, ss->ply), ttPv,
                            BOUND_LOWER,
                            depth - 3, move, ss->staticEval);
                    return value;
                }
            }
         ss->ttPv = ttPv;
    }

    // Step 11. If the position is not in TT, decrease depth by 2 or 1 depending on node type (~3 Elo)
    if (   PvNode
        && depth >= 3
        && !ttMove)
        depth -= 2;

    if (   cutNode
        && depth >= 8
        && !ttMove)
        depth--;

moves_loop: // When in check, search starts here

    // Step 12. A small Probcut idea, when we are in check (~0 Elo)
    probCutBeta = beta + 481;
    if (   ss->inCheck
        && !PvNode
        && depth >= 2
        && ttCapture
        && (tte->bound() & BOUND_LOWER)
        && tte->depth() >= depth - 3
        && ttValue >= probCutBeta
        && abs(ttValue) <= VALUE_KNOWN_WIN
        && abs(beta) <= VALUE_KNOWN_WIN
       )
        return probCutBeta;


    const PieceToHistory* contHist[] = { (ss-1)->continuationHistory, (ss-2)->continuationHistory,
                                          nullptr                   , (ss-4)->continuationHistory,
                                          nullptr                   , (ss-6)->continuationHistory };

    Move countermove = thisThread->counterMoves[pos.piece_on(prevSq)][prevSq];

    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory,
                                      &captureHistory,
                                      contHist,
                                      countermove,
                                      ss->killers);

    value = bestValue;
    moveCountPruning = false;

    // Indicate PvNodes that will probably fail low if the node was searched
    // at a depth equal or greater than the current depth, and the result of this search was a fail low.
    bool likelyFailLow =    PvNode
                         && ttMove
                         && (tte->bound() & BOUND_UPPER)
                         && tte->depth() >= depth;

    // Step 13. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move(moveCountPruning)) != MOVE_NONE)
    {
      assert(is_ok(move));

      if (move == excludedMove)
          continue;

      // At root obey the "searchmoves" option and skip moves not listed in Root
      // Move List. As a consequence any illegal move is also skipped. In MultiPV
      // mode we also skip PV moves which have been already searched and those
      // of lower "TB rank" if we are in a TB root position.
      if (rootNode && !std::count(thisThread->rootMoves.begin() + thisThread->pvIdx,
                                  thisThread->rootMoves.begin() + thisThread->pvLast, move))
          continue;

      // Check for legality
      if (!rootNode && !pos.legal(move))
          continue;

      ss->moveCount = ++moveCount;

      if (rootNode && thisThread == Threads.main() && Time.elapsed() > 3000
          && !Limits.silent
          )
          sync_cout << "info depth " << depth
                    << " currmove " << UCI::move(move, pos.is_chess960())
                    << " currmovenumber " << moveCount + thisThread->pvIdx << sync_endl;
      if (PvNode)
          (ss+1)->pv = nullptr;

      extension = 0;
      capture = pos.capture(move);
      movedPiece = pos.moved_piece(move);
      givesCheck = pos.gives_check(move);

      // Calculate new depth for this move
      newDepth = depth - 1;

      Value delta = beta - alpha;

      // Step 14. Pruning at shallow depth (~98 Elo). Depth conditions are important for mate finding.
      if (  !rootNode
          && (PvNode ? prune_at_shallow_depth : true)
          && pos.non_pawn_material(us)
          && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
      {
          // Skip quiet moves if movecount exceeds our FutilityMoveCount threshold (~7 Elo)
          moveCountPruning = moveCount >= futility_move_count(improving, depth);

          // Reduced depth of the next LMR search
          int lmrDepth = std::max(newDepth - reduction(improving, depth, moveCount, delta, thisThread->rootDelta), 0);

          if (   capture
              || givesCheck)
          {
              // Futility pruning for captures (~0 Elo)
              if (   !pos.empty(to_sq(move))
                  && !givesCheck
                  && !PvNode
                  && lmrDepth < 6
                  && !ss->inCheck
                  && ss->staticEval + 281 + 179 * lmrDepth + PieceValue[EG][pos.piece_on(to_sq(move))]
                   + captureHistory[movedPiece][to_sq(move)][type_of(pos.piece_on(to_sq(move)))] / 6 < alpha)
                  continue;

              // SEE based pruning (~9 Elo)
              if (!pos.see_ge(move, Value(-203) * depth))
                  continue;
          }
          else
          {
              int history =   (*contHist[0])[movedPiece][to_sq(move)]
                            + (*contHist[1])[movedPiece][to_sq(move)]
                            + (*contHist[3])[movedPiece][to_sq(move)];

              // Continuation history based pruning (~2 Elo)
              if (   lmrDepth < 5
                  && history < -3875 * (depth - 1))
                  continue;

              history += thisThread->mainHistory[us][from_to(move)];

              // Futility pruning: parent node (~9 Elo)
              if (   !ss->inCheck
                  && lmrDepth < 11
                  && ss->staticEval + 122 + 138 * lmrDepth + history / 60 <= alpha)
                  continue;

              // Prune moves with negative SEE (~3 Elo)
              if (!pos.see_ge(move, Value(-25 * lmrDepth * lmrDepth - 20 * lmrDepth)))
                  continue;
          }
      }

      // Step 15. Extensions (~66 Elo)
      // We take care to not overdo to avoid search getting stuck.
      if (ss->ply < thisThread->rootDepth * 2)
      {
          // Singular extension search (~58 Elo). If all moves but one fail low on a
          // search of (alpha-s, beta-s), and just one fails high on (alpha, beta),
          // then that move is singular and should be extended. To verify this we do
          // a reduced search on all the other moves but the ttMove and if the
          // result is lower than ttValue minus a margin, then we will extend the ttMove.
          if (   !rootNode
              &&  depth >= 4 - (thisThread->previousDepth > 27) + 2 * (PvNode && tte->is_pv())
              &&  move == ttMove
              && !excludedMove // Avoid recursive singular search
           /* &&  ttValue != VALUE_NONE Already implicit in the next condition */
              &&  abs(ttValue) < VALUE_KNOWN_WIN
              && (tte->bound() & BOUND_LOWER)
              &&  tte->depth() >= depth - 3)
          {
              Value singularBeta = ttValue - 3 * depth;
              Depth singularDepth = (depth - 1) / 2;

              ss->excludedMove = move;
              value = search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
              ss->excludedMove = MOVE_NONE;

              if (value < singularBeta)
              {
                  extension = 1;

                  // Avoid search explosion by limiting the number of double extensions
                  if (  !PvNode
                      && value < singularBeta - 26
                      && ss->doubleExtensions <= 8)
                      extension = 2;
              }

              // Multi-cut pruning
              // Our ttMove is assumed to fail high, and now we failed high also on a reduced
              // search without the ttMove. So we assume this expected Cut-node is not singular,
              // that multiple moves fail high, and we can prune the whole subtree by returning
              // a soft bound.
              else if (singularBeta >= beta)
                  return singularBeta;

              // If the eval of ttMove is greater than beta, we reduce it (negative extension)
              else if (ttValue >= beta)
                  extension = -2;

              // If the eval of ttMove is less than alpha and value, we reduce it (negative extension)
              else if (ttValue <= alpha && ttValue <= value)
                  extension = -1;
          }

          // Check extensions (~1 Elo)
          else if (   givesCheck
                   && depth > 9
                   && abs(ss->staticEval) > 71)
              extension = 1;

          // Quiet ttMove extensions (~0 Elo)
          else if (   PvNode
                   && move == ttMove
                   && move == ss->killers[0]
                   && (*contHist[0])[movedPiece][to_sq(move)] >= 5491)
              extension = 1;
      }

      // Add extension to new depth
      newDepth += extension;
      ss->doubleExtensions = (ss-1)->doubleExtensions + (extension == 2);

      // Speculative prefetch as early as possible
      prefetch(TT.first_entry(pos.key_after(move)));

      // Update the current move (this must be done after singular extension search)
      ss->currentMove = move;
      ss->continuationHistory = &thisThread->continuationHistory[ss->inCheck]
                                                                [capture]
                                                                [movedPiece]
                                                                [to_sq(move)];

      // Step 16. Make the move
      pos.do_move(move, st, givesCheck);

      bool doDeeperSearch = false;

      // Step 17. Late moves reduction / extension (LMR, ~98 Elo)
      // We use various heuristics for the sons of a node after the first son has
      // been searched. In general we would like to reduce them, but there are many
      // cases where we extend a son if it has good chances to be "interesting".
      if (    depth >= 2
          &&  moveCount > 1 + (PvNode && ss->ply <= 1)
          && (   !ss->ttPv
              || !capture
              || (cutNode && (ss-1)->moveCount > 1)))
      {
          Depth r = reduction(improving, depth, moveCount, delta, thisThread->rootDelta);

          // Decrease reduction if position is or has been on the PV
          // and node is not likely to fail low. (~3 Elo)
          if (   ss->ttPv
              && !likelyFailLow)
              r -= 2;

          // Decrease reduction if opponent's move count is high (~1 Elo)
          if ((ss-1)->moveCount > 7)
              r--;

          // Increase reduction for cut nodes (~3 Elo)
          if (cutNode && move != ss->killers[0])
              r += 2;

          // Increase reduction if ttMove is a capture (~3 Elo)
          if (ttCapture)
              r++;

          // Decrease reduction at PvNodes if bestvalue
          // is vastly different from static evaluation
          if (PvNode && !ss->inCheck && abs(ss->staticEval - bestValue) > 250)
              r--;

          // Decrease reduction for PvNodes based on depth
          if (PvNode)
              r -= 1 + 15 / ( 3 + depth );

          // Increase reduction if next ply has a lot of fail high else reset count to 0
          if ((ss+1)->cutoffCnt > 3 && !PvNode)
              r++;

          ss->statScore =  thisThread->mainHistory[us][from_to(move)]
                         + (*contHist[0])[movedPiece][to_sq(move)]
                         + (*contHist[1])[movedPiece][to_sq(move)]
                         + (*contHist[3])[movedPiece][to_sq(move)]
                         - 4334;

          // Decrease/increase reduction for moves with a good/bad history (~30 Elo)
          r -= ss->statScore / 15914;

          // In general we want to cap the LMR depth search at newDepth. But if reductions
          // are really negative and movecount is low, we allow this move to be searched
          // deeper than the first move (this may lead to hidden double extensions).
          int deeper =   r >= -1                   ? 0
                       : moveCount <= 4            ? 2
                       : PvNode                    ? 1
                       : cutNode && moveCount <= 8 ? 1
                       :                             0;

          Depth d = std::clamp(newDepth - r, 1, newDepth + deeper);

          value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, d, true);

          // If the son is reduced and fails high it will be re-searched at full depth
          doFullDepthSearch = value > alpha && d < newDepth;
          doDeeperSearch = value > (alpha + 78 + 11 * (newDepth - d));
          didLMR = true;
      }
      else
      {
          doFullDepthSearch = !PvNode || moveCount > 1;
          didLMR = false;
      }

      // Step 18. Full depth search when LMR is skipped or fails high
      if (doFullDepthSearch)
      {
          value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth + doDeeperSearch, !cutNode);

          // If the move passed LMR update its stats
          if (didLMR)
          {
              int bonus = value > alpha ?  stat_bonus(newDepth)
                                        : -stat_bonus(newDepth);

              if (capture)
                  bonus /= 6;

              update_continuation_histories(ss, movedPiece, to_sq(move), bonus);
          }
      }

      // For PV nodes only, do a full PV search on the first move or after a fail
      // high (in the latter case search only if value < beta), otherwise let the
      // parent node fail low with value <= alpha and try another move.
      if (PvNode && (moveCount == 1 || (value > alpha && (rootNode || value < beta))))
      {
          (ss+1)->pv = pv;
          (ss+1)->pv[0] = MOVE_NONE;

          value = -search<PV>(pos, ss+1, -beta, -alpha,
                              std::min(maxNextDepth, newDepth), false);
      }

      // Step 19. Undo move
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Step 20. Check for a new best move
      // Finished searching the move. If a stop occurred, the return value of
      // the search cannot be trusted, and we return immediately without
      // updating best move, PV and TT.
      if (Threads.stop.load(std::memory_order_relaxed))
          return VALUE_ZERO;

      if (rootNode)
      {
          RootMove& rm = *std::find(thisThread->rootMoves.begin(),
                                    thisThread->rootMoves.end(), move);

          rm.averageScore = rm.averageScore != -VALUE_INFINITE ? (2 * value + rm.averageScore) / 3 : value;

          // PV move or new best move?
          if (moveCount == 1 || value > alpha)
          {
              rm.score = value;
              rm.selDepth = thisThread->selDepth;
              rm.pv.resize(1);

              assert((ss+1)->pv);

              for (Move* m = (ss+1)->pv; *m != MOVE_NONE; ++m)
                  rm.pv.push_back(*m);

              // We record how often the best move has been changed in each iteration.
              // This information is used for time management. In MultiPV mode,
              // we must take care to only do this for the first PV line.
              if (   moveCount > 1
                  && !thisThread->pvIdx)
                  ++thisThread->bestMoveChanges;
          }
          else
              // All other moves but the PV are set to the lowest value: this
              // is not a problem when sorting because the sort is stable and the
              // move position in the list is preserved - just the PV is pushed up.
              rm.score = -VALUE_INFINITE;
      }

      if (value > bestValue)
      {
          bestValue = value;

          if (value > alpha)
          {
              bestMove = move;

              if (PvNode && !rootNode) // Update pv even in fail-high case
                  update_pv(ss->pv, move, (ss+1)->pv);

              if (PvNode && value < beta) // Update alpha! Always alpha < beta
              {
                  alpha = value;

                  // Reduce other moves if we have found at least one score improvement
                  if (   depth > 2
                      && depth < 7
                      && beta  <  VALUE_KNOWN_WIN
                      && alpha > -VALUE_KNOWN_WIN)
                     depth -= 1;

                  assert(depth > 0);
              }
              else
              {
                  ss->cutoffCnt++;
                  assert(value >= beta); // Fail high
                  break;
              }
          }
      }
      else
         ss->cutoffCnt = 0;


      // If the move is worse than some previously searched move, remember it to update its stats later
      if (move != bestMove)
      {
          if (capture && captureCount < 32)
              capturesSearched[captureCount++] = move;

          else if (!capture && quietCount < 64)
              quietsSearched[quietCount++] = move;
      }
    }

    // The following condition would detect a stop only after move loop has been
    // completed. But in this case bestValue is valid because we have fully
    // searched our subtree, and we can anyhow save the result in TT.
    /*
       if (Threads.stop)
        return VALUE_DRAW;
    */

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it
    // must be a mate or a stalemate. If we are in a singular extension search then
    // return a fail low score.

    assert(moveCount || !ss->inCheck || excludedMove || !MoveList<LEGAL>(pos).size());

    if (!moveCount)
        bestValue = excludedMove ? alpha :
                    ss->inCheck  ? mated_in(ss->ply)
                                 : VALUE_DRAW;

    // If there is a move which produces search value greater than alpha we update stats of searched moves
    else if (bestMove)
        update_all_stats(pos, ss, bestMove, bestValue, beta, prevSq,
                         quietsSearched, quietCount, capturesSearched, captureCount, depth);

    // Bonus for prior countermove that caused the fail low
    else if (   (depth >= 4 || PvNode)
             && !priorCapture)
    {
        //Assign extra bonus if current node is PvNode or cutNode
        //or fail low was really bad
        bool extraBonus =    PvNode
                          || cutNode
                          || bestValue < alpha - 70 * depth;

        update_continuation_histories(ss-1, pos.piece_on(prevSq), prevSq, stat_bonus(depth) * (1 + extraBonus));
    }

    if (PvNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree.
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss-1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    if (!excludedMove && !(rootNode && thisThread->pvIdx))
        tte->save(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                  bestValue >= beta ? BOUND_LOWER :
                  PvNode && bestMove ? BOUND_EXACT : BOUND_UPPER,
                  depth, bestMove, ss->staticEval);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }


  // qsearch() is the quiescence search function, which is called by the main search
  // function with zero depth, or recursively with further decreasing depth per call.
  template <NodeType nodeType>
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(depth <= 0);

    Move pv[MAX_PLY+1];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry* tte;
    Key posKey;
    Move ttMove, move, bestMove;
    Depth ttDepth;
    Value bestValue, value, ttValue, futilityValue, futilityBase;
    bool pvHit, givesCheck, capture;
    int moveCount;

    if (PvNode)
    {
        (ss+1)->pv = pv;
        ss->pv[0] = MOVE_NONE;
    }

    Thread* thisThread = pos.this_thread();
    bestMove = MOVE_NONE;
    ss->inCheck = pos.checkers();
    moveCount = 0;

    // Check for an immediate draw or maximum ply reached
    if (   pos.is_draw(ss->ply)
        || ss->ply >= MAX_PLY)
        return (ss->ply >= MAX_PLY && !ss->inCheck) ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Decide whether or not to include checks: this fixes also the type of
    // TT entry depth that we are going to use. Note that in qsearch we use
    // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
    ttDepth = ss->inCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS
                                                  : DEPTH_QS_NO_CHECKS;
    // Transposition table lookup
    posKey = pos.key();
    tte = TT.probe(posKey, ss->ttHit);
    ttValue = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove = ss->ttHit ? tte->move() : MOVE_NONE;
    pvHit = ss->ttHit && tte->is_pv();

    if (  !PvNode
        && ss->ttHit
        && tte->depth() >= ttDepth
        && ttValue != VALUE_NONE // Only in case of TT access race
        && (ttValue >= beta ? (tte->bound() & BOUND_LOWER)
                            : (tte->bound() & BOUND_UPPER)))
        return ttValue;

    // Evaluate the position statically
    if (ss->inCheck)
    {
        ss->staticEval = VALUE_NONE;
        bestValue = futilityBase = -VALUE_INFINITE;
    }
    else
    {
        if (ss->ttHit)
        {
            // Never assume anything about values stored in TT
            if ((ss->staticEval = bestValue = tte->eval()) == VALUE_NONE)
                ss->staticEval = bestValue = evaluate(pos);

            // ttValue can be used as a better position evaluation (~7 Elo)
            if (    ttValue != VALUE_NONE
                && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttValue;
        }
        else
            // In case of null move search use previous static eval with a different sign
            ss->staticEval = bestValue =
            (ss-1)->currentMove != MOVE_NULL ? evaluate(pos)
                                             : -(ss-1)->staticEval;

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            // Save gathered info in transposition table
            if (!ss->ttHit)
                tte->save(posKey, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER,
                          DEPTH_NONE, MOVE_NONE, ss->staticEval);

            return bestValue;
        }

        if (PvNode && bestValue > alpha)
            alpha = bestValue;

        futilityBase = bestValue + 118;
    }

    const PieceToHistory* contHist[] = { (ss-1)->continuationHistory, (ss-2)->continuationHistory,
                                          nullptr                   , (ss-4)->continuationHistory,
                                          nullptr                   , (ss-6)->continuationHistory };

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions, and other checks (only if depth >= DEPTH_QS_CHECKS)
    // will be generated.
    Square prevSq = to_sq((ss-1)->currentMove);
    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory,
                                      &thisThread->captureHistory,
                                      contHist,
                                      prevSq);

    int quietCheckEvasions = 0;

    // Loop through the moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move()) != MOVE_NONE)
    {
      assert(is_ok(move));

      // Check for legality
      if (!pos.legal(move))
          continue;

      givesCheck = pos.gives_check(move);
      capture = pos.capture(move);

      moveCount++;

      // Futility pruning and moveCount pruning (~5 Elo)
      if (    bestValue > VALUE_TB_LOSS_IN_MAX_PLY
          && !givesCheck
          &&  to_sq(move) != prevSq
          &&  futilityBase > -VALUE_KNOWN_WIN
          &&  type_of(move) != PROMOTION)
      {

          if (moveCount > 2)
              continue;

          futilityValue = futilityBase + PieceValue[EG][pos.piece_on(to_sq(move))];

          if (futilityValue <= alpha)
          {
              bestValue = std::max(bestValue, futilityValue);
              continue;
          }

          if (futilityBase <= alpha && !pos.see_ge(move, VALUE_ZERO + 1))
          {
              bestValue = std::max(bestValue, futilityBase);
              continue;
          }
      }

      // Do not search moves with negative SEE values (~5 Elo)
      if (    bestValue > VALUE_TB_LOSS_IN_MAX_PLY
          && !pos.see_ge(move))
          continue;

      // Speculative prefetch as early as possible
      prefetch(TT.first_entry(pos.key_after(move)));

      ss->currentMove = move;
      ss->continuationHistory = &thisThread->continuationHistory[ss->inCheck]
                                                                [capture]
                                                                [pos.moved_piece(move)]
                                                                [to_sq(move)];

      // Continuation history based pruning (~2 Elo)
      if (  !capture
          && bestValue > VALUE_TB_LOSS_IN_MAX_PLY
          && (*contHist[0])[pos.moved_piece(move)][to_sq(move)] < CounterMovePruneThreshold
          && (*contHist[1])[pos.moved_piece(move)][to_sq(move)] < CounterMovePruneThreshold)
          continue;

      // movecount pruning for quiet check evasions
      if (  bestValue > VALUE_TB_LOSS_IN_MAX_PLY
          && quietCheckEvasions > 1
          && !capture
          && ss->inCheck)
          continue;

      quietCheckEvasions += !capture && ss->inCheck;

      // Make and search the move
      pos.do_move(move, st, givesCheck);
      value = -qsearch<nodeType>(pos, ss+1, -beta, -alpha, depth - 1);
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Check for a new best move
      if (value > bestValue)
      {
          bestValue = value;

          if (value > alpha)
          {
              bestMove = move;

              if (PvNode) // Update pv even in fail-high case
                  update_pv(ss->pv, move, (ss+1)->pv);

              if (PvNode && value < beta) // Update alpha here!
                  alpha = value;
              else
                  break; // Fail high
          }
       }
    }

    // All legal moves have been searched. A special case: if we're in check
    // and no legal moves were found, it is checkmate.
    if (ss->inCheck && bestValue == -VALUE_INFINITE)
    {
        assert(!MoveList<LEGAL>(pos).size());

        return mated_in(ss->ply); // Plies to mate from the root
    }

    // Save gathered info in transposition table
    tte->save(posKey, value_to_tt(bestValue, ss->ply), pvHit,
              bestValue >= beta ? BOUND_LOWER : BOUND_UPPER,
              ttDepth, bestMove, ss->staticEval);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
  }


  // value_to_tt() adjusts a mate or TB score from "plies to mate from the root" to
  // "plies to mate from the current position". Standard scores are unchanged.
  // The function is called before storing a value in the transposition table.

  Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);

    return  v >= VALUE_TB_WIN_IN_MAX_PLY  ? v + ply
          : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply : v;
  }


  // value_from_tt() is the inverse of value_to_tt(): it adjusts a mate or TB score
  // from the transposition table (which refers to the plies to mate/be mated from
  // current position) to "plies to mate/be mated (TB win/loss) from the root". However,
  // for mate scores, to avoid potentially false mate scores related to the 50 moves rule
  // and the graph history interaction, we return an optimal TB score instead.

  Value value_from_tt(Value v, int ply, int r50c) {

    if (v == VALUE_NONE)
        return VALUE_NONE;

    if (v >= VALUE_TB_WIN_IN_MAX_PLY)  // TB win or better
    {
        if (v >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - v > 99 - r50c)
            return VALUE_MATE_IN_MAX_PLY - 1; // do not return a potentially false mate score

        return v - ply;
    }

    if (v <= VALUE_TB_LOSS_IN_MAX_PLY) // TB loss or worse
    {
        if (v <= VALUE_MATED_IN_MAX_PLY && VALUE_MATE + v > 99 - r50c)
            return VALUE_MATED_IN_MAX_PLY + 1; // do not return a potentially false mate score

        return v + ply;
    }

    return v;
  }


  // update_pv() adds current move and appends child pv[]

  void update_pv(Move* pv, Move move, Move* childPv) {

    for (*pv++ = move; childPv && *childPv != MOVE_NONE; )
        *pv++ = *childPv++;
    *pv = MOVE_NONE;
  }


  // update_all_stats() updates stats at the end of search() when a bestMove is found

  void update_all_stats(const Position& pos, Stack* ss, Move bestMove, Value bestValue, Value beta, Square prevSq,
                        Move* quietsSearched, int quietCount, Move* capturesSearched, int captureCount, Depth depth) {

    int bonus1, bonus2;
    Color us = pos.side_to_move();
    Thread* thisThread = pos.this_thread();
    CapturePieceToHistory& captureHistory = thisThread->captureHistory;
    Piece moved_piece = pos.moved_piece(bestMove);
    PieceType captured = type_of(pos.piece_on(to_sq(bestMove)));

    bonus1 = stat_bonus(depth + 1);
    bonus2 = bestValue > beta + PawnValueMg ? bonus1               // larger bonus
                                            : stat_bonus(depth);   // smaller bonus

    if (!pos.capture(bestMove))
    {
        // Increase stats for the best move in case it was a quiet move
        update_quiet_stats(pos, ss, bestMove, bonus2);

        // Decrease stats for all non-best quiet moves
        for (int i = 0; i < quietCount; ++i)
        {
            thisThread->mainHistory[us][from_to(quietsSearched[i])] << -bonus2;
            update_continuation_histories(ss, pos.moved_piece(quietsSearched[i]), to_sq(quietsSearched[i]), -bonus2);
        }
    }
    else
        // Increase stats for the best move in case it was a capture move
        captureHistory[moved_piece][to_sq(bestMove)][captured] << bonus1;

    // Extra penalty for a quiet early move that was not a TT move or
    // main killer move in previous ply when it gets refuted.
    if (   ((ss-1)->moveCount == 1 + (ss-1)->ttHit || ((ss-1)->currentMove == (ss-1)->killers[0]))
        && !pos.captured_piece())
            update_continuation_histories(ss-1, pos.piece_on(prevSq), prevSq, -bonus1);

    // Decrease stats for all non-best capture moves
    for (int i = 0; i < captureCount; ++i)
    {
        moved_piece = pos.moved_piece(capturesSearched[i]);
        captured = type_of(pos.piece_on(to_sq(capturesSearched[i])));
        captureHistory[moved_piece][to_sq(capturesSearched[i])][captured] << -bonus1;
    }
  }


  // update_continuation_histories() updates histories of the move pairs formed
  // by moves at ply -1, -2, -4, and -6 with current move.

  void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {

    for (int i : {1, 2, 4, 6})
    {
        // Only update first 2 continuation histories if we are in check
        if (ss->inCheck && i > 2)
            break;
        if (is_ok((ss-i)->currentMove))
            (*(ss-i)->continuationHistory)[pc][to] << bonus;
    }
  }


  // update_quiet_stats() updates move sorting heuristics

  void update_quiet_stats(const Position& pos, Stack* ss, Move move, int bonus) {

    // Update killers
    if (ss->killers[0] != move)
    {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = move;
    }

    Color us = pos.side_to_move();
    Thread* thisThread = pos.this_thread();
    thisThread->mainHistory[us][from_to(move)] << bonus;
    update_continuation_histories(ss, pos.moved_piece(move), to_sq(move), bonus);

    // Update countermove history
    if (is_ok((ss-1)->currentMove))
    {
        Square prevSq = to_sq((ss-1)->currentMove);
        thisThread->counterMoves[pos.piece_on(prevSq)][prevSq] = move;
    }
  }

  // When playing with strength handicap, choose best move among a set of RootMoves
  // using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.

  Move Skill::pick_best(size_t multiPV) {

    const RootMoves& rootMoves = Threads.main()->rootMoves;
    static PRNG rng(now()); // PRNG sequence should be non-deterministic

    // RootMoves are already sorted by score in descending order
    Value topScore = rootMoves[0].score;
    int delta = std::min(topScore - rootMoves[multiPV - 1].score, PawnValueMg);
    int maxScore = -VALUE_INFINITE;
    double weakness = 120 - 2 * level;

    // Choose best move. For each move score we add two terms, both dependent on
    // weakness. One is deterministic and bigger for weaker levels, and one is
    // random. Then we choose the move with the resulting highest score.
    for (size_t i = 0; i < multiPV; ++i)
    {
        // This is our magic formula
        int push = int((  weakness * int(topScore - rootMoves[i].score)
                        + delta * (rng.rand<unsigned>() % int(weakness))) / 128);

        if (rootMoves[i].score + push >= maxScore)
        {
            maxScore = rootMoves[i].score + push;
            best = rootMoves[i].pv[0];
        }
    }

    return best;
  }

} // namespace


/// MainThread::check_time() is used to print debug info and, more importantly,
/// to detect when we are out of available time and thus stop the search.

void MainThread::check_time() {

  if (--callsCnt > 0)
      return;

  // When using nodes, ensure checking rate is not lower than 0.1% of nodes
  callsCnt = Limits.nodes ? std::min(1024, int(Limits.nodes / 1024)) : 1024;

  static TimePoint lastInfoTime = now();

  TimePoint elapsed = Time.elapsed();
  TimePoint tick = Limits.startTime + elapsed;

  if (tick - lastInfoTime >= 1000)
  {
      lastInfoTime = tick;
      dbg_print();
  }

  // We should not stop pondering until told so by the GUI
  if (ponder)
      return;

  if (   (Limits.use_time_management() && (elapsed > Time.maximum() - 10 || stopOnPonderhit))
      || (Limits.movetime && elapsed >= Limits.movetime)
      || (Limits.nodes && Threads.nodes_searched() >= (uint64_t)Limits.nodes))
      Threads.stop = true;
}


/// UCI::pv() formats PV information according to the UCI protocol. UCI requires
/// that all (if any) unsearched PV lines are sent using a previous search score.

string UCI::pv(const Position& pos, Depth depth, Value alpha, Value beta) {

  std::stringstream ss;
  TimePoint elapsed = Time.elapsed() + 1;
  const RootMoves& rootMoves = pos.this_thread()->rootMoves;
  size_t pvIdx = pos.this_thread()->pvIdx;
  size_t multiPV = std::min((size_t)Options["MultiPV"], rootMoves.size());
  uint64_t nodesSearched = Threads.nodes_searched();
  uint64_t tbHits = Threads.tb_hits() + (pos.this_thread()->rootInTB ? rootMoves.size() : 0);

  for (size_t i = 0; i < multiPV; ++i)
  {
      bool updated = rootMoves[i].score != -VALUE_INFINITE;

      if (depth == 1 && !updated && i > 0)
          continue;

      Depth d = updated ? depth : std::max(1, depth - 1);
      Value v = updated ? rootMoves[i].score : rootMoves[i].previousScore;

      if (v == -VALUE_INFINITE)
          v = VALUE_ZERO;

      bool tb = pos.this_thread()->rootInTB && abs(v) < VALUE_MATE_IN_MAX_PLY;
      v = tb ? rootMoves[i].tbScore : v;

      if (ss.rdbuf()->in_avail()) // Not at first line
          ss << "\n";

      ss << "info"
         << " depth "    << d
         << " seldepth " << rootMoves[i].selDepth
         << " multipv "  << i + 1
         << " score "    << UCI::value(v);

      if (Options["UCI_ShowWDL"])
          ss << UCI::wdl(v, pos.game_ply());

      if (!tb && i == pvIdx)
          ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

      ss << " nodes "    << nodesSearched
         << " nps "      << nodesSearched * 1000 / elapsed;

      if (elapsed > 1000) // Earlier makes little sense
          ss << " hashfull " << TT.hashfull();

      ss << " tbhits "   << tbHits
         << " time "     << elapsed
         << " pv";

      for (Move m : rootMoves[i].pv)
          ss << " " << UCI::move(m, pos.is_chess960());
  }

  return ss.str();
}


/// RootMove::extract_ponder_from_tt() is called in case we have no ponder move
/// before exiting the search, for instance, in case we stop the search during a
/// fail high at root. We try hard to have a ponder move to return to the GUI,
/// otherwise in case of 'ponder on' we have nothing to think on.

bool RootMove::extract_ponder_from_tt(Position& pos) {

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    bool ttHit;

    assert(pv.size() == 1);

    if (pv[0] == MOVE_NONE)
        return false;

    pos.do_move(pv[0], st);
    TTEntry* tte = TT.probe(pos.key(), ttHit);

    if (ttHit)
    {
        Move m = tte->move(); // Local copy to be SMP safe
        if (MoveList<LEGAL>(pos).contains(m))
            pv.push_back(m);
    }

    pos.undo_move(pv[0]);
    return pv.size() > 1;
}

void Tablebases::rank_root_moves(Position& pos, Search::RootMoves& rootMoves) {

    pos.this_thread()->Cardinality = int(Options["SyzygyProbeLimit"]);
    pos.this_thread()->ProbeDepth = int(Options["SyzygyProbeDepth"]);
    pos.this_thread()->UseRule50 = bool(Options["Syzygy50MoveRule"]);
    pos.this_thread()->rootInTB = false;

    auto& cardinality = pos.this_thread()->Cardinality;
    auto& probeDepth = pos.this_thread()->ProbeDepth;
    auto& rootInTB = pos.this_thread()->rootInTB;
    bool dtz_available = true;

    // Tables with fewer pieces than SyzygyProbeLimit are searched with
    // ProbeDepth == DEPTH_ZERO
    if (cardinality > Tablebases::MaxCardinality)
    {
        cardinality = Tablebases::MaxCardinality;
        probeDepth = 0;
    }

    if (cardinality >= popcount(pos.pieces()) && !pos.can_castle(ANY_CASTLING))
    {
        // Rank moves using DTZ tables
        rootInTB = root_probe(pos, rootMoves);

        if (!rootInTB)
        {
            // DTZ tables are missing; try to rank moves using WDL tables
            dtz_available = false;
            rootInTB = root_probe_wdl(pos, rootMoves);
        }
    }

    if (rootInTB)
    {
        // Sort moves according to TB rank
        std::stable_sort(rootMoves.begin(), rootMoves.end(),
                  [](const RootMove &a, const RootMove &b) { return a.tbRank > b.tbRank; } );

        // Probe during search only if DTZ is not available and we are winning
        if (dtz_available || rootMoves[0].tbScore <= VALUE_DRAW)
            cardinality = 0;
    }
    else
    {
        // Clean up if root_probe() and root_probe_wdl() have failed
        for (auto& m : rootMoves)
            m.tbRank = 0;
    }

}

// --- expose the functions such as fixed depth search used for learning to the outside
namespace Search
{
  // For learning, prepare a stub that can call search,qsearch() from one thread.
  // From now on, it is better to have a Searcher and prepare a substitution table for each thread like Apery.
  // It might have been good.

  // Initialization for learning.
  // Called from Tools::search(),Tools::qsearch().
  static bool init_for_search(Position& pos, Stack* ss)
  {

    // RootNode requires ss->ply == 0.
    // Because it clears to zero, ss->ply == 0, so it's okay...

    std::memset(ss - 7, 0, 10 * sizeof(Stack));

    // Regarding this_thread.

    {
      auto th = pos.this_thread();

      th->completedDepth = 0;
      th->selDepth = 0;
      th->rootDepth = 0;
      th->nmpMinPly = th->bestMoveChanges = th->failedHighCnt = 0;

      // Zero initialization of the number of search nodes
      th->nodes = 0;
      th->maxNodes = 0;

      // Clear all history types. This initialization takes a little time, and
      // the accuracy of the search is rather low, so the good and bad are
      // not well understood.

      // th->clear();

      // Evaluation score is from the white point of view
      th->trend = make_score(0, 0);

      for (int i = 7; i > 0; i--)
          (ss - i)->continuationHistory = &th->continuationHistory[0][0][NO_PIECE][0]; // Use as a sentinel

      for (int i = 0; i <= MAX_PLY + 2; ++i)
          (ss + i)->ply = i;

      // set rootMoves
      auto& rootMoves = th->rootMoves;

      rootMoves.clear();
      for (auto m: MoveList<LEGAL>(pos))
        rootMoves.push_back(Search::RootMove(m));

      // Check if we're at a terminal node. Otherwise we end up returning
      // malformed PV later on.
      if (rootMoves.empty())
        return false;

      Tablebases::rank_root_moves(pos, rootMoves);
    }

    return true;
  }

  // Stationary search.
  //
  // Precondition) Search thread is set by pos.set_this_thread(Threads[thread_id]).
  // Also, when Threads.stop arrives, the search is interrupted, so the PV at that time is not correct.
  // After returning from search(), if Threads.stop == true, do not use the search result.
  // Also, note that before calling, if you do not call it with Threads.stop == false, the search will be interrupted and it will return.
  //
  // If it is clogged, MOVE_RESIGN is returned in the PV array.
  //
  //Although it was possible to specify alpha and beta with arguments, this will show the result when searching in that window
  // Because it writes to the substitution table, the value that can be pruned is written to that window when learning
  // As it has a bad effect, I decided to stop allowing the window range to be specified.
  ValueAndPV qsearch(Position& pos)
  {
    Stack stack[MAX_PLY+10], *ss = stack+7;
    Move  pv[MAX_PLY+1];

    if (!init_for_search(pos, ss))
      return {};

    ss->pv = pv; // For the time being, it must be a dummy and somewhere with a buffer.

    if (pos.is_draw(0)) {
      // Return draw value if draw.
      return { VALUE_DRAW, {} };
    }

    // Is it stuck?
    if (MoveList<LEGAL>(pos).size() == 0)
    {
      // Return the mated value if checkmated.
      return { mated_in(/*ss->ply*/ 0 + 1), {} };
    }

    auto bestValue = Stockfish::qsearch<PV>(pos, ss, -VALUE_INFINITE, VALUE_INFINITE, 0);

    // Returns the PV obtained.
    std::vector<Move> pvs;
    for (Move* p = &ss->pv[0]; is_ok(*p); ++p)
      pvs.push_back(*p);

    return ValueAndPV(bestValue, pvs);
  }

  // Normal search. Depth depth (specified as an integer).
  // 3 If you want a score for hand reading,
  // auto v = search(pos,3);
  // Do something like
  // Evaluation value is obtained in v.first and PV is obtained in v.second.
  // When multi pv is enabled, you can get the PV (reading line) array in pos.this_thread()->rootMoves[N].pv.
  // Specify multi pv with the argument multiPV of this function. (The value of Options["MultiPV"] is ignored)
  //
  // Declaration win judgment is not done as root (because it is troublesome to handle), so it is not done here.
  // Handle it by the caller.
  //
  // Precondition) Search thread is set by pos.set_this_thread(Threads[thread_id]).
  // Also, when Threads.stop arrives, the search is interrupted, so the PV at that time is not correct.
  // After returning from search(), if Threads.stop == true, do not use the search result.
  // Also, note that before calling, if you do not call it with Threads.stop == false, the search will be interrupted and it will return.

  ValueAndPV search(Position& pos, int depth_, size_t multiPV /* = 1 */, uint64_t nodesLimit /* = 0 */)
  {
    // Sometimes a depth takes extreme amount of time (in the order of x1000 or more)
    // than the previous depth, which can cause the search bounded by nodes to go for a long time.
    // Because of that we add an additional limit that's 10x higher and is checked within
    // the search function and can kill the search regardless of whether the full depth
    // has been completed or not.
    constexpr uint64_t nodesOversearchFactor = 10;

    std::vector<Move> pvs;

    Depth depth = depth_;
    if (depth < 0)
      return std::pair<Value, std::vector<Move>>(Eval::evaluate(pos), std::vector<Move>());

    if (depth == 0)
      return qsearch(pos);

    Stack stack[MAX_PLY + 10], * ss = stack + 7;
    Move pv[MAX_PLY + 1];

    if (!init_for_search(pos, ss))
      return {};

	ss->pv = pv; // For the time being, it must be a dummy and somewhere with a buffer.

    // Initialize the variables related to this_thread
    auto th = pos.this_thread();
    auto& rootDepth = th->rootDepth;
    auto& pvIdx = th->pvIdx;
    auto& pvLast = th->pvLast;
    auto& rootMoves = th->rootMoves;
    auto& completedDepth = th->completedDepth;
    auto& selDepth = th->selDepth;

     // A function to search the top N of this stage as best move
     //size_t multiPV = Options["MultiPV"];

     // Do not exceed the number of moves in this situation
    multiPV = std::min(multiPV, rootMoves.size());

     // If you do not multiply the node limit by the value of MultiPV, you will not be thinking about the same node for one candidate hand when you fix the depth and have MultiPV.
    nodesLimit *= multiPV;

    Value alpha = -VALUE_INFINITE;
    Value beta = VALUE_INFINITE;
    Value delta = -VALUE_INFINITE;
    Value bestValue = -VALUE_INFINITE;

    if (nodesLimit != 0)
    {
      th->maxNodes = nodesLimit * nodesOversearchFactor;
    }

    while ((rootDepth += 1) <= depth
      // exit this loop even if the node limit is exceeded
      // The number of search nodes is passed in the argument of this function.
      && !(nodesLimit /* limited nodes */ && th->nodes.load(std::memory_order_relaxed) >= nodesLimit)
      )
    {
      for (RootMove& rm : rootMoves)
        rm.previousScore = rm.score;

      size_t pvFirst = 0;
      pvLast = 0;

      // MultiPV loop. We perform a full root search for each PV line
      for (pvIdx = 0; pvIdx < multiPV && !Threads.stop; ++pvIdx)
      {
        if (pvIdx == pvLast)
        {
          pvFirst = pvLast;
          for (pvLast++; pvLast < rootMoves.size(); pvLast++)
            if (rootMoves[pvLast].tbRank != rootMoves[pvFirst].tbRank)
              break;
        }

        // selDepth output with USI info for each depth and PV line
        selDepth = 0;

        // Switch to aspiration search for depth 5 and above.
        if (rootDepth >= 4)
        {
            Value prev = rootMoves[pvIdx].previousScore;
            delta = Value(17);
            alpha = std::max(prev - delta,-VALUE_INFINITE);
            beta  = std::min(prev + delta, VALUE_INFINITE);
        }

        while (true)
        {
          Depth adjustedDepth = std::max(1, rootDepth);
          bestValue = Stockfish::search<Root>(pos, ss, alpha, beta, adjustedDepth, false);

          if (th->maxNodes && th->nodes.load(std::memory_order_relaxed) >= th->maxNodes)
          {
            break;
          }

          stable_sort(rootMoves.begin() + pvIdx, rootMoves.end());
          //my_stable_sort(pos.this_thread()->thread_id(),&rootMoves[0] + pvIdx, rootMoves.size() - pvIdx);

          // Expand aspiration window for fail low/high.
          // However, if it is the value specified by the argument, it will be treated as fail low/high and break.
          if (bestValue <= alpha)
          {
            beta = (alpha + beta) / 2;
            alpha = std::max(bestValue - delta, -VALUE_INFINITE);
          }
          else if (bestValue >= beta)
          {
            beta = std::min(bestValue + delta, VALUE_INFINITE);
          }
          else
            break;

          delta += delta / 4 + 5;
          assert(-VALUE_INFINITE <= alpha && beta <= VALUE_INFINITE);

          // runaway check
          //assert(th->nodes.load(std::memory_order_relaxed) <= 1000000 );
        }

        stable_sort(rootMoves.begin(), rootMoves.begin() + pvIdx + 1);
        //my_stable_sort(pos.this_thread()->thread_id() , &rootMoves[0] , pvIdx + 1);

      } // multi PV

      completedDepth = rootDepth;
    }

    // Pass PV_is(ok) to eliminate this PV, there may be NULL_MOVE in the middle.
    // MOVE_WIN has never been thrust. (For now)
    for (Move move : rootMoves[0].pv)
    {
      if (!is_ok(move))
        break;
      pvs.push_back(move);
    }

    //sync_cout << rootDepth << sync_endl;

    // Considering multiPV, the score of rootMoves[0] is returned as bestValue.
    bestValue = rootMoves[0].score;

    th->maxNodes = 0;

    return ValueAndPV(bestValue, pvs);
  }



  // This implementation of the MCTS is heavily based on Stephane Nicolet's work here
  // https://github.com/snicolet/Stockfish/commit/28501872a1e7ce84dd1f38ab9e59c5adb0d24b41
  // and the adjusted implementation of it in ShashChess https://github.com/amchess/ShashChess

  namespace MCTS
  {
    static constexpr float sigmoidScale = 600.0f;

    static inline float fast_sigmoid(float x) {
      bool negative = x < 0.0f;
      if (negative)
        x = -x;
      const float xx = x*x;
      const float v = 1.0f / (1.0f + 1.0f / (1.0f + x + xx*(0.555f + xx*0.143f)));
      if (negative)
        return 1.0f - v;
      else
        return v;
    }

    static inline Value reward_to_value(float r) {
        if (r > 0.99f) return  VALUE_KNOWN_WIN;
        if (r < 0.01f) return -VALUE_KNOWN_WIN;

        return Value(-sigmoidScale * std::log(1.0f/r - 1.0f));
    }

    static inline float value_to_reward(Value v) {
      return fast_sigmoid(static_cast<float>(v) * (1.0f / sigmoidScale));
    }

    // struct MCTSNode : store info at one node of the MCTS algorithm

    struct MCTSNode {

      Key                    posKey            = 0;           // for consistency checks
      MCTSNode*              parent            = nullptr;     // only nullptr for the root node
      unique_ptr<MCTSNode[]> children          = nullptr;     // only nullptr for nodes that have not been expanded
      uint64_t               numVisits         = 0;           // the number of playouts for this node and all descendants
      Value                  leafSearchEval    = VALUE_NONE;  // the evaluation from AB playout
      float                  prior             = 0.0f;        // the policy, currently a rough estimation based on the playout of the parent
      float                  actionValue       = 0.0f;        // the accumulated rewards
      float                  actionValueWeight = 0.0f;        // the maximum value for the accumulater rewards
      Move                   prevMove          = MOVE_NONE;   // the move on the edge from the parent
      int                    numChildren       = 0;           // the number of legal moves, filled on expansion
      int                    childId           = 0;           // the index of this node in the parent's children array
      Depth                  leafSearchDepth   = DEPTH_NONE;  // the depth with which the AB playout was done
      bool                   isTerminal        = false;       // whether the node is terminal. Terminal nodes are always "expanded" immediately.


      // ucb_value() calculates the upper confidence bound of a child.
      // When searching for the node to expand/playout we take one with the highest ucb.

      float ucb_value(MCTSNode& child, float explorationFactor, bool flipPerspective = false) const
      {

        assert(explorationFactor >= 0.0f);
        assert(child.actionValue >= 0.0f);
        assert(child.actionValueWeight >= 0.0f);
        assert(child.actionValue <= child.actionValueWeight);
        assert(child.prior >= 0.0f);
        assert(child.prior <= 1.0f);

        // For the nodes which have not been played-out we use the prior.
        // Otherwise we have some averaged score or the eval already.
        float reward = child.numVisits == 0 ? child.prior
                                            : child.actionValue / child.actionValueWeight;

        if (flipPerspective)
          reward = 1.0f - reward;

        // The exploration factor.
        // In theory unplayed nodes should have priority, but we
        // add 1 to avoid div by 0 so they might not always be prioritized.

        if (explorationFactor != 0.0f)
          reward +=
            explorationFactor
            * std::sqrt(std::log(1.0 + numVisits) / (1.0 + child.numVisits));

        assert(!std::isnan(reward));
        assert(reward >= 0.0f);

        return reward;
      }


      // get_best_child() returns a const reference to the best child node,
      // according to the UCB value.

      const MCTSNode& get_best_child(float explorationFactor) const
      {

        assert(!is_leaf());
        assert(numChildren > 0);

        if (numChildren == 1)
        {
          assert(children[0].childId == 0);
          return children[0];
        }

        int bestIdx = -1;
        float bestValue = std::numeric_limits<float>::lowest();
        for (int i = 0 ; i < numChildren ; ++i)
        {
          MCTSNode& child = children[i];
          // The "best" is the one with the best UCB.
          // Child values are with opposite signs.
          const float r = ucb_value(child, explorationFactor, true);
          if (r > bestValue)
          {
            bestIdx = i;
            bestValue = r;
          }
        }

        assert(bestIdx >= 0);
        assert(bestIdx < numChildren);
        assert(children[bestIdx].childId == bestIdx);

        return children[bestIdx];
      }


      // get_best_child() : like the previous one, but does not return a const reference

      MCTSNode& get_best_child(float explorationFactor) {
        return const_cast<MCTSNode&>(static_cast<const MCTSNode*>(this)->get_best_child(explorationFactor));
      }


      // get_best_move() returns a pair (move,value) leading to the best child,
      // according to the action value heuristic.

      std::pair<Move, Value> get_best_move() const {

        assert(!is_leaf());
        assert(numChildren > 0);

        int bestIdx = -1;
        float bestValue = std::numeric_limits<float>::lowest();
        for (int i = 0; i < numChildren ; ++i)
        {
          MCTSNode& child = children[i];
          // The "best" is the one with the best action value.
          // Child values are with opposite signs.
          const float r = 1.0f - (child.actionValue / child.actionValueWeight);
          if (r > bestValue)
          {
            bestIdx = i;
            bestValue = r;
          }
        }

        assert(bestIdx >= 0);
        assert(bestIdx < numChildren);
        assert(children[bestIdx].childId == bestIdx);

        return { children[bestIdx].prevMove, reward_to_value(bestValue) };
      }


      // get_child_by_move() finds a child, given the move that leads to it

      const MCTSNode* get_child_by_move(Move move) const {
        for (int i = 0; i < numChildren ; ++i)
        {
          MCTSNode& child = children[i];
          if (child.prevMove == move)
            return &child;
        }

        return nullptr;
      }


      // get_child_by_move() : like the previous one, but does not return a const

      MCTSNode* get_child_by_move(Move move) {
        return const_cast<MCTSNode*>(static_cast<const MCTSNode*>(this)->get_child_by_move(move));
      }


      // is_root() returns true when node is the root

      bool is_root() const {
        return parent == nullptr;
      }

      // is_leaf() returns true when node is a leaf

      bool is_leaf() const {
        return children == nullptr;
      }
    };


    // struct BackpropValues is a structure to manipulate the kind of stuff
    // that needs to be back-propagated down and up the tree by MCTS.

    struct BackpropValues {

      uint64_t numVisits = 0;
      float actionValue = 0.0f;
      float actionValueWeight = 0.0f;

      // We always keep everything for the side to move perspective.
      // When changing the side the score flips.
      void flip_side() {
        assert(actionValueWeight >= actionValue);
        assert(actionValue >= 0.0f);
        assert(actionValueWeight >= 0.0f);

        actionValue = actionValueWeight - actionValue;
      }
    };


    // struct MonteCarloTreeSearch implements the methods for the MCTS algorithm

    struct MonteCarloTreeSearch {

      // IMPORTANT:
      // The position is stateful so we always have one.
      // It has to match certain expectations in different functions.
      // For example when looking for the node to expand the pos must correspond
      // to the root mcts node. When expanding the node it must correspond to the
      // node being expanded, etc.

      static constexpr Depth terminalEvalDepth = Depth(255);

      // We add a lot of stuff to the actionValue, but the weights differ.
      // The prior is currently bad so low weight,
      static constexpr float priorWeight    = 0.01f;
      static constexpr float terminalWeight = 1.0f;   // could be increased? Different for wins/draws?
      static constexpr float normalWeight   = 1.0f;

      static_assert(priorWeight    > 0.0f);
      static_assert(terminalWeight > 0.0f);
      static_assert(normalWeight   > 0.0f);

      MonteCarloTreeSearch() {}
      MonteCarloTreeSearch(const MonteCarloTreeSearch&) = delete;


      // search_new() : let's start the search !

      ValueAndPV search_new(
        Position& pos,
        std::uint64_t maxPlayouts,
        Depth leafDepth,
        float explorationFactor = 0.25f) {

        init_for_mcts_search(pos);
        return search_continue(pos, maxPlayouts, leafDepth, explorationFactor);
      }


      // search_continue_after_move() : continue after a move and reuse the relevant
      // part of the tree. The prevMove is the move that lead to position 'pos'.
      //
      // TODO: make the node limit be the total.

      ValueAndPV search_continue_after_move( Position& pos,
                                             Move prevMove,
                                             std::uint64_t maxPlayouts,
                                             Depth leafDepth,
                                             float explorationFactor = 0.25f) {
        do_move_at_root(pos, prevMove);
        return search_continue(pos, maxPlayouts, leafDepth, explorationFactor);
      }


      // get_all_continuations() is missing description

      std::vector<MctsContinuation> get_all_continuations() const {

        std::vector<MctsContinuation> continuations;
        continuations.resize(rootNode.numChildren);

        for (int i = 0; i < rootNode.numChildren; ++i)
        {
            MCTSNode& child = rootNode.children[i];

            auto& cont = continuations[i];

            cont.numVisits   = child.numVisits;
            cont.value       = reward_to_value(cont.actionValue);
            cont.pv          = get_pv(child);
            cont.actionValue = 1.0f - (child.actionValue / child.actionValueWeight); // child value is with opposite sign
        }

        std::stable_sort( continuations.begin(),
                          continuations.end(),
                          [](const auto& lhs, const auto& rhs) { return lhs.value > rhs.value; }
        );

        return continuations;
      }


      // search_continue() : continues with the same tree

      ValueAndPV search_continue( Position& pos,
                                  std::uint64_t maxPlayouts,
                                  Depth leafDepth,
                                  float explorationFactor = 0.25f) {

        if (rootNode.leafSearchDepth == DEPTH_NONE)
          do_playout(pos, rootNode, leafDepth);

        while (numPlayouts < maxPlayouts)
        {
          debug << "Starting iteration " << numPlayouts << endl;
          do_search_iteration(pos, leafDepth, explorationFactor);
        }

        if (rootNode.is_leaf())
          return {};
        else
          return { rootNode.get_best_move().second, get_pv() };
      }


      Stack     stackBuffer [MAX_PLY + 10];
      StateInfo statesBuffer[MAX_PLY + 10];

      Stack*     stack  = stackBuffer  + 7;
      StateInfo* states = statesBuffer + 7;

      MCTSNode rootNode;

      int ply = 1;
      int maximumPly = ply; // Effectively the selective depth.
      std::uint64_t numPlayouts = 0;

    private :

      // reset_stats(), recalculate_stats() and accumulate_stats_recursively()
      // are used to recalculate the number of playouts in our MCTS tree. Note
      // that at the moment we call recalculate_stats() each time we play a move
      // at root, to recalculate the stats in the subtree.

      void reset_stats() {
        numPlayouts = 0;
      }

      void accumulate_stats_recursively(MCTSNode& node) {

        if (node.leafSearchDepth != DEPTH_NONE)
          numPlayouts += 1;

        if (!node.is_leaf())
          for (int i = 0; i < node.numChildren; ++i)
            accumulate_stats_recursively(node.children[i]);
      }

      void recalculate_stats() {
        reset_stats();
        accumulate_stats_recursively(rootNode);
      }


      // do_move_at_root() is missing description
      // Tree reuse (?)
      // pos is the position after move.

      void do_move_at_root(Position& pos, Move move) {

        MCTSNode* child = rootNode.get_child_by_move(move);
        if (child == nullptr)
          create_new_root(pos);
        else
        {
          rootNode = std::move(*child);
          rootNode.parent = nullptr;
          rootNode.childId = 0;
          // keep rootNode.prevMove for move ordering heuristics
        }

        recalculate_stats();

        assert(rootNode.posKey == pos.key());
      }


      // do_search_iteration() does one iteration of the search.
      //
      // Basically:
      // 1. find a node to expand/playout
      // 2. if the node is a terminal then we just get the stuff and backprop
      // 3. if we only have prior for the node then do a playout
      // 4. otherwise we expand the children and do at least one playout from the best child (chosen by prior)
      //   4.1. a terminal node counts as a playout. All terminal nodes are played out.
      // 5. Backpropagate all changes down the tree.

      void do_search_iteration(Position& pos, Depth leafDepth, float explorationFactor) {

        MCTSNode& node = find_node_to_expand_or_playout(pos, explorationFactor);
        BackpropValues backprops{};
        if (node.isTerminal)
        {
          debug << "Root is terminal" << endl;
          backprops.numVisits = 1;
          backprops.actionValue += node.actionValue;
          backprops.actionValueWeight += node.actionValueWeight;

          numPlayouts += 1;
        }
        else if (node.leafSearchDepth == DEPTH_NONE)
        {
          // The node is considered the best but it only has a prior value.
          // We don't really want to expand nodes based just on the prior, so
          // first do a playout to get a better estimate, and expand only in the
          // next iteration.
          // Normally we playout immediately the move with the best prior, but that
          // playout can put it below another move.
          backprops = do_playout(pos, node, leafDepth);
        }
        else
        {
          // We have done leaf evaluation with AB search so we know that
          // this node is *actually good* and not just *prior good*, so we
          // can now expand it and do an immediate playout for the node with the best prior.
          backprops = expand_node_and_do_playout(pos, node, leafDepth, explorationFactor);
        }

        backpropagate(pos, node, backprops);
      }


      // Backpropagates() is the function we use to back-propagate the changes
      // after an expand/playout, all the way to the root. The position 'pos'
      // is expected to be at the node from which we start backpropagating.

      void backpropagate(Position& pos, MCTSNode& node, BackpropValues backprops) {

        assert(node.posKey == pos.key());
        assert(ply >= 1);

        debug << "Backpropagating: " << pos.fen() << endl;

        MCTSNode* currentNode = &node;
        while (!currentNode->is_root())
        {
          // On each descent we switch the side to move

          undo_move(pos);
          currentNode = currentNode->parent;
          backprops.flip_side();

          debug << "Backprop step: " << pos.fen() << endl;
          assert(currentNode->posKey == pos.key());

          currentNode->numVisits += backprops.numVisits;
          currentNode->actionValue += backprops.actionValue;
          currentNode->actionValueWeight += backprops.actionValueWeight;
        }

        // At the end we must be at the root

        assert(currentNode == &rootNode);
        assert(rootNode.posKey == pos.key());
      }


      // find_node_to_expand_or_playout() navigates from pos to the node to expand/playout,
      // according to the get_best_child() heuristics.

      MCTSNode& find_node_to_expand_or_playout(Position& pos, float explorationFactor) {
        assert(rootNode.posKey == pos.key());

        // Find a node that has not yet been expanded
        MCTSNode* currentNode = &rootNode;
        while (!currentNode->is_leaf())
        {
          MCTSNode& bestChild = currentNode->get_best_child(explorationFactor);

          do_move(pos, *currentNode, bestChild);

          currentNode = &bestChild;
        }

        return *currentNode;
      }


      // generate_moves_unordered() generates moves in a random order

      int generate_moves_unordered(Position& pos, Move* out) const {
        int moveCount = 0;
        for (auto move : MoveList<LEGAL>(pos))
          out[moveCount++] = move;

        return moveCount;
      }


      // generate_moves_ordered() generates moves with some reasonable ordering.
      // Using this function, we can assume some reasonable priors.

      int generate_moves_ordered(Position& pos, MCTSNode& node, Depth leafDepth, Move* out) const {
        assert(ply >= 1);

        debug << "Generating moves: " << pos.fen() << endl;

        Thread* const thread = pos.this_thread();
        const Square prevSq = to_sq(node.prevMove);
        const Move ttMove = MOVE_NONE; // TODO: retrieve tt move
        const Depth depth = leafDepth + 1;

        const PieceToHistory* contHist[] = {
          stack[ply-1].continuationHistory, stack[ply-2].continuationHistory,
          nullptr                         , stack[ply-4].continuationHistory,
          nullptr                         , stack[ply-6].continuationHistory
        };

        assert(contHist[0] != nullptr);
        assert(contHist[1] != nullptr);
        assert(contHist[3] != nullptr);
        assert(contHist[5] != nullptr);

        MovePicker mp(
          pos,
          ttMove,
          depth,
          &(thread->mainHistory),
          &(thread->captureHistory),
          contHist,
          prevSq
        );

        int moveCount = 0;
        while (true)
        {
          const Move move = mp.next_move();
          debug << "Generated move " << UCI::move(move, false) << ": " << pos.fen() << endl;

          if (move == MOVE_NONE)
            break;

          if (pos.legal(move))
            out[moveCount++] = move;
        }

        debug << "Generated " << moveCount << " legal moves: " << pos.fen() << endl;

        return moveCount;
      }


      // init_for_leaf_search() prepares some global variables in the thread of the
      // given position, for compatibility with the normal AB search of Stockfish.
      // This allows us to use that AB search to get an estimated value of the leaf,
      // if necessary.

      void init_for_leaf_search(Position& pos) {

        auto th = pos.this_thread();

        th->completedDepth = 0;
        th->selDepth = 0;
        th->rootDepth = 0;
        th->nmpMinPly = th->bestMoveChanges = th->failedHighCnt = 0;
        th->nodes = 0;
        th->maxNodes = 0;
      }


      // terminal_value() checks whether the position is terminal. We return
      // the right value if position is terminal, otherwise we return VALUE_NONE.

      Value terminal_value(Position& pos) const {

        if (MoveList<LEGAL>(pos).size() == 0)
          return pos.checkers() ? VALUE_MATE : VALUE_DRAW;

        if (ply >= MAX_PLY - 2 || pos.is_draw(ply - 1))
          return VALUE_DRAW;

        return VALUE_NONE;
      }


      // evaluate_leaf() does AB search on the position to get its value

      Value evaluate_leaf(Position& pos, MCTSNode& node, Depth leafDepth) {

        assert(node.posKey == pos.key());
        assert(node.leafSearchDepth == DEPTH_NONE);
        assert(node.leafSearchEval == VALUE_NONE);

        debug << "Evaluating leaf: " << pos.fen() << endl;

        init_for_leaf_search(pos);

        Move pv[MAX_PLY + 1];
        stack[ply].pv = pv;
        stack[ply].currentMove = MOVE_NONE;
        stack[ply].excludedMove = MOVE_NONE;

        if (!node.is_root() && node.parent->leafSearchEval != VALUE_NONE)
        {
          // If we have some parent score then use an aspiration window.
          // We know what to expect.
          Value delta = Value(18);
          Value alpha = std::max(node.parent->leafSearchEval - delta, -VALUE_INFINITE);
          Value beta = std::min(node.parent->leafSearchEval + delta, VALUE_INFINITE);
          while (true)
          {
            const Value value = Stockfish::search<PV>(pos, stack + ply, alpha, beta, leafDepth, false);
            if (value <= alpha)
            {
              beta = (alpha + beta) / 2;
              alpha = std::max(value - delta, -VALUE_INFINITE);
            }
            else
            if (value >= beta)
              beta = std::min(value + delta, VALUE_INFINITE);
            else
              return value;

            delta += delta / 4 + 5;
          }
        }

        else
          // If no parent score then do infinite aspiration window.
          return Stockfish::search<PV>(pos, stack + ply, -VALUE_INFINITE, VALUE_INFINITE, leafDepth, false);
      }


      // get_pv(node) tries to get a pv, starting from the given node

      std::vector<Move> get_pv(const MCTSNode& node) const {
        std::vector<Move> pv;

        const MCTSNode* currentNode = &node;
        if (!currentNode->is_root())
          pv.emplace_back(currentNode->prevMove);

        while (!currentNode->is_leaf())
        {
          // No exploration factor for choosing the PV.
          const MCTSNode& bestChild = currentNode->get_best_child(0.0f);
          pv.emplace_back(bestChild.prevMove);
          currentNode = &bestChild;
        }

        return pv;
      }


      // get_pv() tries to get the pv, starting from the root

      std::vector<Move> get_pv() const {
        return get_pv(rootNode);
      }


      // do_playout() does a single playout and returns what is needed to backprop

      BackpropValues do_playout(Position& pos, MCTSNode& node, Depth leafDepth) {

        assert(node.posKey == pos.key());
        assert(node.numVisits == 0);
        assert(node.is_leaf());
        assert(node.numChildren == 0);
        assert(node.leafSearchDepth == DEPTH_NONE);
        assert(!node.isTerminal);

        debug << "Doing playout " << numPlayouts << ": " << pos.fen() << endl;

        numPlayouts += 1;

        const Value v = evaluate_leaf(pos, node, leafDepth);

        BackpropValues backprops{};
        backprops.numVisits = 1;        // playout counts as a visit
        backprops.actionValue += value_to_reward(v);
        backprops.actionValueWeight += normalWeight;

        // Bookkeeping for raw eval
        node.leafSearchEval = v;
        node.leafSearchDepth = leafDepth;

        // Local backprop because normal backprop handles only the
        // nodes starting from the parent of this one.
        node.numVisits          = backprops.numVisits;
        node.actionValue       += backprops.actionValue;
        node.actionValueWeight += backprops.actionValueWeight;

        return backprops;
      }


      // expand_node_and_do_playout() : expand a node and do at least one playout.
      // May do more "playouts" if there are terminals as those are "played out" immediately.
      // Returns what needs to be backpropagated.

      BackpropValues expand_node_and_do_playout( Position& pos,
                                                 MCTSNode& node,
                                                 Depth leafDepth,
                                                 float explorationFactor)
      {
        assert(node.posKey == pos.key());          // node must match the position
        assert(node.is_leaf());                    // otherwise already expanded
        assert(node.numChildren == 0);             // leafs have no children
        assert(!node.isTerminal);                  // terminals cannot be expanded
        assert(node.numVisits == 1);               // we expect it to have the "playout visit". Fake visit for the root.
        assert(node.leafSearchDepth != DEPTH_NONE);
        assert(node.leafSearchEval != VALUE_NONE);

        debug << "Expanding and playing out: " << pos.fen() << endl;

        Move moves[MAX_MOVES];
        const int moveCount = generate_moves_ordered(pos, node, leafDepth, moves);

        assert(moveCount > 0);

        node.children = std::make_unique<MCTSNode[]>(moveCount);
        node.numChildren = moveCount;

        int numTerminals = 0;
        BackpropValues backprops{};

        float prior = value_to_reward(node.leafSearchEval);

        // Note that prior is attenuated for later moves - we rely on move ordering.
        // Attenuate more at higher plies, where we have better move ordering.

        const float priorAttenuation = 1.0f - std::min((ply - 1) / 100.0f, 0.05f);
        for (int i = 0; i < moveCount; ++i)
        {
          // Setup the child
          MCTSNode& child = node.children[i];
          child.prevMove = moves[i];
          child.childId = i;
          child.parent = &node;

          debug << "Expanding move " << i+1 << " out of " << moveCount << ": " << pos.fen() << endl;

          // We enter the child's position
          do_move(pos, node, child);
          child.posKey = pos.key();

          const Value terminalValue = terminal_value(pos);
          if (terminalValue != VALUE_NONE)
          {
              // if it's a terminal then "play it out"
              child.isTerminal = true;
              child.prior = value_to_reward(terminalValue);
              child.numVisits = 1;
              child.actionValue = child.prior * terminalWeight;
              child.actionValueWeight = terminalWeight;
              child.leafSearchEval = terminalValue;
              child.leafSearchDepth = terminalEvalDepth;

              numTerminals += 1;
              numPlayouts += 1;
          }
          else
          {
              // Otherwise we just note the prior (policy)
              child.prior = 1.0f - prior;
              child.actionValue = child.prior * priorWeight;
              child.actionValueWeight = priorWeight;
          }

          undo_move(pos);

          // Accumulate the policies to backprop
          backprops.actionValue += child.actionValue;
          backprops.actionValueWeight += child.actionValueWeight;

          // Reduce the prior for the next move
          prior *= priorAttenuation;
        }

        if (numTerminals == 0)
        {
          // If no terminals then we do one playout on the best child
          MCTSNode& bestChild = node.get_best_child(explorationFactor);
          do_move(pos, node, bestChild);

          backprops.numVisits += 1;

          auto playoutBackprops = do_playout(pos, bestChild, leafDepth);
          backprops.numVisits += playoutBackprops.numVisits;
          backprops.actionValue += playoutBackprops.actionValue;
          backprops.actionValueWeight += playoutBackprops.actionValueWeight;

          undo_move(pos);
        }
        else
        {
          // If there are any terminals we don't do more playouts
          backprops.numVisits += numTerminals;
        }

        // Local backprop because normal backprop handles only the
        // nodes starting from the parent of this one
        backprops.flip_side();

        node.actionValue = backprops.actionValue;
        node.actionValueWeight = backprops.actionValueWeight;
        node.numVisits = backprops.numVisits;

        return backprops;
      }


      // do_move() does a move and updates the stack

      void do_move(Position& pos, MCTSNode& parentNode, MCTSNode& childNode) {

        assert(ply < MAX_PLY);
        assert(!parentNode.is_leaf());
        assert(&parentNode.children[childNode.childId] == &childNode);
        assert(parentNode.posKey == pos.key());

        const Move move = childNode.prevMove;

        stack[ply].currentMove = move;
        stack[ply].inCheck = pos.checkers();
        stack[ply].continuationHistory =
          &(
            pos.this_thread()->continuationHistory
              [stack[ply].inCheck]
              [pos.capture(move)]
              [pos.moved_piece(move)]
              [to_sq(move)]
          );
        stack[ply].staticEval = parentNode.leafSearchEval;
        stack[ply].moveCount = childNode.childId + 1;

        pos.do_move(move, states[ply]);

        // The first time around we don't have posKey set yet,
        // because we need to do the move first.
        assert(childNode.posKey == 0 || childNode.posKey == pos.key());

        ply += 1;

        if (ply > maximumPly)
          maximumPly = ply;
      }


      // undo_move() undoes a move and pops the stack

      void undo_move(Position& pos) {
        assert(ply > 1);

        ply -= 1;

        pos.undo_move(stack[ply].currentMove);
      }


      // create_new_root() inits a root from the given position

      void create_new_root(Position& pos) {
        rootNode = MCTSNode{};
        rootNode.posKey = pos.key();
        rootNode.isTerminal = MoveList<LEGAL>(pos).size() == 0;
      }


      void init_for_mcts_search(Position& pos) {
        std::memset(stack - 7, 0, 10 * sizeof(Stack));

        auto th = pos.this_thread();

        // stack + 0 also needs to be initialized because we start from ply = 1
        for (int i = 7; i >= 0; --i)
          (stack - i)->continuationHistory = &th->continuationHistory[0][0][NO_PIECE][0]; // Use as a sentinel

        for (int i = 1; i <= MAX_PLY; ++i)
          (stack + i)->ply = i;

        // Evaluation score is from the white point of view
        th->trend = make_score(0, 0);

        create_new_root(pos);

        ply = 1;
        maximumPly = ply;
        numPlayouts = 0;
      }
    };


    // search_mcts() : this is the main function of the MonteCarloTreeSearch class

    ValueAndPV search_mcts( Position& pos,
                            uint64_t numPlayouts,
                            Depth leafDepth,
                            float explorationFactor)
    {
      MonteCarloTreeSearch mcts{};
      return mcts.search_new(pos, numPlayouts, leafDepth, explorationFactor);
    }


    // search_mcts_multipv() : use this for multiPV

    std::vector<MctsContinuation> search_mcts_multipv( Position& pos,
                                                       uint64_t numPlayouts,
                                                       Depth leafDepth,
                                                       float explorationFactor)
    {
      MonteCarloTreeSearch mcts{};
      mcts.search_new(pos, numPlayouts, leafDepth, explorationFactor);

      return mcts.get_all_continuations();
    }
  }
}

} // namespace Stockfish
