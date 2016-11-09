/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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
#include "position.h"
#include "search.h"
#include "timeman.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

namespace Search {

  SignalsType Signals;
  LimitsType Limits;
}

namespace Tablebases {

  int Cardinality;
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

  // Different node types, used as a template parameter
  enum NodeType { NonPV, PV };

  // Razoring and futility margin based on depth
  const int razor_margin[4] = { 483, 570, 603, 554 };
  Value futility_margin(Depth d) { return Value(150 * d / ONE_PLY); }

  // Futility and reductions lookup tables, initialized at startup
  int FutilityMoveCounts[2][16]; // [improving][depth]
  int Reductions[2][2][64][64];  // [pv][improving][depth][moveNumber]

  template <bool PvNode> Depth reduction(bool i, Depth d, int mn) {
    return Reductions[PvNode][i][std::min(d / ONE_PLY, 63)][std::min(mn, 63)] * ONE_PLY;
  }

  // Skill structure is used to implement strength limit
  struct Skill {
    Skill(int l) : level(l) {}
    bool enabled() const { return level < 20; }
    bool time_to_pick(Depth depth) const { return depth / ONE_PLY == 1 + level; }
    Move best_move(size_t multiPV) { return best ? best : pick_best(multiPV); }
    Move pick_best(size_t multiPV);

    int level;
    Move best = MOVE_NONE;
  };

  // EasyMoveManager structure is used to detect an 'easy move'. When the PV is
  // stable across multiple search iterations, we can quickly return the best move.
  struct EasyMoveManager {

    void clear() {
      stableCnt = 0;
      expectedPosKey = 0;
      pv[0] = pv[1] = pv[2] = MOVE_NONE;
    }

    Move get(Key key) const {
      return expectedPosKey == key ? pv[2] : MOVE_NONE;
    }

    void update(Position& pos, const std::vector<Move>& newPv) {

      assert(newPv.size() >= 3);

      // Keep track of how many times in a row the 3rd ply remains stable
      stableCnt = (newPv[2] == pv[2]) ? stableCnt + 1 : 0;

      if (!std::equal(newPv.begin(), newPv.begin() + 3, pv))
      {
          std::copy(newPv.begin(), newPv.begin() + 3, pv);

          StateInfo st[2];
          pos.do_move(newPv[0], st[0], pos.gives_check(newPv[0]));
          pos.do_move(newPv[1], st[1], pos.gives_check(newPv[1]));
          expectedPosKey = pos.key();
          pos.undo_move(newPv[1]);
          pos.undo_move(newPv[0]);
      }
    }

    int stableCnt;
    Key expectedPosKey;
    Move pv[3];
  };

  // Set of rows with half bits set to 1 and half to 0. It is used to allocate
  // the search depths across the threads.
  typedef std::vector<int> Row;

  const Row HalfDensity[] = {
    {0, 1},
    {1, 0},
    {0, 0, 1, 1},
    {0, 1, 1, 0},
    {1, 1, 0, 0},
    {1, 0, 0, 1},
    {0, 0, 0, 1, 1, 1},
    {0, 0, 1, 1, 1, 0},
    {0, 1, 1, 1, 0, 0},
    {1, 1, 1, 0, 0, 0},
    {1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 1, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 1, 0 ,0},
    {0, 1, 1, 1, 1, 0, 0 ,0},
    {1, 1, 1, 1, 0, 0, 0 ,0},
    {1, 1, 1, 0, 0, 0, 0 ,1},
    {1, 1, 0, 0, 0, 0, 1 ,1},
    {1, 0, 0, 0, 0, 1, 1 ,1},
  };

  const size_t HalfDensitySize = std::extent<decltype(HalfDensity)>::value;

  EasyMoveManager EasyMove;
  Value DrawValue[COLOR_NB];

  template <NodeType NT>
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode);

  template <NodeType NT, bool InCheck>
  Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth);

  Value value_to_tt(Value v, int ply);
  Value value_from_tt(Value v, int ply);
  void update_pv(Move* pv, Move move, Move* childPv);
  void update_cm_stats(Stack* ss, Piece pc, Square s, Value bonus);
  void update_stats(const Position& pos, Stack* ss, Move move, Move* quiets, int quietsCnt, Value bonus);
  void check_time();

} // namespace


/// Search::init() is called during startup to initialize various lookup tables

void Search::init() {

  for (int imp = 0; imp <= 1; ++imp)
      for (int d = 1; d < 64; ++d)
          for (int mc = 1; mc < 64; ++mc)
          {
              double r = log(d) * log(mc) / 2;

              Reductions[NonPV][imp][d][mc] = int(std::round(r));
              Reductions[PV][imp][d][mc] = std::max(Reductions[NonPV][imp][d][mc] - 1, 0);

              // Increase reduction for non-PV nodes when eval is not improving
              if (!imp && Reductions[NonPV][imp][d][mc] >= 2)
                Reductions[NonPV][imp][d][mc]++;
          }

  for (int d = 0; d < 16; ++d)
  {
      FutilityMoveCounts[0][d] = int(2.4 + 0.773 * pow(d + 0.00, 1.8));
      FutilityMoveCounts[1][d] = int(2.9 + 1.045 * pow(d + 0.49, 1.8));
  }
}


/// Search::clear() resets search state to zero, to obtain reproducible results

void Search::clear() {

  TT.clear();

  for (Thread* th : Threads)
  {
      th->history.clear();
      th->counterMoves.clear();
      th->fromTo.clear();
      th->counterMoveHistory.clear();
      th->resetCalls = true;
  }

  Threads.main()->previousScore = VALUE_INFINITE;
}


/// Search::perft() is our utility to verify move generation. All the leaf nodes
/// up to the given depth are generated and counted, and the sum is returned.
template<bool Root>
uint64_t Search::perft(Position& pos, Depth depth) {

  StateInfo st;
  uint64_t cnt, nodes = 0;
  const bool leaf = (depth == 2 * ONE_PLY);

  for (const auto& m : MoveList<LEGAL>(pos))
  {
      if (Root && depth <= ONE_PLY)
          cnt = 1, nodes++;
      else
      {
          pos.do_move(m, st, pos.gives_check(m));
          cnt = leaf ? MoveList<LEGAL>(pos).size() : perft<false>(pos, depth - ONE_PLY);
          nodes += cnt;
          pos.undo_move(m);
      }
      if (Root)
          sync_cout << UCI::move(m, pos.is_chess960()) << ": " << cnt << sync_endl;
  }
  return nodes;
}

template uint64_t Search::perft<true>(Position&, Depth);


/// MainThread::search() is called by the main thread when the program receives
/// the UCI 'go' command. It searches from the root position and outputs the "bestmove".

void MainThread::search() {

  Color us = rootPos.side_to_move();
  Time.init(Limits, us, rootPos.game_ply());

  int contempt = Options["Contempt"] * PawnValueEg / 100; // From centipawns
  DrawValue[ us] = VALUE_DRAW - Value(contempt);
  DrawValue[~us] = VALUE_DRAW + Value(contempt);

  if (rootMoves.empty())
  {
      rootMoves.push_back(RootMove(MOVE_NONE));
      sync_cout << "info depth 0 score "
                << UCI::value(rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW)
                << sync_endl;
  }
  else
  {
      for (Thread* th : Threads)
          if (th != this)
              th->start_searching();

      Thread::search(); // Let's start searching!
  }

  // When playing in 'nodes as time' mode, subtract the searched nodes from
  // the available ones before exiting.
  if (Limits.npmsec)
      Time.availableNodes += Limits.inc[us] - Threads.nodes_searched();

  // When we reach the maximum depth, we can arrive here without a raise of
  // Signals.stop. However, if we are pondering or in an infinite search,
  // the UCI protocol states that we shouldn't print the best move before the
  // GUI sends a "stop" or "ponderhit" command. We therefore simply wait here
  // until the GUI sends one of those commands (which also raises Signals.stop).
  if (!Signals.stop && (Limits.ponder || Limits.infinite))
  {
      Signals.stopOnPonderhit = true;
      wait(Signals.stop);
  }

  // Stop the threads if not already stopped
  Signals.stop = true;

  // Wait until all threads have finished
  for (Thread* th : Threads)
      if (th != this)
          th->wait_for_search_finished();

  // Check if there are threads with a better score than main thread
  Thread* bestThread = this;
  if (   !this->easyMovePlayed
      &&  Options["MultiPV"] == 1
      && !Limits.depth
      && !Skill(Options["Skill Level"]).enabled()
      &&  rootMoves[0].pv[0] != MOVE_NONE)
  {
      for (Thread* th : Threads)
          if (   th->completedDepth > bestThread->completedDepth
              && th->rootMoves[0].score > bestThread->rootMoves[0].score)
              bestThread = th;
  }

  previousScore = bestThread->rootMoves[0].score;

  // Send new PV when needed
  if (bestThread != this)
      sync_cout << UCI::pv(bestThread->rootPos, bestThread->completedDepth, -VALUE_INFINITE, VALUE_INFINITE) << sync_endl;

  sync_cout << "bestmove " << UCI::move(bestThread->rootMoves[0].pv[0], rootPos.is_chess960());

  if (bestThread->rootMoves[0].pv.size() > 1 || bestThread->rootMoves[0].extract_ponder_from_tt(rootPos))
      std::cout << " ponder " << UCI::move(bestThread->rootMoves[0].pv[1], rootPos.is_chess960());

  std::cout << sync_endl;
}


// Thread::search() is the main iterative deepening loop. It calls search()
// repeatedly with increasing depth until the allocated thinking time has been
// consumed, the user stops the search, or the maximum search depth is reached.

void Thread::search() {

  Stack stack[MAX_PLY+7], *ss = stack+5; // To allow referencing (ss-5) and (ss+2)
  Value bestValue, alpha, beta, delta;
  Move easyMove = MOVE_NONE;
  MainThread* mainThread = (this == Threads.main() ? Threads.main() : nullptr);

  std::memset(ss-5, 0, 8 * sizeof(Stack));

  bestValue = delta = alpha = -VALUE_INFINITE;
  beta = VALUE_INFINITE;
  completedDepth = DEPTH_ZERO;

  if (mainThread)
  {
      easyMove = EasyMove.get(rootPos.key());
      EasyMove.clear();
      mainThread->easyMovePlayed = mainThread->failedLow = false;
      mainThread->bestMoveChanges = 0;
      TT.new_search();
  }

  size_t multiPV = Options["MultiPV"];
  Skill skill(Options["Skill Level"]);

  // When playing with strength handicap enable MultiPV search that we will
  // use behind the scenes to retrieve a set of possible moves.
  if (skill.enabled())
      multiPV = std::max(multiPV, (size_t)4);

  multiPV = std::min(multiPV, rootMoves.size());

  // Iterative deepening loop until requested to stop or the target depth is reached
  while (   (rootDepth += ONE_PLY) < DEPTH_MAX
         && !Signals.stop
         && (!Limits.depth || Threads.main()->rootDepth / ONE_PLY <= Limits.depth))
  {
      // Set up the new depths for the helper threads skipping on average every
      // 2nd ply (using a half-density matrix).
      if (!mainThread)
      {
          const Row& row = HalfDensity[(idx - 1) % HalfDensitySize];
          if (row[(rootDepth / ONE_PLY + rootPos.game_ply()) % row.size()])
             continue;
      }

      // Age out PV variability metric
      if (mainThread)
          mainThread->bestMoveChanges *= 0.505, mainThread->failedLow = false;

      // Save the last iteration's scores before first PV line is searched and
      // all the move scores except the (new) PV are set to -VALUE_INFINITE.
      for (RootMove& rm : rootMoves)
          rm.previousScore = rm.score;

      // MultiPV loop. We perform a full root search for each PV line
      for (PVIdx = 0; PVIdx < multiPV && !Signals.stop; ++PVIdx)
      {
          // Reset aspiration window starting size
          if (rootDepth >= 5 * ONE_PLY)
          {
              delta = Value(18);
              alpha = std::max(rootMoves[PVIdx].previousScore - delta,-VALUE_INFINITE);
              beta  = std::min(rootMoves[PVIdx].previousScore + delta, VALUE_INFINITE);
          }

          // Start with a small aspiration window and, in the case of a fail
          // high/low, re-search with a bigger window until we're not failing
          // high/low anymore.
          while (true)
          {
              bestValue = ::search<PV>(rootPos, ss, alpha, beta, rootDepth, false);

              // Bring the best move to the front. It is critical that sorting
              // is done with a stable algorithm because all the values but the
              // first and eventually the new best one are set to -VALUE_INFINITE
              // and we want to keep the same order for all the moves except the
              // new PV that goes to the front. Note that in case of MultiPV
              // search the already searched PV lines are preserved.
              std::stable_sort(rootMoves.begin() + PVIdx, rootMoves.end());

              // If search has been stopped, break immediately. Sorting and
              // writing PV back to TT is safe because RootMoves is still
              // valid, although it refers to the previous iteration.
              if (Signals.stop)
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

                  if (mainThread)
                  {
                      mainThread->failedLow = true;
                      Signals.stopOnPonderhit = false;
                  }
              }
              else if (bestValue >= beta)
              {
                  alpha = (alpha + beta) / 2;
                  beta = std::min(bestValue + delta, VALUE_INFINITE);
              }
              else
                  break;

              delta += delta / 4 + 5;

              assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
          }

          // Sort the PV lines searched so far and update the GUI
          std::stable_sort(rootMoves.begin(), rootMoves.begin() + PVIdx + 1);

          if (!mainThread)
              continue;

          if (Signals.stop || PVIdx + 1 == multiPV || Time.elapsed() > 3000)
              sync_cout << UCI::pv(rootPos, rootDepth, alpha, beta) << sync_endl;
      }

      if (!Signals.stop)
          completedDepth = rootDepth;

      if (!mainThread)
          continue;

      // If skill level is enabled and time is up, pick a sub-optimal best move
      if (skill.enabled() && skill.time_to_pick(rootDepth))
          skill.pick_best(multiPV);

      // Have we found a "mate in x"?
      if (   Limits.mate
          && bestValue >= VALUE_MATE_IN_MAX_PLY
          && VALUE_MATE - bestValue <= 2 * Limits.mate)
          Signals.stop = true;

      // Do we have time for the next iteration? Can we stop searching now?
      if (Limits.use_time_management())
      {
          if (!Signals.stop && !Signals.stopOnPonderhit)
          {
              // Stop the search if only one legal move is available, or if all
              // of the available time has been used, or if we matched an easyMove
              // from the previous search and just did a fast verification.
              const int F[] = { mainThread->failedLow,
                                bestValue - mainThread->previousScore };

              int improvingFactor = std::max(229, std::min(715, 357 + 119 * F[0] - 6 * F[1]));
              double unstablePvFactor = 1 + mainThread->bestMoveChanges;

              bool doEasyMove =   rootMoves[0].pv[0] == easyMove
                               && mainThread->bestMoveChanges < 0.03
                               && Time.elapsed() > Time.optimum() * 5 / 42;

              if (   rootMoves.size() == 1
                  || Time.elapsed() > Time.optimum() * unstablePvFactor * improvingFactor / 628
                  || (mainThread->easyMovePlayed = doEasyMove, doEasyMove))
              {
                  // If we are allowed to ponder do not stop the search now but
                  // keep pondering until the GUI sends "ponderhit" or "stop".
                  if (Limits.ponder)
                      Signals.stopOnPonderhit = true;
                  else
                      Signals.stop = true;
              }
          }

          if (rootMoves[0].pv.size() >= 3)
              EasyMove.update(rootPos, rootMoves[0].pv);
          else
              EasyMove.clear();
      }
  }

  if (!mainThread)
      return;

  // Clear any candidate easy move that wasn't stable for the last search
  // iterations; the second condition prevents consecutive fast moves.
  if (EasyMove.stableCnt < 6 || mainThread->easyMovePlayed)
      EasyMove.clear();

  // If skill level is enabled, swap best PV line with the sub-optimal one
  if (skill.enabled())
      std::swap(rootMoves[0], *std::find(rootMoves.begin(),
                rootMoves.end(), skill.best_move(multiPV)));
}


namespace {

  // search<>() is the main search function for both PV and non-PV nodes

  template <NodeType NT>
  Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    const bool PvNode = NT == PV;
    const bool rootNode = PvNode && (ss-1)->ply == 0;

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(DEPTH_ZERO < depth && depth < DEPTH_MAX);
    assert(!(PvNode && cutNode));
    assert(depth / ONE_PLY * ONE_PLY == depth);

    Move pv[MAX_PLY+1], quietsSearched[64];
    StateInfo st;
    TTEntry* tte;
    Key posKey;
    Move ttMove, move, excludedMove, bestMove;
    Depth extension, newDepth;
    Value bestValue, value, ttValue, eval, nullValue;
    bool ttHit, inCheck, givesCheck, singularExtensionNode, improving;
    bool captureOrPromotion, doFullDepthSearch, moveCountPruning;
    Piece moved_piece;
    int moveCount, quietCount;

    // Step 1. Initialize node
    Thread* thisThread = pos.this_thread();
    inCheck = pos.checkers();
    moveCount = quietCount =  ss->moveCount = 0;
    ss->history = VALUE_ZERO;
    bestValue = -VALUE_INFINITE;
    ss->ply = (ss-1)->ply + 1;

    // Check for the available remaining time
    if (thisThread->resetCalls.load(std::memory_order_relaxed))
    {
        thisThread->resetCalls = false;
        thisThread->callsCnt = 0;
    }
    if (++thisThread->callsCnt > 4096)
    {
        for (Thread* th : Threads)
            th->resetCalls = true;

        check_time();
    }

    // Used to send selDepth info to GUI
    if (PvNode && thisThread->maxPly < ss->ply)
        thisThread->maxPly = ss->ply;

    if (!rootNode)
    {
        // Step 2. Check for aborted search and immediate draw
        if (Signals.stop.load(std::memory_order_relaxed) || pos.is_draw() || ss->ply >= MAX_PLY)
            return ss->ply >= MAX_PLY && !inCheck ? evaluate(pos)
                                                  : DrawValue[pos.side_to_move()];

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

    ss->currentMove = (ss+1)->excludedMove = bestMove = MOVE_NONE;
    ss->counterMoves = nullptr;
    (ss+1)->skipEarlyPruning = false;
    (ss+2)->killers[0] = (ss+2)->killers[1] = MOVE_NONE;

    // Step 4. Transposition table lookup. We don't want the score of a partial
    // search to overwrite a previous full search TT value, so we use a different
    // position key in case of an excluded move.
    excludedMove = ss->excludedMove;
    posKey = pos.key() ^ Key(excludedMove);
    tte = TT.probe(posKey, ttHit);
    ttValue = ttHit ? value_from_tt(tte->value(), ss->ply) : VALUE_NONE;
    ttMove =  rootNode ? thisThread->rootMoves[thisThread->PVIdx].pv[0]
            : ttHit    ? tte->move() : MOVE_NONE;

    // At non-PV nodes we check for an early TT cutoff
    if (  !PvNode
        && ttHit
        && tte->depth() >= depth
        && ttValue != VALUE_NONE // Possible in case of TT access race
        && (ttValue >= beta ? (tte->bound() & BOUND_LOWER)
                            : (tte->bound() & BOUND_UPPER)))
    {
        // If ttMove is quiet, update killers, history, counter move on TT hit
        if (ttValue >= beta && ttMove)
        {
            int d = depth / ONE_PLY;

            if (!pos.capture_or_promotion(ttMove))
            {
                Value bonus = Value(d * d + 2 * d - 2);
                update_stats(pos, ss, ttMove, nullptr, 0, bonus);
            }

            // Extra penalty for a quiet TT move in previous ply when it gets refuted
            if ((ss-1)->moveCount == 1 && !pos.captured_piece())
            {
                Value penalty = Value(d * d + 4 * d + 1);
                Square prevSq = to_sq((ss-1)->currentMove);
                update_cm_stats(ss-1, pos.piece_on(prevSq), prevSq, -penalty);
            }
        }
        return ttValue;
    }

    // Step 4a. Tablebase probe
    if (!rootNode && TB::Cardinality)
    {
        int piecesCnt = pos.count<ALL_PIECES>(WHITE) + pos.count<ALL_PIECES>(BLACK);

        if (    piecesCnt <= TB::Cardinality
            && (piecesCnt <  TB::Cardinality || depth >= TB::ProbeDepth)
            &&  pos.rule50_count() == 0
            && !pos.can_castle(ANY_CASTLING))
        {
            TB::ProbeState err;
            TB::WDLScore v = Tablebases::probe_wdl(pos, &err);

            if (err != TB::ProbeState::FAIL)
            {
                thisThread->tbHits++;

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

    // Step 5. Evaluate the position statically
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
        (ss-1)->currentMove != MOVE_NULL ? evaluate(pos)
                                         : -(ss-1)->staticEval + 2 * Eval::Tempo;

        tte->save(posKey, VALUE_NONE, BOUND_NONE, DEPTH_NONE, MOVE_NONE,
                  ss->staticEval, TT.generation());
    }

    if (ss->skipEarlyPruning)
        goto moves_loop;

    // Step 6. Razoring (skipped when in check)
    if (   !PvNode
        &&  depth < 4 * ONE_PLY
        &&  ttMove == MOVE_NONE
        &&  eval + razor_margin[depth / ONE_PLY] <= alpha)
    {
        if (depth <= ONE_PLY)
            return qsearch<NonPV, false>(pos, ss, alpha, beta, DEPTH_ZERO);

        Value ralpha = alpha - razor_margin[depth / ONE_PLY];
        Value v = qsearch<NonPV, false>(pos, ss, ralpha, ralpha+1, DEPTH_ZERO);
        if (v <= ralpha)
            return v;
    }

    // Step 7. Futility pruning: child node (skipped when in check)
    if (   !rootNode
        &&  depth < 7 * ONE_PLY
        &&  eval - futility_margin(depth) >= beta
        &&  eval < VALUE_KNOWN_WIN  // Do not return unproven wins
        &&  pos.non_pawn_material(pos.side_to_move()))
        return eval;

    // Step 8. Null move search with verification search (is omitted in PV nodes)
    if (   !PvNode
        &&  eval >= beta
        && (ss->staticEval >= beta - 35 * (depth / ONE_PLY - 6) || depth >= 13 * ONE_PLY)
        &&  pos.non_pawn_material(pos.side_to_move()))
    {
        ss->currentMove = MOVE_NULL;
        ss->counterMoves = nullptr;

        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth and value
        Depth R = ((823 + 67 * depth / ONE_PLY) / 256 + std::min((eval - beta) / PawnValueMg, 3)) * ONE_PLY;

        pos.do_null_move(st);
        (ss+1)->skipEarlyPruning = true;
        nullValue = depth-R < ONE_PLY ? -qsearch<NonPV, false>(pos, ss+1, -beta, -beta+1, DEPTH_ZERO)
                                      : - search<NonPV>(pos, ss+1, -beta, -beta+1, depth-R, !cutNode);
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
                                        :  search<NonPV>(pos, ss, beta-1, beta, depth-R, false);
            ss->skipEarlyPruning = false;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 9. ProbCut (skipped when in check)
    // If we have a good enough capture and a reduced search returns a value
    // much above beta, we can (almost) safely prune the previous move.
    if (   !PvNode
        &&  depth >= 5 * ONE_PLY
        &&  abs(beta) < VALUE_MATE_IN_MAX_PLY)
    {
        Value rbeta = std::min(beta + 200, VALUE_INFINITE);
        Depth rdepth = depth - 4 * ONE_PLY;

        assert(rdepth >= ONE_PLY);
        assert((ss-1)->currentMove != MOVE_NONE);
        assert((ss-1)->currentMove != MOVE_NULL);

        MovePicker mp(pos, ttMove, rbeta - ss->staticEval);

        while ((move = mp.next_move()) != MOVE_NONE)
            if (pos.legal(move))
            {
                ss->currentMove = move;
                ss->counterMoves = &thisThread->counterMoveHistory[pos.moved_piece(move)][to_sq(move)];
                pos.do_move(move, st, pos.gives_check(move));
                value = -search<NonPV>(pos, ss+1, -rbeta, -rbeta+1, rdepth, !cutNode);
                pos.undo_move(move);
                if (value >= rbeta)
                    return value;
            }
    }

    // Step 10. Internal iterative deepening (skipped when in check)
    if (    depth >= 6 * ONE_PLY
        && !ttMove
        && (PvNode || ss->staticEval + 256 >= beta))
    {
        Depth d = (3 * depth / (4 * ONE_PLY) - 2) * ONE_PLY;
        ss->skipEarlyPruning = true;
        search<NT>(pos, ss, alpha, beta, d, cutNode);
        ss->skipEarlyPruning = false;

        tte = TT.probe(posKey, ttHit);
        ttMove = ttHit ? tte->move() : MOVE_NONE;
    }

moves_loop: // When in check search starts from here

    const CounterMoveStats* cmh  = (ss-1)->counterMoves;
    const CounterMoveStats* fmh  = (ss-2)->counterMoves;
    const CounterMoveStats* fmh2 = (ss-4)->counterMoves;

    MovePicker mp(pos, ttMove, depth, ss);
    value = bestValue; // Workaround a bogus 'uninitialized' warning under gcc
    improving =   ss->staticEval >= (ss-2)->staticEval
            /* || ss->staticEval == VALUE_NONE Already implicit in the previous condition */
               ||(ss-2)->staticEval == VALUE_NONE;

    singularExtensionNode =   !rootNode
                           &&  depth >= 8 * ONE_PLY
                           &&  ttMove != MOVE_NONE
                           &&  ttValue != VALUE_NONE
                           && !excludedMove // Recursive singular search is not allowed
                           && (tte->bound() & BOUND_LOWER)
                           &&  tte->depth() >= depth - 3 * ONE_PLY;

    // Step 11. Loop through moves
    // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move()) != MOVE_NONE)
    {
      assert(is_ok(move));

      if (move == excludedMove)
          continue;

      // At root obey the "searchmoves" option and skip moves not listed in Root
      // Move List. As a consequence any illegal move is also skipped. In MultiPV
      // mode we also skip PV moves which have been already searched.
      if (rootNode && !std::count(thisThread->rootMoves.begin() + thisThread->PVIdx,
                                  thisThread->rootMoves.end(), move))
          continue;

      ss->moveCount = ++moveCount;

      if (rootNode && thisThread == Threads.main() && Time.elapsed() > 3000)
          sync_cout << "info depth " << depth / ONE_PLY
                    << " currmove " << UCI::move(move, pos.is_chess960())
                    << " currmovenumber " << moveCount + thisThread->PVIdx << sync_endl;

      if (PvNode)
          (ss+1)->pv = nullptr;

      extension = DEPTH_ZERO;
      captureOrPromotion = pos.capture_or_promotion(move);
      moved_piece = pos.moved_piece(move);

      givesCheck =  type_of(move) == NORMAL && !pos.discovered_check_candidates()
                  ? pos.check_squares(type_of(pos.piece_on(from_sq(move)))) & to_sq(move)
                  : pos.gives_check(move);

      moveCountPruning =   depth < 16 * ONE_PLY
                        && moveCount >= FutilityMoveCounts[improving][depth / ONE_PLY];

      // Step 12. Extend checks
      if (    givesCheck
          && !moveCountPruning
          &&  pos.see_ge(move, VALUE_ZERO))
          extension = ONE_PLY;

      // Singular extension search. If all moves but one fail low on a search of
      // (alpha-s, beta-s), and just one fails high on (alpha, beta), then that move
      // is singular and should be extended. To verify this we do a reduced search
      // on all the other moves but the ttMove and if the result is lower than
      // ttValue minus a margin then we extend the ttMove.
      if (    singularExtensionNode
          &&  move == ttMove
          && !extension
          &&  pos.legal(move))
      {
          Value rBeta = std::max(ttValue - 2 * depth / ONE_PLY, -VALUE_MATE);
          Depth d = (depth / (2 * ONE_PLY)) * ONE_PLY;
          ss->excludedMove = move;
          ss->skipEarlyPruning = true;
          value = search<NonPV>(pos, ss, rBeta - 1, rBeta, d, cutNode);
          ss->skipEarlyPruning = false;
          ss->excludedMove = MOVE_NONE;

          if (value < rBeta)
              extension = ONE_PLY;
      }

      // Update the current move (this must be done after singular extension search)
      newDepth = depth - ONE_PLY + extension;

      // Step 13. Pruning at shallow depth
      if (  !rootNode
          && bestValue > VALUE_MATED_IN_MAX_PLY)
      {
          if (   !captureOrPromotion
              && !givesCheck
              && !pos.advanced_pawn_push(move))
          {
              // Move count based pruning
              if (moveCountPruning)
                  continue;

              // Reduced depth of the next LMR search
              int lmrDepth = std::max(newDepth - reduction<PvNode>(improving, depth, moveCount), DEPTH_ZERO) / ONE_PLY;

              // Countermoves based pruning
              if (   lmrDepth < 3
                  && (!cmh  || (*cmh )[moved_piece][to_sq(move)] < VALUE_ZERO)
                  && (!fmh  || (*fmh )[moved_piece][to_sq(move)] < VALUE_ZERO)
                  && (!fmh2 || (*fmh2)[moved_piece][to_sq(move)] < VALUE_ZERO || (cmh && fmh)))
                  continue;

              // Futility pruning: parent node
              if (   lmrDepth < 7
                  && !inCheck
                  && ss->staticEval + 256 + 200 * lmrDepth <= alpha)
                  continue;

              // Prune moves with negative SEE
              if (   lmrDepth < 8
                  && !pos.see_ge(move, Value(-35 * lmrDepth * lmrDepth)))
                  continue;
          }
          else if (   depth < 7 * ONE_PLY
                   && !extension
                   && !pos.see_ge(move, Value(-35 * depth / ONE_PLY * depth / ONE_PLY)))
                  continue;
      }

      // Speculative prefetch as early as possible
      prefetch(TT.first_entry(pos.key_after(move)));

      // Check for legality just before making the move
      if (!rootNode && !pos.legal(move))
      {
          ss->moveCount = --moveCount;
          continue;
      }

      ss->currentMove = move;
      ss->counterMoves = &thisThread->counterMoveHistory[moved_piece][to_sq(move)];

      // Step 14. Make the move
      pos.do_move(move, st, givesCheck);

      // Step 15. Reduced depth search (LMR). If the move fails high it will be
      // re-searched at full depth.
      if (    depth >= 3 * ONE_PLY
          &&  moveCount > 1
          && (!captureOrPromotion || moveCountPruning))
      {
          Depth r = reduction<PvNode>(improving, depth, moveCount);

          if (captureOrPromotion)
              r -= r ? ONE_PLY : DEPTH_ZERO;
          else
          {
              // Increase reduction for cut nodes
              if (cutNode)
                  r += 2 * ONE_PLY;

              // Decrease reduction for moves that escape a capture. Filter out
              // castling moves, because they are coded as "king captures rook" and
              // hence break make_move(). Also use see() instead of see_sign(),
              // because the destination square is empty.
              else if (   type_of(move) == NORMAL
                       && type_of(pos.piece_on(to_sq(move))) != PAWN
                       && !pos.see_ge(make_move(to_sq(move), from_sq(move)),  VALUE_ZERO))
                  r -= 2 * ONE_PLY;

              ss->history = thisThread->history[moved_piece][to_sq(move)]
                           +    (cmh  ? (*cmh )[moved_piece][to_sq(move)] : VALUE_ZERO)
                           +    (fmh  ? (*fmh )[moved_piece][to_sq(move)] : VALUE_ZERO)
                           +    (fmh2 ? (*fmh2)[moved_piece][to_sq(move)] : VALUE_ZERO)
                           +    thisThread->fromTo.get(~pos.side_to_move(), move)
                           -    8000; // Correction factor

              // Decrease/increase reduction by comparing opponent's stat score
              if (ss->history > VALUE_ZERO && (ss-1)->history < VALUE_ZERO)
                  r -= ONE_PLY;

              else if (ss->history < VALUE_ZERO && (ss-1)->history > VALUE_ZERO)
                  r += ONE_PLY;

              // Decrease/increase reduction for moves with a good/bad history
              r = std::max(DEPTH_ZERO, (r / ONE_PLY - ss->history / 20000) * ONE_PLY);
          }

          Depth d = std::max(newDepth - r, ONE_PLY);

          value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, d, true);

          doFullDepthSearch = (value > alpha && d != newDepth);
      }
      else
          doFullDepthSearch = !PvNode || moveCount > 1;

      // Step 16. Full depth search when LMR is skipped or fails high
      if (doFullDepthSearch)
          value = newDepth <   ONE_PLY ?
                            givesCheck ? -qsearch<NonPV,  true>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
                                       : -qsearch<NonPV, false>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
                                       : - search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth, !cutNode);

      // For PV nodes only, do a full PV search on the first move or after a fail
      // high (in the latter case search only if value < beta), otherwise let the
      // parent node fail low with value <= alpha and try another move.
      if (PvNode && (moveCount == 1 || (value > alpha && (rootNode || value < beta))))
      {
          (ss+1)->pv = pv;
          (ss+1)->pv[0] = MOVE_NONE;

          value = newDepth <   ONE_PLY ?
                            givesCheck ? -qsearch<PV,  true>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                       : -qsearch<PV, false>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                                       : - search<PV>(pos, ss+1, -beta, -alpha, newDepth, false);
      }

      // Step 17. Undo move
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Step 18. Check for a new best move
      // Finished searching the move. If a stop occurred, the return value of
      // the search cannot be trusted, and we return immediately without
      // updating best move, PV and TT.
      if (Signals.stop.load(std::memory_order_relaxed))
          return VALUE_ZERO;

      if (rootNode)
      {
          RootMove& rm = *std::find(thisThread->rootMoves.begin(),
                                    thisThread->rootMoves.end(), move);

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
              if (moveCount > 1 && thisThread == Threads.main())
                  ++static_cast<MainThread*>(thisThread)->bestMoveChanges;
          }
          else
              // All other moves but the PV are set to the lowest value: this is
              // not a problem when sorting because the sort is stable and the
              // move position in the list is preserved - just the PV is pushed up.
              rm.score = -VALUE_INFINITE;
      }

      if (value > bestValue)
      {
          bestValue = value;

          if (value > alpha)
          {
              // If there is an easy move for this position, clear it if unstable
              if (    PvNode
                  &&  thisThread == Threads.main()
                  &&  EasyMove.get(pos.key())
                  && (move != EasyMove.get(pos.key()) || moveCount > 1))
                  EasyMove.clear();

              bestMove = move;

              if (PvNode && !rootNode) // Update pv even in fail-high case
                  update_pv(ss->pv, move, (ss+1)->pv);

              if (PvNode && value < beta) // Update alpha! Always alpha < beta
                  alpha = value;
              else
              {
                  assert(value >= beta); // Fail high
                  break;
              }
          }
      }

      if (!captureOrPromotion && move != bestMove && quietCount < 64)
          quietsSearched[quietCount++] = move;
    }

    // The following condition would detect a stop only after move loop has been
    // completed. But in this case bestValue is valid because we have fully
    // searched our subtree, and we can anyhow save the result in TT.
    /*
       if (Signals.stop)
        return VALUE_DRAW;
    */

    // Step 20. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it
    // must be a mate or a stalemate. If we are in a singular extension search then
    // return a fail low score.

    assert(moveCount || !inCheck || excludedMove || !MoveList<LEGAL>(pos).size());

    if (!moveCount)
        bestValue = excludedMove ? alpha
                   :     inCheck ? mated_in(ss->ply) : DrawValue[pos.side_to_move()];
    else if (bestMove)
    {
        int d = depth / ONE_PLY;

        // Quiet best move: update killers, history and countermoves
        if (!pos.capture_or_promotion(bestMove))
        {
            Value bonus = Value(d * d + 2 * d - 2);
            update_stats(pos, ss, bestMove, quietsSearched, quietCount, bonus);
        }

        // Extra penalty for a quiet TT move in previous ply when it gets refuted
        if ((ss-1)->moveCount == 1 && !pos.captured_piece())
        {
            Value penalty = Value(d * d + 4 * d + 1);
            Square prevSq = to_sq((ss-1)->currentMove);
            update_cm_stats(ss-1, pos.piece_on(prevSq), prevSq, -penalty);
        }
    }
    // Bonus for prior countermove that caused the fail low
    else if (    depth >= 3 * ONE_PLY
             && !pos.captured_piece()
             && is_ok((ss-1)->currentMove))
    {
        int d = depth / ONE_PLY;
        Value bonus = Value(d * d + 2 * d - 2);
        Square prevSq = to_sq((ss-1)->currentMove);
        update_cm_stats(ss-1, pos.piece_on(prevSq), prevSq, bonus);
    }

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

    assert(InCheck == !!pos.checkers());
    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(depth <= DEPTH_ZERO);
    assert(depth / ONE_PLY * ONE_PLY == depth);

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
        return ss->ply >= MAX_PLY && !InCheck ? evaluate(pos)
                                              : DrawValue[pos.side_to_move()];

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
        return ttValue;

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
            (ss-1)->currentMove != MOVE_NULL ? evaluate(pos)
                                             : -(ss-1)->staticEval + 2 * Eval::Tempo;

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
    MovePicker mp(pos, ttMove, depth, to_sq((ss-1)->currentMove));

    // Loop through the moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move()) != MOVE_NONE)
    {
      assert(is_ok(move));

      givesCheck =  type_of(move) == NORMAL && !pos.discovered_check_candidates()
                  ? pos.check_squares(type_of(pos.piece_on(from_sq(move)))) & to_sq(move)
                  : pos.gives_check(move);

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

          if (futilityBase <= alpha && !pos.see_ge(move, VALUE_ZERO + 1))
          {
              bestValue = std::max(bestValue, futilityBase);
              continue;
          }
      }

      // Detect non-capture evasions that are candidates to be pruned
      evasionPrunable =    InCheck
                       &&  bestValue > VALUE_MATED_IN_MAX_PLY
                       && !pos.capture(move);

      // Don't search moves with negative SEE values
      if (  (!InCheck || evasionPrunable)
          &&  type_of(move) != PROMOTION
          &&  !pos.see_ge(move, VALUE_ZERO))
          continue;

      // Speculative prefetch as early as possible
      prefetch(TT.first_entry(pos.key_after(move)));

      // Check for legality just before making the move
      if (!pos.legal(move))
          continue;

      ss->currentMove = move;

      // Make and search the move
      pos.do_move(move, st, givesCheck);
      value = givesCheck ? -qsearch<NT,  true>(pos, ss+1, -beta, -alpha, depth - ONE_PLY)
                         : -qsearch<NT, false>(pos, ss+1, -beta, -alpha, depth - ONE_PLY);
      pos.undo_move(move);

      assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

      // Check for a new best move
      if (value > bestValue)
      {
          bestValue = value;

          if (value > alpha)
          {
              if (PvNode) // Update pv even in fail-high case
                  update_pv(ss->pv, move, (ss+1)->pv);

              if (PvNode && value < beta) // Update alpha here!
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


  // update_cm_stats() updates countermove and follow-up move history

  void update_cm_stats(Stack* ss, Piece pc, Square s, Value bonus) {

    CounterMoveStats* cmh  = (ss-1)->counterMoves;
    CounterMoveStats* fmh1 = (ss-2)->counterMoves;
    CounterMoveStats* fmh2 = (ss-4)->counterMoves;

    if (cmh)
        cmh->update(pc, s, bonus);

    if (fmh1)
        fmh1->update(pc, s, bonus);

    if (fmh2)
        fmh2->update(pc, s, bonus);
  }


  // update_stats() updates killers, history, countermove and countermove plus
  // follow-up move history when a new quiet best move is found.

  void update_stats(const Position& pos, Stack* ss, Move move,
                    Move* quiets, int quietsCnt, Value bonus) {

    if (ss->killers[0] != move)
    {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = move;
    }

    Color c = pos.side_to_move();
    Thread* thisThread = pos.this_thread();
    thisThread->fromTo.update(c, move, bonus);
    thisThread->history.update(pos.moved_piece(move), to_sq(move), bonus);
    update_cm_stats(ss, pos.moved_piece(move), to_sq(move), bonus);

    if ((ss-1)->counterMoves)
    {
        Square prevSq = to_sq((ss-1)->currentMove);
        thisThread->counterMoves.update(pos.piece_on(prevSq), prevSq, move);
    }

    // Decrease all the other played quiet moves
    for (int i = 0; i < quietsCnt; ++i)
    {
        thisThread->fromTo.update(c, quiets[i], -bonus);
        thisThread->history.update(pos.moved_piece(quiets[i]), to_sq(quiets[i]), -bonus);
        update_cm_stats(ss, pos.moved_piece(quiets[i]), to_sq(quiets[i]), -bonus);
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
    int weakness = 120 - 2 * level;
    int maxScore = -VALUE_INFINITE;

    // Choose best move. For each move score we add two terms, both dependent on
    // weakness. One is deterministic and bigger for weaker levels, and one is
    // random. Then we choose the move with the resulting highest score.
    for (size_t i = 0; i < multiPV; ++i)
    {
        // This is our magic formula
        int push = (  weakness * int(topScore - rootMoves[i].score)
                    + delta * (rng.rand<unsigned>() % weakness)) / 128;

        if (rootMoves[i].score + push > maxScore)
        {
            maxScore = rootMoves[i].score + push;
            best = rootMoves[i].pv[0];
        }
    }

    return best;
  }


  // check_time() is used to print debug info and, more importantly, to detect
  // when we are out of available time and thus stop the search.

  void check_time() {

    static TimePoint lastInfoTime = now();

    int elapsed = Time.elapsed();
    TimePoint tick = Limits.startTime + elapsed;

    if (tick - lastInfoTime >= 1000)
    {
        lastInfoTime = tick;
        dbg_print();
    }

    // An engine may not stop pondering until told so by the GUI
    if (Limits.ponder)
        return;

    if (   (Limits.use_time_management() && elapsed > Time.maximum() - 10)
        || (Limits.movetime && elapsed >= Limits.movetime)
        || (Limits.nodes && Threads.nodes_searched() >= (uint64_t)Limits.nodes))
            Signals.stop = true;
  }

} // namespace


/// UCI::pv() formats PV information according to the UCI protocol. UCI requires
/// that all (if any) unsearched PV lines are sent using a previous search score.

string UCI::pv(const Position& pos, Depth depth, Value alpha, Value beta) {

  std::stringstream ss;
  int elapsed = Time.elapsed() + 1;
  const RootMoves& rootMoves = pos.this_thread()->rootMoves;
  size_t PVIdx = pos.this_thread()->PVIdx;
  size_t multiPV = std::min((size_t)Options["MultiPV"], rootMoves.size());
  uint64_t nodesSearched = Threads.nodes_searched();
  uint64_t tbHits = Threads.tb_hits() + (TB::RootInTB ? rootMoves.size() : 0);

  for (size_t i = 0; i < multiPV; ++i)
  {
      bool updated = (i <= PVIdx);

      if (depth == ONE_PLY && !updated)
          continue;

      Depth d = updated ? depth : depth - ONE_PLY;
      Value v = updated ? rootMoves[i].score : rootMoves[i].previousScore;

      bool tb = TB::RootInTB && abs(v) < VALUE_MATE - MAX_PLY;
      v = tb ? TB::Score : v;

      if (ss.rdbuf()->in_avail()) // Not at first line
          ss << "\n";

      ss << "info"
         << " depth "    << d / ONE_PLY
         << " seldepth " << pos.this_thread()->maxPly
         << " multipv "  << i + 1
         << " score "    << UCI::value(v);

      if (!tb && i == PVIdx)
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
    bool ttHit;

    assert(pv.size() == 1);

    if (!pv[0])
        return false;

    pos.do_move(pv[0], st, pos.gives_check(pv[0]));
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

void Tablebases::filter_root_moves(Position& pos, Search::RootMoves& rootMoves) {

    RootInTB = false;
    UseRule50 = Options["Syzygy50MoveRule"];
    ProbeDepth = Options["SyzygyProbeDepth"] * ONE_PLY;
    Cardinality = Options["SyzygyProbeLimit"];

    // Skip TB probing when no TB found: !TBLargest -> !TB::Cardinality
    if (Cardinality > MaxCardinality)
    {
        Cardinality = MaxCardinality;
        ProbeDepth = DEPTH_ZERO;
    }

    if (Cardinality < popcount(pos.pieces()) || pos.can_castle(ANY_CASTLING))
        return;

    // If the current root position is in the tablebases, then RootMoves
    // contains only moves that preserve the draw or the win.
    RootInTB = root_probe(pos, rootMoves, TB::Score);

    if (RootInTB)
        Cardinality = 0; // Do not probe tablebases during the search

    else // If DTZ tables are missing, use WDL tables as a fallback
    {
        // Filter out moves that do not preserve the draw or the win.
        RootInTB = root_probe_wdl(pos, rootMoves, TB::Score);

        // Only probe during search if winning
        if (RootInTB && TB::Score <= VALUE_DRAW)
            Cardinality = 0;
    }

    if (RootInTB && !UseRule50)
        TB::Score =  TB::Score > VALUE_DRAW ?  VALUE_MATE - MAX_PLY - 1
                   : TB::Score < VALUE_DRAW ? -VALUE_MATE + MAX_PLY + 1
                                            :  VALUE_DRAW;
}
