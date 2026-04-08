/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#include "search.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <list>
#include <ratio>
#include <string>
#include <utility>

#include "bitboard.h"
#include "evaluate.h"
#include "history.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include "position.h"
#include "syzygy/tbprobe.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"
#include "uci.h"
#include "ucioption.h"

namespace Stockfish {

namespace TB = Tablebases;

void syzygy_extend_pv(const OptionsMap&            options,
                      const Search::LimitsType&    limits,
                      Stockfish::Position&         pos,
                      Stockfish::Search::RootMove& rootMove,
                      Value&                       v);

using namespace Search;

namespace {

constexpr int SEARCHEDLIST_CAPACITY = 32;
using SearchedList                  = ValueList<Move, SEARCHEDLIST_CAPACITY>;

// (*Scalers):
// The values with Scaler asterisks have proven non-linear scaling.
// They are optimized to time controls of 180 + 1.8 and longer,
// so changing them or adding conditions that are similar requires
// tests at these types of time controls.

// (*Scaler) All tuned parameters at time controls shorter than
// optimized for require verifications at longer time controls

int correction_value(const Worker& w, const Position& pos, const Stack* const ss) {
    const Color us     = pos.side_to_move();
    const auto  m      = (ss - 1)->currentMove;
    const auto& shared = w.sharedHistory;
    const int   pcv    = shared.pawn_correction_entry(pos).at(us).pawn;
    const int   micv   = shared.minor_piece_correction_entry(pos).at(us).minor;
    const int   wnpcv  = shared.nonpawn_correction_entry<WHITE>(pos).at(us).nonPawnWhite;
    const int   bnpcv  = shared.nonpawn_correction_entry<BLACK>(pos).at(us).nonPawnBlack;
    const int   cntcv =
      m.is_ok() ? (*(ss - 2)->continuationCorrectionHistory)[pos.piece_on(m.to_sq())][m.to_sq()]
                    + (*(ss - 4)->continuationCorrectionHistory)[pos.piece_on(m.to_sq())][m.to_sq()]
                  : 8;

    return 12153 * pcv + 8620 * micv + 12355 * (wnpcv + bnpcv) + 7982 * cntcv;
}

// Add correctionHistory value to raw staticEval and guarantee evaluation
// does not hit the tablebase range.
Value to_corrected_static_eval(const Value v, const int cv) {
    return std::clamp(v + cv / 131072, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}

void update_correction_history(const Position& pos,
                               Stack* const    ss,
                               Search::Worker& workerThread,
                               const int       bonus) {
    const Move  m  = (ss - 1)->currentMove;
    const Color us = pos.side_to_move();

    constexpr int nonPawnWeight = 187;
    auto&         shared        = workerThread.sharedHistory;

    shared.pawn_correction_entry(pos).at(us).pawn << bonus;
    shared.minor_piece_correction_entry(pos).at(us).minor << bonus * 153 / 128;
    shared.nonpawn_correction_entry<WHITE>(pos).at(us).nonPawnWhite << bonus * nonPawnWeight / 128;
    shared.nonpawn_correction_entry<BLACK>(pos).at(us).nonPawnBlack << bonus * nonPawnWeight / 128;

    // Branchless: use mask to zero bonus when move is not ok
    const int    mask   = int(m.is_ok());
    const Square to     = m.to_sq_unchecked();
    const Piece  pc     = pos.piece_on(to);
    const int    bonus2 = (bonus * 126 / 128) * mask;
    const int    bonus4 = (bonus * 63 / 128) * mask;
    (*(ss - 2)->continuationCorrectionHistory)[pc][to] << bonus2;
    (*(ss - 4)->continuationCorrectionHistory)[pc][to] << bonus4;
}

// Add a small random component to draw evaluations to avoid 3-fold blindness
Value value_draw(size_t nodes) { return VALUE_DRAW - 1 + Value(nodes & 0x2); }
Value value_to_tt(Value v, int ply);
Value value_from_tt(Value v, int ply, int r50c);
void  update_pv(Move* pv, Move move, const Move* childPv);
void  update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus);
void  update_quiet_histories(
   const Position& pos, Stack* ss, Search::Worker& workerThread, Move move, int bonus);
void update_all_stats(const Position& pos,
                      Stack*          ss,
                      Search::Worker& workerThread,
                      Move            bestMove,
                      Square          prevSq,
                      SearchedList&   quietsSearched,
                      SearchedList&   capturesSearched,
                      Depth           depth,
                      Move            ttMove);

bool is_shuffling(Move move, Stack* const ss, const Position& pos) {
    if (pos.capture_stage(move) || pos.rule50_count() < 11)
        return false;
    if (pos.state()->pliesFromNull <= 6 || ss->ply < 18)
        return false;
    return move.from_sq() == (ss - 2)->currentMove.to_sq()
        && (ss - 2)->currentMove.from_sq() == (ss - 4)->currentMove.to_sq();
}

// Check if position is a simple endgame with high rule50 count requiring special handling
bool is_critical_endgame(const Position& pos) {
    // Only apply to positions with 4 or fewer pieces (excluding kings)
    if (popcount(pos.pieces()) > 4)
        return false;
    
    // Only apply when approaching 50-move rule
    if (pos.rule50_count() < 90)
        return false;
    
    return true;
}

}  // namespace

Search::Worker::Worker(SharedState&                    sharedState,
                       std::unique_ptr<ISearchManager> sm,
                       size_t                          threadId,
                       size_t                          numaThreadId,
                       size_t                          numaTotalThreads,
                       NumaReplicatedAccessToken       token) :
    // Unpack the SharedState struct into member variables
    sharedHistory(sharedState.sharedHistories.at(token.get_numa_index())),
    threadIdx(threadId),
    numaThreadIdx(numaThreadId),
    numaTotal(numaTotalThreads),
    numaAccessToken(token),
    manager(std::move(sm)),
    options(sharedState.options),
    threads(sharedState.threads),
    tt(sharedState.tt),
    networks(sharedState.networks),
    refreshTable(networks[token]) {
    clear();
}

void Search::Worker::ensure_network_replicated() {
    // Access once to force lazy initialization.
    // We do this because we want to avoid initialization during search.
    (void) (networks[numaAccessToken]);
}

void Search::Worker::start_searching() {

    accumulatorStack.reset();
    lastIterationPV.clear();

    // Non-main threads go directly to iterative_deepening()
    if (!is_mainthread())
    {
        iterative_deepening();
        return;
    }

    main_manager()->tm.init(limits, rootPos.side_to_move(), rootPos.game_ply(), options,
                            main_manager()->originalTimeAdjust);
    tt.new_search();

    if (rootMoves.empty())
    {
        rootMoves.emplace_back(Move::none());
        main_manager()->updates.onUpdateNoMoves(
          {0, {rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW, rootPos}});
    }
    else
    {
        threads.start_searching();  // start non-main threads
        iterative_deepening();      // main thread start searching
    }

    // When we reach the maximum depth, we can arrive here without a raise of
    // threads.stop. However, if we are pondering or in an infinite search,
    // the UCI protocol states that we shouldn't print the best move before the
    // GUI sends a "stop" or "ponderhit" command. We therefore simply wait here
    // until the GUI sends one of those commands.
    while (!threads.stop && (main_manager()->ponder || limits.infinite))
    {}  // Busy wait for a stop or a ponder reset

    // Stop the threads if not already stopped (also raise the stop if
    // "ponderhit" just reset threads.ponder)
    threads.stop = true;

    // Wait until all threads have finished
    threads.wait_for_search_finished();

    // When playing in 'nodes as time' mode, subtract the searched nodes from
    // the available ones before exiting.
    if (limits.npmsec)
        main_manager()->tm.advance_nodes_time(threads.nodes_searched()
                                              - limits.inc[rootPos.side_to_move()]);

    Worker* bestThread = this;
    Skill   skill =
      Skill(options["Skill Level"], options["UCI_LimitStrength"] ? int(options["UCI_Elo"]) : 0);

    if (int(options["MultiPV"]) == 1 && !limits.depth && !skill.enabled()
        && rootMoves[0].pv[0] != Move::none())
        bestThread = threads.get_best_thread()->worker.get();

    main_manager()->bestPreviousScore        = bestThread->rootMoves[0].score;
    main_manager()->bestPreviousAverageScore = bestThread->rootMoves[0].averageScore;

    std::string ponder;
    bool        extractedPonder = false;

    if (bestThread->rootMoves[0].pv.size() == 1)
        extractedPonder = bestThread->rootMoves[0].extract_ponder_from_tt(tt, rootPos);

    // Send again PV info if we have a new best thread or extracted a ponder move.
    if (bestThread != this || extractedPonder)
        main_manager()->pv(*bestThread, threads, tt, bestThread->completedDepth);

    // In rare cases, pv() may change the ponder move through syzygy_extend_pv().
    if (bestThread->rootMoves[0].pv.size() > 1)
        ponder = UCIEngine::move(bestThread->rootMoves[0].pv[1], rootPos.is_chess960());

    auto bestmove = UCIEngine::move(bestThread->rootMoves[0].pv[0], rootPos.is_chess960());
    main_manager()->updates.onBestmove(bestmove, ponder);
}

// Main iterative deepening loop. It calls search()
// repeatedly with increasing depth until the allocated thinking time has been
// consumed, the user stops the search, or the maximum search depth is reached.
void Search::Worker::iterative_deepening() {

    SearchManager* mainThread = (is_mainthread() ? main_manager() : nullptr);

    Move pv[MAX_PLY + 1];

    Depth lastBestMoveDepth = 0;

    Value  alpha, beta;
    Value  bestValue     = -VALUE_INFINITE;
    Color  us            = rootPos.side_to_move();
    double timeReduction = 1, totBestMoveChanges = 0;
    int    delta, iterIdx                        = 0;

    // Allocate stack with extra size to allow access from (ss - 7) to (ss + 2):
    // (ss - 7) is needed for update_continuation_histories(ss - 1) which accesses (ss - 6),
    // (ss + 2) is needed for initialization of cutOffCnt.
    Stack  stack[MAX_PLY + 10] = {};
    Stack* ss                  = stack + 7;

    for (int i = 7; i > 0; --i)
    {
        (ss - i)->continuationHistory =
          &continuationHistory[0][0][NO_PIECE][0];  // Use as a sentinel
        (ss - i)->continuationCorrectionHistory = &continuationCorrectionHistory[NO_PIECE][0];
        (ss - i)->staticEval                    = VALUE_NONE;
    }

    for (int i = 0; i <= MAX_PLY + 2; ++i)
        (ss + i)->ply = i;

    ss->pv = pv;

    if (mainThread)
    {
        if (mainThread->bestPreviousScore == VALUE_INFINITE)
            mainThread->iterValue.fill(VALUE_ZERO);
        else
            mainThread->iterValue.fill(mainThread->bestPreviousScore);
    }

    size_t multiPV = size_t(options["MultiPV"]);
    Skill skill(options["Skill Level"], options["UCI_LimitStrength"] ? int(options["UCI_Elo"]) : 0);

    // When playing with strength handicap enable MultiPV search that we will
    // use behind-the-scenes to retrieve a set of possible moves.
    if (skill.enabled())
        multiPV = std::max(multiPV, size_t(4));

    multiPV = std::min(multiPV, rootMoves.size());

    int searchAgainCounter = 0;

    lowPlyHistory.fill(98);

    for (Color c : {WHITE, BLACK})
        for (int i = 0; i < UINT_16_HISTORY_SIZE; i++)
            mainHistory[c][i] = mainHistory[c][i] * 820 / 1024;

    // Iterative deepening loop until requested to stop or the target depth is reached
    while (++rootDepth < MAX_PLY && !threads.stop
           && !(limits.depth && mainThread && rootDepth > limits.depth))
    {
        // Age out PV variability metric
        if (mainThread)
            totBestMoveChanges /= 2;

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (RootMove& rm : rootMoves)
            rm.previousScore = rm.score;

        size_t pvFirst = 0;
        pvLast         = 0;

        if (!threads.increaseDepth)
            searchAgainCounter++;

        // MultiPV loop. We perform a full root search for each PV line
        for (pvIdx = 0; pvIdx < multiPV; ++pvIdx)
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
            delta     = 5 + threadIdx % 8 + std::abs(rootMoves[pvIdx].meanSquaredScore) / 10208;
            Value avg = rootMoves[pvIdx].averageScore;
            alpha     = std::max(avg - delta, -VALUE_INFINITE);
            beta      = std::min(avg + delta, VALUE_INFINITE);

            // Adjust optimism based on root move's averageScore
            optimism[us]  = 144 * avg / (std::abs(avg) + 91);
            optimism[~us] = -optimism[us];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, re-search with a bigger window until we don't fail
            // high/low anymore.
            int failedHighCnt = 0;
            while (true)
            {
                // Adjust the effective depth searched, but ensure at least one
                // effective increment for every four searchAgain steps (see issue #2717).
                Depth adjustedDepth =
                  std::max(1, rootDepth - failedHighCnt - 3 * (searchAgainCounter + 1) / 4);
                rootDelta = beta - alpha;
                bestValue = search<Root>(rootPos, ss, alpha, beta, adjustedDepth, false);

                // Bring the best move to the front. It is critical that sorting
                // is done with a stable algorithm because all the values but the
                // first and eventually the new best one is set to -VALUE_INFINITE
                // and we want to keep the same order for all the moves except the
                // new PV that goes to the front. Note that in the case of MultiPV
                // search the already searched PV lines are preserved.
                std::stable_sort(rootMoves.begin() + pvIdx, rootMoves.begin() + pvLast);

                // If search has been stopped, we break immediately. Sorting is
                // safe because RootMoves is still valid, although it refers to
                // the previous iteration.
                if (threads.stop)
                    break;

                // When failing high/low give some update before a re-search. To avoid
                // excessive output that could hang GUIs like Fritz 19, only start
                // at nodes > 10M (rather than depth N, which can be reached quickly)
                if (mainThread && multiPV == 1 && (bestValue <= alpha || bestValue >= beta)
                    && nodes > 10000000)
                    main_manager()->pv(*this, threads, tt, rootDepth);

                // In case of failing low/high increase aspiration window and re-search,
                // otherwise exit the loop.
                if (bestValue <= alpha)
                {
                    beta  = alpha;
                    alpha = std::max(bestValue - delta, -VALUE_INFINITE);

                    failedHighCnt = 0;
                    if (mainThread)
                        mainThread->stopOnPonderhit = false;
                }
                else if (bestValue >= beta)
                {
                    alpha = std::max(beta - delta, alpha);
                    beta  = std::min(bestValue + delta, VALUE_INFINITE);
                    ++failedHighCnt;
                }
                else
                    break;

                delta += delta / 3;

                assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
            }

            // Sort the PV lines searched so far and update the GUI
            std::stable_sort(rootMoves.begin() + pvFirst, rootMoves.begin() + pvIdx + 1);

            if (mainThread
                && (threads.stop || pvIdx + 1 == multiPV || nodes > 10000000)
                // A thread that aborted search can have a mated-in/TB-loss score and
                // PV that cannot be trusted, i.e. it can be delayed or refuted if we
                // would have had time to fully search other root-moves. Thus here we
                // suppress any exact mated-in/TB loss output and, if we do, below pick
                // the score/PV from the previous iteration.
                && !(threads.stop && is_loss(rootMoves[0].uciScore)
                     && rootMoves[0].score == rootMoves[0].uciScore))
                main_manager()->pv(*this, threads, tt, rootDepth);

            if (threads.stop)
                break;
        }

        if (!threads.stop)
        {
            completedDepth = rootDepth;

            if (lastIterationPV.empty() || rootMoves[0].pv[0] != lastIterationPV[0])
                lastBestMoveDepth = rootDepth;

            lastIterationPV = rootMoves[0].pv;
        }

        // We make sure not to pick an unproven mated-in score,
        // in case this thread prematurely stopped search (aborted-search).
        if (completedDepth != rootDepth && rootMoves[0].score != -VALUE_INFINITE
            && is_loss(rootMoves[0].score))
        {
            // Bring the last best move to the front for best thread selection.
            // For an aborted d1 search we label the loss score as inexact.
            if (!lastIterationPV.empty())
            {
                Utility::move_to_front(rootMoves, [&lastPV = std::as_const(lastIterationPV)](
                                                    const auto& rm) { return rm == lastPV[0]; });
                rootMoves[0].pv    = lastIterationPV;
                rootMoves[0].score = rootMoves[0].uciScore = rootMoves[0].previousScore;
            }
            else
            {
                if (!rootMoves[0].scoreLowerbound)
                    rootMoves[0].scoreUpperbound = true;
                if (mainThread)
                    main_manager()->pv(*this, threads, tt, rootDepth);
            }
        }

        // Have we found a "mate in x" after a completed iteration?
        if (limits.mate && !threads.stop
            && ((rootMoves[0].score >= VALUE_MATE_IN_MAX_PLY
                 && VALUE_MATE - rootMoves[0].score <= 2 * limits.mate)
                || (rootMoves[0].score <= VALUE_MATED_IN_MAX_PLY
                    && VALUE_MATE + rootMoves[0].score <= 2 * limits.mate)))
            threads.stop = true;

        if (!mainThread)
            continue;

        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (skill.enabled() && skill.time_to_pick(rootDepth))
            skill.pick_best(rootMoves, multiPV);

        // Use part of the gained time from a previous stable move for the current move
        for (auto&& th : threads)
        {
            totBestMoveChanges += th.worker->bestMoveChanges;
            th.worker->bestMoveChanges = 0;
        }

        // Do we have time for the next iteration? Can we stop searching now?
        if (limits.use_time_management() && !threads.stop && !mainThread->stopOnPonderhit)
        {
            double fallingEval = (66 + 14 * (mainThread->bestPreviousAverageScore - bestValue)
                                       + 6 * (mainThread->iterValue[iterIdx] - bestValue))
                               / 616.6;
            fallingEval = std::clamp(fallingEval, 0.5, 1.5);

            // If the bestMove is stable over several iterations, reduce time accordingly
            timeReduction    = lastBestMoveDepth + 8 < completedDepth ? 1.56 : 0.69;
            double reduction = (1.4 + mainThread->previousTimeReduction) / (2.03 * timeReduction);
            double bestMoveInstability = 1 + 1.8 * totBestMoveChanges / threads.size();

            double totalTime = mainThread->tm.optimum() * fallingEval * reduction
                             * bestMoveInstability;

            // Cap used time in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = std::min(500.0, totalTime);

            // Stop the search if we have exceeded the totalTime
            if (elapsed_time() > totalTime)
            {
                // If we are allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainThread->ponder)
                    mainThread->stopOnPonderhit = true;
                else
                    threads.stop = true;
            }
            else if (!mainThread->ponder && elapsed_time() > totalTime * 0.50)
                mainThread->tm.advance_nodes_time(elapsed_time());
        }

        mainThread->iterValue[iterIdx] = bestValue;
        iterIdx                        = (iterIdx + 1) & 3;
    }

    if (!mainThread)
        return;

    mainThread->previousTimeReduction = timeReduction;
}

// This is the main search function, for both PV and non-PV nodes
template<NodeType nodeType>
Value Search::Worker::search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth,
                             bool cutNode) {

    constexpr bool PvNode   = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    // Dive into quiescence search when the depth reaches zero
    if (depth <= 0)
        return qsearch<PvNode ? PV : NonPV>(pos, ss, alpha, beta);

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(0 < depth && depth < MAX_PLY);
    assert(!(PvNode && cutNode));

    Move     pv[MAX_PLY + 1], capturesSearched[32], quietsSearched[64];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry*    tte;
    Key         posKey;
    Move        ttMove, move, excludedMove, bestMove;
    Depth       extension, newDepth;
    Value       bestValue, value, ttValue, eval, maxValue, probcutBeta;
    bool        givesCheck, improving, priorCapture, singularQuietLMR;
    bool        capture, moveCountPruning, ttCapture;
    Piece       movedPiece;
    int         moveCount, captureCount, quietCount;
    int         singularBeta, multiCut, cutoffCnt, thisThread = threadIdx;
    Square      prevSq = (ss - 1)->currentMove.is_ok() ? (ss - 1)->currentMove.to_sq() : SQ_NONE;

    // Step 1. Initialize node
    if (PvNode)
    {
        (ss + 1)->pv           = pv;
        ss->pv[0]              = Move::none();
    }

    Worker* thisWorker         = this;
    ss->currentMove            = Move::none();
    ss->continuationHistory    = &thisWorker->continuationHistory[ss->inCheck][ss->ttPv][NO_PIECE][0];
    ss->continuationCorrectionHistory = &thisWorker->continuationCorrectionHistory[NO_PIECE][0];
    ss++->ply                  = (ss - 1)->ply + 1;

    // Check for the available remaining time
    if (thisThread == 0)
        main_manager()->check_time(*thisWorker);

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && thisWorker->selDepth < ss->ply + 1)
        thisWorker->selDepth = ss->ply + 1;

    if (!rootNode)
    {
        // Step 2. Check for aborted search and immediate draw
        if (threads.stop.load(std::memory_order_relaxed) || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return (ss->ply >= MAX_PLY && !pos.checkers()) ? evaluate(pos)
                                                           : value_draw(thisWorker->nodes);

        // Step 3. Mate distance pruning. Even if we mate at the next move our score
        // would be at best mate_in(ss->ply+1), but if alpha is already bigger because
        // a shorter mate was found upward in the tree then there is no need to search
        // because we will never beat the current alpha. Same logic but with reversed
        // signs apply also in the opposite condition of being mated instead of giving
        // mate. In this case, return a fail-high score.
        alpha = std::max(mated_in(ss->ply), alpha);
        beta  = std::min(mate_in(ss->ply + 1), beta);
        if (alpha >= beta)
            return alpha;
    }

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    (ss + 1)->excludedMove = Move::none();
    (ss + 2)->killers[0] = (ss + 2)->killers[1] = Move::none();
    (ss + 2)->cutoffCnt                         = 0;
    ss->doubleExtensions                        = (ss - 1)->doubleExtensions;
    Square prevPrevSq = (ss - 2)->currentMove.is_ok() ? (ss - 2)->currentMove.to_sq() : SQ_NONE;
    ss->statScore     = 0;

    // Step 4. Transposition table lookup.
    excludedMove = ss->excludedMove;
    posKey       = pos.key();
    tte          = tt.probe(posKey, ss->ttHit);
    ttValue      = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove       = rootNode ? thisWorker->rootMoves[thisWorker->pvIdx].pv[0]
                            : ss->ttHit  ? tte->move()
                                         : Move::none();
    ttCapture    = ttMove && pos.capture_stage(ttMove);

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && !excludedMove && tte->depth() > depth - 4 && ttValue != VALUE_NONE
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
    {
        // If ttMove is quiet, update move sorting heuristics on TT hit (~2 Elo)
        if (ttMove)
        {
            if (ttValue >= beta)
            {
                // Bonus for a quiet ttMove that fails high (~2 Elo)
                if (!ttCapture)
                    update_quiet_histories(pos, ss, *thisWorker, ttMove, stat_bonus(depth));

                // Extra penalty for early quiet moves of
                // the previous ply (~1 Elo)
                if ((ss - 1)->moveCount <= 2 && !priorCapture)
                    update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                                  -stat_bonus(depth + 1));
            }
            // Penalty for a quiet ttMove that fails low (~1 Elo)
            else if (!ttCapture)
            {
                int penalty = -stat_bonus(depth);
                thisWorker->mainHistory[pos.side_to_move()][ttMove.from_to()] << penalty;
                update_continuation_histories(ss, pos.moved_piece(ttMove), ttMove.to_sq(), penalty);
            }
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < 90)
            return ttValue;
    }

    // Step 5. Tablebases probe
    if (!rootNode && !excludedMove && tbConfig.cardinality
        && pos.count<ALL_PIECES>() <= tbConfig.cardinality
        && (pos.count<ALL_PIECES>() < tbConfig.cardinality || depth >= tbConfig.probeDepth)
        && pos.rule50_count() * 2 < 100)
    {
        int piecesCount = pos.count<ALL_PIECES>();

        if (piecesCount <= TB::MaxCardinality && !pos.can_castle(ANY_CASTLING))
        {
            // If the current root position is in the tablebases, then
            // RootMoves should have been filtered already.
            assert(depth != thisWorker->rootDepth);

            TB::ProbeState err;
            TB::WDLScore   wdl = Tablebases::probe_wdl(pos, &err);

            // Force check of time on the next occasion
            if (thisThread == 0)
                main_manager()->callsCnt = 0;

            if (err != TB::ProbeState::FAIL)
            {
                thisWorker->tbHits.fetch_add(1, std::memory_order_relaxed);

                int drawScore = TB::UseRule50 ? 1 : 0;

                // use the range to map TB scores
                Value tbValue =  wdl < -drawScore ? VALUE_MATED_IN_MAX_PLY  + MAX_PLY + 1
                               : wdl >  drawScore ? VALUE_MATE_IN_MAX_PLY - MAX_PLY - 1
                                                  : VALUE_DRAW + 2 * wdl * drawScore;

                Bound tbBound =  wdl < -drawScore ? BOUND_UPPER
                               : wdl >  drawScore ? BOUND_LOWER : BOUND_EXACT;

                if (tbBound == BOUND_EXACT
                    || (tbBound == BOUND_LOWER ? tbValue >= beta : tbValue <= alpha))
                {
                    tte->save(posKey, value_to_tt(tbValue, ss->ply), ss->ttPv, tbBound,
                              std::min(MAX_PLY - 1, depth + 6), Move::none(), VALUE_NONE);

                    return tbValue;
                }

                if (PvNode)
                {
                    if (tbBound == BOUND_LOWER)
                        bestValue = tbValue, alpha = std::max(alpha, bestValue);
                    else
                        maxValue = tbValue;
                }
            }
        }
    }

    CapturePieceToHistory& captureHistory = thisWorker->captureHistory;

    // Step 6. Static evaluation of the position
    Value unadjustedStaticEval = VALUE_NONE;
    if (pos.checkers())
    {
        // Skip early pruning when in check
        ss->staticEval = eval = VALUE_NONE;
        improving             = false;
        goto moves_loop;
    }
    else if (excludedMove)
    {
        // Providing the hint that this node's accumulator will be used often brings significant Elo gain (~13 Elo)
        Eval::NNUE::hint_common_parent_position(pos, thisWorker->refreshTable, thisWorker->accumulatorStack);
        eval = ss->staticEval;
    }
    else if (ss->ttHit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = tte->eval();
        if (unadjustedStaticEval != VALUE_NONE)
        {
            eval = unadjustedStaticEval;
            // ttValue can be used as a better position evaluation (~7 Elo)
            if (ttValue != VALUE_NONE && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
                eval = ttValue;
        }
        else
            eval = evaluate(pos);

        // Can ttValue be used as a better position evaluation?
        if (ttValue != VALUE_NONE
            && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;

        ss->staticEval = eval = to_corrected_static_eval(eval, correction_value(*thisWorker, pos, ss));
    }
    else
    {
        eval                 = evaluate(pos);
        unadjustedStaticEval = eval;
        ss->staticEval = eval = to_corrected_static_eval(eval, correction_value(*thisWorker, pos, ss));

        // Fresh TT entry
        if (!excludedMove)
            tte->save(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_NONE, Move::none(),
                      unadjustedStaticEval);
    }

    // Use static evaluation difference to improve quiet move ordering (~9 Elo)
    if (is_ok(prevSq) && (ss - 1)->staticEval != VALUE_NONE
        && !priorCapture && is_ok((ss - 1)->currentMove))
    {
        int bonus = std::clamp(-18 * int((ss - 1)->staticEval + ss->staticEval), -1812, 1812);
        thisWorker->mainHistory[~pos.side_to_move()][(ss - 1)->currentMove.from_to()] << bonus;
    }

    // Set up the improving flag, which is true if current static evaluation is
    // bigger than the previous static evaluation at our turn (if we were in
    // check at previous move we look at static evaluation at move prior to it
    // and if we were in check at move prior to it flag is set to true)
    improving = (ss - 2)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 2)->staticEval
                                                    : (ss - 4)->staticEval != VALUE_NONE && ss->staticEval > (ss - 4)->staticEval;

    // Step 7. Razoring (~1 Elo)
    // If eval is really low check with qsearch if it can exceed alpha, if it can't,
    // return a fail low.
    // Adjust razor margin according to cutoffCnt. (~1 Elo)
    if (depth <= 7 && eval < alpha - 369 - 254 * depth - 200 * ((ss + 1)->cutoffCnt > 3))
    {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha)
        {
            // Adjust correction history
            update_correction_history(pos, ss, *thisWorker, stat_bonus(depth) * -200 / 128);
            return value;
        }
    }

    // Step 8. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!PvNode && depth < 9
        && eval - futility_margin(depth, cutNode && !ss->ttHit, improving) - (ss - 1)->statScore / 337 >= beta
        && eval >= beta && beta > VALUE_TB_LOSS_IN_MAX_PLY && eval < VALUE_TB_WIN_IN_MAX_PLY)
    {
        // Adjust correction history
        update_correction_history(pos, ss, *thisWorker, stat_bonus(depth) * 200 / 128);
        return beta > eval ? (eval + beta) / 2 : eval;  // Do not return unproven wins
    }

    // Step 9. Null move search with verification search (~35 Elo)
    if (!PvNode && (ss - 1)->currentMove != Move::null() && (ss - 1)->statScore < 17329
        && eval >= beta && beta > VALUE_TB_LOSS_IN_MAX_PLY && eval < VALUE_TB_WIN_IN_MAX_PLY
        && ss->staticEval >= beta - 21 * depth + 258 && !excludedMove
        && pos.non_pawn_material(pos.side_to_move()) && ss->ply >= thisWorker->nmpMinPly
        && (!ss->ttHit || tte->depth() < depth))
    {
        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth, eval and beta (~3 Elo)
        Depth R = std::min(int(eval - beta) / 168, 6) + depth / 3 + 4;

        ss->currentMove         = Move::null();
        ss->continuationHistory = &thisWorker->continuationHistory[0][0][NO_PIECE][0];
        ss->continuationCorrectionHistory = &thisWorker->continuationCorrectionHistory[NO_PIECE][0];

        pos.do_null_move(st);

        Value nullValue = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);

        pos.undo_null_move();

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && nullValue < VALUE_TB_WIN_IN_MAX_PLY)
        {
            if (thisWorker->nmpMinPly || depth < 14)
                return nullValue;

            assert(!thisWorker->nmpMinPly);

            // Do verification search at high depths, with null move pruning disabled
            // until ply exceeds nmpMinPly.
            thisWorker->nmpMinPly = ss->ply + 3 * (depth - R) / 4;

            Value v = search<NonPV>(pos, ss, beta - 1, beta, depth - R, false);

            thisWorker->nmpMinPly = 0;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 10. Internal iterative reductions (~9 Elo)
    // For PV nodes without a ttMove, we decrease depth by 2,
    // or by 4 if the current position is present in the TT and
    // the stored depth is greater than or equal to the current depth.
    // Use qsearch if depth <= 0.
    if (PvNode && !ttMove)
        depth -= 2 + 2 * (ss->ttHit && tte->depth() >= depth);

    probcutBeta = beta + 168 - 61 * improving;

    // Step 11. ProbCut (~10 Elo)
    // If we have a good enough capture (or queen promotion) and a reduced search returns a value
    // much above beta, we can (almost) safely prune the previous move.
    if (!PvNode && depth > 3 && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
        // If value from transposition table is lower than probcutBeta, don't attempt probcut
        // there and in further interactions with transposition table cutoff depth is set to depth - 3
        // because probcut search has depth set to depth - 4 but we also do a move before it
        // So effective depth is equal to depth - 3
        && !(tte->depth() >= depth - 3 && ttValue != VALUE_NONE && ttValue < probcutBeta))
    {
        assert(probcutBeta < VALUE_INFINITE && probcutBeta > beta);

        MovePicker mp(pos, ttMove, probcutBeta - ss->staticEval, &captureHistory,
                      thisWorker->refreshTable);

        while ((move = mp.next_move()) != Move::none())
            if (move != excludedMove && pos.legal(move))
            {
                assert(pos.capture_stage(move));

                // Prefetch the TT entry for the resulting position
                prefetch(tt.first_entry(pos.key_after(move)));

                ss->currentMove = move;
                ss->continuationHistory =
                  &thisWorker
                     ->continuationHistory[pos.checkers()][ss->ttPv][pos.moved_piece(move)][move.to_sq()];
                ss->continuationCorrectionHistory = &thisWorker->continuationCorrectionHistory[pos.moved_piece(move)][move.to_sq()];

                pos.do_move(move, st, givesCheck);

                // Perform a preliminary qsearch to verify that the move holds
                value = -qsearch<NonPV>(pos, ss + 1, -probcutBeta, -probcutBeta + 1);

                // If the qsearch held, perform the regular search
                if (value >= probcutBeta)
                    value = -search<NonPV>(pos, ss + 1, -probcutBeta, -probcutBeta + 1, depth - 4, !cutNode);

                pos.undo_move(move);

                if (value >= probcutBeta)
                {
                    // Save ProbCut data into the transposition table
                    tte->save(posKey, value_to_tt(value, ss->ply), ss->ttPv, BOUND_LOWER, depth - 3,
                              move, unadjustedStaticEval);
                    return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probcutBeta - beta) : value;
                }
            }

        Eval::NNUE::hint_common_parent_position(pos, thisWorker->refreshTable, thisWorker->accumulatorStack);
    }

moves_loop:  // When in check, search starts here

    // Step 12. A small Probcut idea, when we are in check (~4 Elo)
    probcutBeta = beta + 425;
    if (pos.checkers() && !PvNode && ttCapture && (tte->bound() & BOUND_LOWER)
        && tte->depth() >= depth - 4 && ttValue >= probcutBeta && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY)
        return probcutBeta;

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory,
                                        (ss - 3)->continuationHistory,
                                        (ss - 4)->continuationHistory,
                                        nullptr,
                                        (ss - 6)->continuationHistory};

    Move       countermove = prevSq != SQ_NONE ? thisWorker->counterMoves[pos.piece_on(prevSq)][prevSq] : Move::none();
    MovePicker mp(pos, ttMove, depth, &thisWorker->mainHistory, &thisWorker->lowPlyHistory,
                  &captureHistory, contHist, &thisWorker->ttMoveHistory, countermove, ss->killers);

    value            = bestValue = -VALUE_INFINITE;
    moveCountPruning = singularQuietLMR = false;
    ttCapture        = ttMove && pos.capture_stage(ttMove);
    singularBeta     = -VALUE_INFINITE;
    multiCut         = 0;
    cutoffCnt        = 0;

    // Indicate if a quiet ttMove has been searched
    bool ttQuietSearched = false;

    // Step 13. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move(moveCountPruning)) != Move::none())
    {
        assert(pos.pseudo_legal(move));

        if (move == excludedMove)
            continue;

        // Check for legality
        if (!pos.legal(move))
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in Root
        // Move List. In MultiPV mode we also skip PV moves that have been already
        // searched and those of lower "TB rank" if we are in a TB root position.
        if (rootNode && !std::count(thisWorker->rootMoves.begin() + thisWorker->pvIdx,
                                    thisWorker->rootMoves.begin() + thisWorker->pvLast, move))
            continue;

        // Prefetch the TT entry for the resulting position
        prefetch(tt.first_entry(pos.key_after(move)));

        ss->moveCount = ++moveCount;

        if (rootNode && thisWorker == threads.main_worker() && elapsed_time() > 2500)
            main_manager()->updates.onIter({depth, UCIEngine::move(move, pos.is_chess960()), moveCount});

        if (PvNode)
            (ss + 1)->pv = nullptr;

        extension   = 0;
        capture     = pos.capture_stage(move);
        movedPiece  = pos.moved_piece(move);
        givesCheck  = pos.gives_check(move);

        //Calculuate the new depth for this move
        newDepth = depth - 1;

        Value delta = beta - alpha;

        Depth r = reduction(improving, depth, moveCount, delta);

        // Step 14. Pruning at shallow depth (~120 Elo). Depth conditions are important for mate finding.
        if (!rootNode && pos.non_pawn_material(pos.side_to_move()) && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
        {
            // Skip quiet moves if movecount exceeds our FutilityMoveCount threshold (~8 Elo)
            if (!capture && moveCount >= futility_move_count(improving, depth))
                moveCountPruning = true;

            // Reduced depth of the next LMR search
            int lmrDepth = newDepth - r;

            if (capture || givesCheck)
            {
                // Futility pruning for captures (~2 Elo)
                if (!givesCheck && lmrDepth < 7 && !ss->inCheck)
                {
                    Value futilityValue =
                      ss->staticEval + 238 + 305 * lmrDepth + PieceValue[pos.piece_on(move.to_sq())]
                      + captureHistory[movedPiece][move.to_sq()][type_of(pos.piece_on(move.to_sq()))]
                          / 7;
                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures and checks (~11 Elo)
                if (!pos.see_ge(move, -185 * depth))
                    continue;
            }
            else
            {
                int history = (*(contHist[0]))[movedPiece][move.to_sq()]
                            + (*(contHist[1]))[movedPiece][move.to_sq()]
                            + (*(contHist[3]))[movedPiece][move.to_sq()];

                // Continuation history based pruning (~2 Elo)
                if (lmrDepth < 28 && history < -3832 * depth)
                    continue;

                history += thisWorker->mainHistory[pos.side_to_move()][move.from_to()];

                lmrDepth += history / 7011;
                lmrDepth = std::max(lmrDepth, -2);

                // Futility pruning: parent node (~13 Elo)
                if (!ss->inCheck && lmrDepth < 14
                    && ss->staticEval + 115 + 137 * lmrDepth + history / 60 <= alpha)
                    continue;

                lmrDepth = std::max(lmrDepth, 0);

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, -31 * lmrDepth * lmrDepth))
                    continue;
            }
        }

        // Step 15. Extensions (~100 Elo)
        // We take care to not overdo to avoid search getting stuck.
        if (ss->ply < thisWorker->rootDepth * 2)
        {
            // Singular extension search (~94 Elo). If all moves but one fail low on a
            // search of (alpha-s, beta-s), and just one fails high on (alpha, beta),
            // then that move is singular and should be extended. To verify this we do
            // a reduced search on the position excluding the ttMove and if the result
            // is lower than ttValue minus a margin, then we will extend the ttMove.

            // Note: the depth margin and singularBeta margin are known for having non-linear
            // scaling. Their values are optimized to time controls of 180+1.8 and longer
            // so changing them or adding conditions that are similar requires tests
            // at these types of time controls.

            // Singular extension candidate ~:
            // If the ttMove is capture or if some strong capture beat us, skip singular extension
            if (!rootNode && move == ttMove && !excludedMove
                && depth >= 4 - (thisWorker->completedDepth > 24) + 2 * (PvNode && tte->is_pv())
                && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && (tte->bound() & BOUND_LOWER)
                && tte->depth() >= depth - 3)
            {
                Value singularBeta = ttValue - (64 + 57 * (ss->ttPv && !PvNode)) * depth / 64;
                Depth singularDepth = newDepth / 2;

                ss->excludedMove = move;
                value = search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
                ss->excludedMove = Move::none();

                if (value < singularBeta)
                {
                    extension        = 1;
                    singularQuietLMR = !ttCapture;

                    // Avoid search explosion by limiting the number of double extensions
                    if (!PvNode && value < singularBeta - 18 && ss->doubleExtensions <= 11)
                    {
                        extension = 2;
                        depth += depth < 15;
                    }
                }

                // Multi-cut pruning
                // Our ttMove is assumed to fail high based on the bound of the TT entry,
                // and if after excluding the ttMove with a reduced search we fail high over the original beta,
                // we assume this expected Cut-node is not singular (multiple moves fail high),
                // and we can prune the whole subtree by returning a softbound.
                else if (singularBeta >= beta)
                    return singularBeta;

                // Negative extensions
                // If other moves failed high over (ttValue - margin) without the ttMove on a reduced search,
                // but we cannot do multi-cut because (ttValue - margin) is lower than the original beta,
                // we do not know if the ttMove is singular or can do a multi-cut,
                // so we reduce the ttMove in favor of other moves based on some conditions:

                // If the ttMove is assumed to fail high over current beta (~7 Elo)
                else if (ttValue >= beta)
                    extension = -2 - !PvNode;

                // If we are on a cutNode, reduce it based on depth (~1 Elo)
                else if (cutNode)
                    extension = depth < 19 ? -2 : -1;

                // If the ttMove is assumed to fail high over the value of the reduced search (but the
                // reduced search cannot confirm this because it failed high over the ttValue subtracted by the margin)
                // we can still do a multi-cut (~1 Elo)
                else if (ttValue >= value)
                    extension = -1;
            }

            // Check extensions (~1 Elo)
            else if (givesCheck && depth > 9)
                extension = 1;

            // Quiet ttMove extensions (~1 Elo)
            else if (PvNode && move == ttMove && move == ss->killers[0]
                     && (*contHist[0])[movedPiece][move.to_sq()] >= 5177)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;
        ss->doubleExtensions = (ss - 1)->doubleExtensions + (extension == 2);

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        // Update the current move (this must be done after singular extension search)
        ss->currentMove = move;
        ss->continuationHistory =
          &thisWorker->continuationHistory[pos.checkers()][ss->ttPv][movedPiece][move.to_sq()];
        ss->continuationCorrectionHistory = &thisWorker->continuationCorrectionHistory[movedPiece][move.to_sq()];

        // Step 16. Make the move
        pos.do_move(move, st, givesCheck);

        // Decrease reduction if position is or has been on the PV (~4 Elo)
        if (ss->ttPv)
            r -= cutNode && tte->depth() >= depth ? 3 : 2;

        // Decrease reduction if opponent's move count is high (~1 Elo)
        if ((ss - 1)->moveCount > 7)
            r--;

        // Increase reduction for cut nodes (~3 Elo)
        if (cutNode)
            r += 2;

        // Increase reduction if ttMove is a capture (~3 Elo)
        if (ttCapture)
            r++;

        // Decrease reduction for PvNodes (~2 Elo)
        if (PvNode)
            r--;

        // Decrease reduction if a quiet ttMove has been searched (~1 Elo)
        if (ttQuietSearched)
            r--;

        // Increase reduction on repetitions (~1 Elo)
        if (pos.has_repeated())
            r += 2;

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if ((ss + 1)->cutoffCnt > 3)
            r++;

        // Set reduction to 0 for first picked move (ttMove) (~2 Elo)
        // Nullifies all the above reduction computation on ttMove
        if (move == ttMove)
            r = 0;

        ss->statScore = 2 * thisWorker->mainHistory[pos.side_to_move()][move.from_to()]
                      + (*contHist[0])[movedPiece][move.to_sq()]
                      + (*contHist[1])[movedPiece][move.to_sq()]
                      + (*contHist[3])[movedPiece][move.to_sq()] - 4334;

        // Step 17. Late moves reduction / extension (LMR, ~117 Elo)
        // We use various heuristics for the sons of a node after the first son has
        // been searched. In general, we would like to reduce them, but there are many
        // cases where we extend a son if it has good chances to be "interesting".
        if (depth >= 2 && moveCount > 1 + (PvNode && ss->ply <= 1)
            && (!ss->ttPv || !capture || (cutNode && (ss - 1)->moveCount > 1)))
        {
            // In general we want to cap the LMR depth search at newDepth, but when
            // reduction is negative, we allow this move a limited search extension
            // beyond the first move
            Depth d = std::clamp(newDepth - r, 1, newDepth + 1);

            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result
                // was good enough search deeper, if it was bad enough search shallower.
                const bool doDeeperSearch    = value > (bestValue + 51 + 10 * (newDepth - d));
                const bool doShallowerSearch = value < bestValue + newDepth;

                newDepth += doDeeperSearch - doShallowerSearch;

                if (newDepth > d)
                    value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                int bonus = value <= alpha ? -stat_bonus(newDepth)
                          : value >= beta  ? stat_bonus(newDepth)
                                           : 0;

                update_continuation_histories(ss, movedPiece, move.to_sq(), bonus);
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PvNode || moveCount > 1)
        {
            // Increase reduction for very late moves (~9 Elo)
            if (!capture && moveCount > 12)
                r++;

            // Note that if expected reduction is high, we reduce search depth by 1 here (~9 Elo)
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth - (r > 4), !cutNode);
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and to try
        // another move.
        if (PvNode && (moveCount == 1 || value > alpha))
        {
            (ss + 1)->pv    = pv;
            (ss + 1)->pv[0] = Move::none();

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth, false);
        }

        // Step 19. Undo move
        pos.undo_move(move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and we return immediately without
        // updating best move, PV and TT.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (rootNode)
        {
            RootMove& rm = *std::find(thisWorker->rootMoves.begin(), thisWorker->rootMoves.end(), move);

            rm.effort += thisWorker->nodes + thisWorker->tbHits - rm.effort;

            if (moveCount == 1 || value > alpha)
            {
                rm.score     = rm.uciScore = value;
                rm.selDepth  = thisWorker->selDepth;
                rm.scoreLowerbound = rm.scoreUpperbound = false;

                if (value >= beta)
                {
                    rm.scoreLowerbound = true;
                    rm.uciScore = beta;
                }
                else if (value <= alpha)
                {
                    rm.scoreUpperbound = true;
                    rm.uciScore = alpha;
                }

                rm.pv.resize(1);

                assert((ss + 1)->pv);

                for (Move* m = (ss + 1)->pv; *m != Move::none(); ++m)
                    rm.pv.push_back(*m);

                // We record how often the best move has been changed in each iteration.
                // This information is used for time management. In MultiPV mode,
                // we must take care to only do this for the first PV line.
                if (moveCount > 1 && !thisWorker->pvIdx)
                    ++thisWorker->bestMoveChanges;
            }
            else
                // All other moves but the PV, are set to the lowest value: this
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

                if (PvNode && !rootNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                {
                    ss->cutoffCnt += 1 + !ttMove;
                    assert(value >= beta);  // Fail high
                    break;
                }
                else
                {
                    // Reduce other moves if we have found at least one score improvement (~2 Elo)
                    if (depth > 2 && depth < 12 && beta - alpha > 230 && value > -12761)
                        depth -= 2;

                    assert(value < beta);  // Fail low
                    alpha = value;
                }
            }
        }

        // If the move is quiet, update move sorting heuristics
        if (!capture)
        {
            if (move != bestMove)
            {
                update_quiet_histories(pos, ss, *thisWorker, move, -stat_bonus(depth));

                // Decrease all the non-best quiet moves
                for (int i = captureCount; i < quietCount; ++i)
                {
                    thisWorker->mainHistory[pos.side_to_move()][quietsSearched[i].from_to()]
                      << -stat_bonus(depth) / 64;

                    update_continuation_histories(ss, pos.moved_piece(quietsSearched[i]),
                                                  quietsSearched[i].to_sq(), -stat_bonus(depth) / 64);
                }
            }
            else
                ttQuietSearched = true;

            quietsSearched[quietCount++] = move;
        }
        else
            // Increase the best capture move
            capturesSearched[captureCount++] = move;

        // The following condition would detect a stop only after move loop has been
        // completed. But in this case, bestValue is valid because we have fully
        // searched our subtree, and we can anyhow save the result in TT.
        /*
           if (threads.stop)
               return VALUE_DRAW;
        */
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it
    // must be a mate or a stalemate. If we are in a singular extension search then
    // return a fail low score.

    assert(moveCount || !pos.checkers() || excludedMove || !MoveList<LEGAL>(pos).size());

    // Do not overwrite a previous mate score
    if (!moveCount && !excludedMove)
        bestValue = pos.checkers() ? mated_in(ss->ply) : value_draw(thisWorker->nodes);

    // If there is a move that produces a search value greater than alpha we update the stats of searched moves
    else if (bestMove)
    {
        update_all_stats(pos, ss, *thisWorker, bestMove, prevSq, quietsSearched, capturesSearched,
                         depth, ttMove);

        // Adjust correction history
        if (!pos.capture_stage(bestMove))
            update_correction_history(pos, ss, *thisWorker, stat_bonus(depth) * 200 / 128);
    }

    // Adjust correction history for special endgames with high rule50 count
    else if (is_critical_endgame(pos) && bestValue <= alpha)
        update_correction_history(pos, ss, *thisWorker, stat_bonus(depth) * -400 / 128);

    // Bonus for prior countermove that caused the fail low
    else if (!priorCapture && prevSq != SQ_NONE)
    {
        int bonus = (depth > 5) + (PvNode || cutNode) + ((ss - 1)->statScore < -18782)
                  + ((ss - 1)->moveCount > 10) + (!ss->inCheck && bestValue <= ss->staticEval - 120);
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq, stat_bonus(depth) * bonus);
        thisWorker->mainHistory[~pos.side_to_move()][(ss - 1)->currentMove.from_to()]
          << stat_bonus(depth) * bonus / 64;
    }

    if (PvNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~7 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    if (!excludedMove && !(rootNode && thisWorker->pvIdx))
        tte->save(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                  bestValue >= beta    ? BOUND_LOWER
                  : PvNode && bestMove ? BOUND_EXACT
                                       : BOUND_UPPER,
                  depth, bestMove, unadjustedStaticEval);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}

// Quiescence search function, which is called by the main search
// function with zero depth, or recursively with further decreasing depth per call.
// (~155 Elo)
template<NodeType nodeType>
Value Search::Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));

    // Check if we have an upcoming move that draws by repetition, or
    // if the opponent had an alternative move earlier to this position.
    if (alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
    {
        alpha = value_draw(nodes);
        if (alpha >= beta)
            return alpha;
    }

    Move     pv[MAX_PLY + 1];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry* tte;
    Key      posKey;
    Move     ttMove, move, bestMove;
    Depth    ttDepth;
    Value    bestValue, value, ttValue, futilityValue, futilityBase;
    bool     pvHit, givesCheck, capture;
    int      moveCount;

    // Step 1. Initialize node
    if (PvNode)
    {
        (ss + 1)->pv           = pv;
        ss->pv[0]              = Move::none();
    }

    Worker* thisWorker = this;
    ss->currentMove    = Move::none();
    ss->continuationHistory =
      &thisWorker->continuationHistory[ss->inCheck][ss->ttPv][NO_PIECE][0];
    ss->continuationCorrectionHistory = &thisWorker->continuationCorrectionHistory[NO_PIECE][0];

    // Check for an immediate draw or maximum ply reached
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY)
        return (ss->ply >= MAX_PLY && !pos.checkers()) ? evaluate(pos) : value_draw(thisWorker->nodes);

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Decide the replacement and cutoff priority of the qsearch TT entries
    ttDepth = ss->inCheck || depth >= DEPTH_QS ? DEPTH_QS : DEPTH_QS - 1;

    // Step 2. Transposition table lookup
    posKey  = pos.key();
    tte     = tt.probe(posKey, ss->ttHit);
    ttValue = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove  = ss->ttHit ? tte->move() : Move::none();
    pvHit   = ss->ttHit && tte->is_pv();

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && tte->depth() >= ttDepth && ttValue != VALUE_NONE
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    // Step 3. Evaluate the position statically
    Value unadjustedStaticEval = VALUE_NONE;
    if (pos.checkers())
    {
        ss->staticEval = bestValue = futilityBase = -VALUE_INFINITE;
    }
    else
    {
        if (ss->ttHit)
        {
            // Never assume anything about values stored in TT
            unadjustedStaticEval = tte->eval();
            if (unadjustedStaticEval != VALUE_NONE)
            {
                ss->staticEval = unadjustedStaticEval;

                // ttValue can be used as a better position evaluation (~13 Elo)
                if (ttValue != VALUE_NONE && (tte->bound() & (ttValue > ss->staticEval ? BOUND_LOWER : BOUND_UPPER)))
                    ss->staticEval = ttValue;
            }
            else
                ss->staticEval = evaluate(pos);
        }
        else
        {
            // In case of null move search, use previous static eval with a different sign
            unadjustedStaticEval = ss->staticEval = (ss - 1)->currentMove != Move::null() ? evaluate(pos)
                                                                                           : -(ss - 1)->staticEval;
        }

        ss->staticEval = to_corrected_static_eval(ss->staticEval, correction_value(*thisWorker, pos, ss));

        // Stand pat. Return immediately if static value is at least beta
        bestValue = ss->staticEval;

        if (bestValue >= beta)
        {
            // Save gathered info in transposition table
            if (!ss->ttHit)
                tte->save(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv, BOUND_LOWER, ttDepth,
                          Move::none(), unadjustedStaticEval);

            return bestValue;
        }

        if (bestValue > alpha)
            alpha = bestValue;

        futilityBase = ss->staticEval + 200;
    }

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory,
                                        nullptr,
                                        (ss - 4)->continuationHistory,
                                        nullptr,
                                        (ss - 6)->continuationHistory};

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions, and other checks (only if depth >= DEPTH_QS) will be generated.
    Square     prevSq = is_ok((ss - 1)->currentMove) ? (ss - 1)->currentMove.to_sq() : SQ_NONE;
    MovePicker mp(pos, ttMove, depth, &thisWorker->mainHistory, &thisWorker->captureHistory, contHist, prevSq, thisWorker->refreshTable);

    int quietCheckEvasions = 0;

    // Step 4. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::none())
    {
        assert(pos.pseudo_legal(move));

        // Check for legality
        if (!pos.legal(move))
            continue;

        givesCheck = pos.gives_check(move);
        capture    = pos.capture_stage(move);

        moveCount++;

        // Step 5. Pruning
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(pos.side_to_move()))
        {
            // Futility pruning and moveCount pruning (~10 Elo)
            if (!givesCheck && move.to_sq() != prevSq && futilityBase > -VALUE_KNOWN_WIN
                && move.type_of() != PROMOTION)
            {
                if (moveCount > 2)
                    continue;

                futilityValue = futilityBase + PieceValue[pos.piece_on(move.to_sq())];

                // If static eval + value of piece we are going to capture is much lower
                // than alpha we can prune this move.
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }

                // If static eval is much lower than alpha and move is not winning material
                // we can prune this move.
                if (futilityBase <= alpha && !pos.see_ge(move, 1))
                {
                    bestValue = std::max(bestValue, futilityBase);
                    continue;
                }

                // If static evaluation is much lower than alpha, and we cannot gain enough
                // material to reach alpha we can prune this move.
                if (futilityBase > alpha && !pos.see_ge(move, (alpha - futilityBase) * 4))
                    continue;
            }

            // We prune after the second quiet check evasion move, where being 'in check' is
            // implicitly checked through the counter, and being a 'quiet move' apart from
            // being a tt move is assumed after an increment because captures are ordered first.
            if (pos.checkers() && !capture && ++quietCheckEvasions > 2)
                break;

            // Continuation history based pruning (~3 Elo)
            if (!capture && (*contHist[0])[pos.moved_piece(move)][move.to_sq()] < 0
                && (*contHist[1])[pos.moved_piece(move)][move.to_sq()] < 0)
                continue;

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (!pos.see_ge(move, -90))
                continue;
        }

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        // Update the current move
        ss->currentMove = move;
        ss->continuationHistory =
          &thisWorker
             ->continuationHistory[pos.checkers()][ss->ttPv][pos.moved_piece(move)][move.to_sq()];
        ss->continuationCorrectionHistory = &thisWorker->continuationCorrectionHistory[pos.moved_piece(move)][move.to_sq()];

        // Step 6. Make and search the move
        thisWorker->nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);
        value = -qsearch<nodeType>(pos, ss + 1, -beta, -alpha);
        pos.undo_move(move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 7. Check for a new best move
        if (value > bestValue)
        {
            bestValue = value;

            if (value > alpha)
            {
                bestMove = move;

                if (PvNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value < beta)  // Update alpha here!
                    alpha = value;
                else
                    break;  // Fail high
            }
        }
    }

    // Step 8. Check for mate
    // All legal moves have been searched. A special case: if we're in check
    // and no legal moves were found, it is checkmate.
    if (pos.checkers() && !moveCount)
        return mated_in(ss->ply);  // Plies to mate from the root

    // Save gathered info in transposition table
    tte->save(posKey, value_to_tt(bestValue, ss->ply), pvHit,
              bestValue >= beta ? BOUND_LOWER : BOUND_UPPER, ttDepth, bestMove, unadjustedStaticEval);

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}

// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores
// are unchanged. The function is called before storing a value in the TT.
Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);

    return v >= VALUE_TB_WIN_IN_MAX_PLY ? v + ply : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated
// from current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, we return the stuck value when the rule50 count
// is high and the absolute value of the score is below the TB values.
Value value_from_tt(Value v, int ply, int r50c) {

    if (v == VALUE_NONE)
        return VALUE_NONE;

    // handle the common case of non-TB/mate scores first
    if (v < VALUE_TB_LOSS_IN_MAX_PLY || v > VALUE_TB_WIN_IN_MAX_PLY)
        return v;

    // For rule50 counts close to the draw score, we return VALUE_DRAW
    if (r50c > 90 && (v > VALUE_TB_LOSS_IN_MAX_PLY && v < VALUE_TB_WIN_IN_MAX_PLY))
        return VALUE_DRAW;

    return v >= VALUE_TB_WIN_IN_MAX_PLY ? v - ply : v + ply;
}

// Adds current move and appends child pv[]
static void update_pv(Move* pv, Move move, const Move* childPv) {

    for (*pv++ = move; childPv && *childPv != Move::none();)
        *pv++ = *childPv++;
    *pv = Move::none();
}

// Updates histories of the move pairs formed
// by moves at ply -1, -2, -4, and -6 with current move.
static void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {

    for (int i : {1, 2, 4, 6})
    {
        // Only update the first 2 continuation histories if we are in check
        if (ss->inCheck && i > 2)
            break;
        if (is_ok((ss - i)->currentMove))
            (*(ss - i)->continuationHistory)[pc][to] << bonus / (1 + 3 * (i == 4));
    }
}

// Updates move sorting heuristics
static void update_quiet_histories(const Position& pos,
                                   Stack*          ss,
                                   Search::Worker& workerThread,
                                   Move            move,
                                   int             bonus) {

    // Update killers
    if (ss->killers[0] != move)
    {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = move;
    }

    Color   us         = pos.side_to_move();
    Worker* thisWorker = &workerThread;
    thisWorker->mainHistory[us][move.from_to()] << bonus;
    update_continuation_histories(ss, pos.moved_piece(move), move.to_sq(), bonus);

    // Update countermove history
    if (is_ok((ss - 1)->currentMove))
    {
        Square prevSq                                          = (ss - 1)->currentMove.to_sq();
        thisWorker->counterMoves[pos.piece_on(prevSq)][prevSq] = move;
    }

    // Update low ply history
    if (ss->ply < MAX_LPH)
        thisWorker->lowPlyHistory[ss->ply][move.from_to()] << stat_bonus(6);
}

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on "level". Idea by Heinz van Saanen.
Move Skill::pick_best(const RootMoves& rootMoves, size_t multiPV) {

    static PRNG rng(now());

    // RootMoves are already sorted by score in descending order
    Value  topScore = rootMoves[0].score;
    int    delta    = std::min(topScore - rootMoves[multiPV - 1].score, Value(PawnValue));
    int    maxScore = -VALUE_INFINITE;
    double weakness = 120 - 2 * level;

    // Choose best move. For each move score we add two terms, both dependent on weakness.
    // One is deterministic and bigger for weaker levels, and one is random. Then we choose
    // the move with the resulting highest score.
    for (size_t i = 0; i < multiPV; ++i)
    {
        // This is our magic formula
        int push = int((weakness * int(topScore - rootMoves[i].score)
                        + delta * (rng.rand<unsigned>() % int(weakness))) / 128);

        if (rootMoves[i].score + push >= maxScore)
        {
            maxScore = rootMoves[i].score + push;
            best     = rootMoves[i].pv[0];
        }
    }

    return best;
}

// Used to print debug info and, more importantly,
// to detect when we are out of available time and thus stop the search.
void SearchManager::check_time(Search::Worker& worker) {

    if (--callsCnt > 0)
        return;

    // When using nodes, ensure checking rate is not lower than 0.1% of nodes
    callsCnt = worker.limits.nodes ? std::min(512, int(worker.limits.nodes / 1024)) : 512;

    static TimePoint lastInfoTime = now();

    TimePoint elapsed = tm.elapsed(worker.threads);
    TimePoint tick    = worker.limits.startTime + elapsed;

    if (tick - lastInfoTime >= 1000)
    {
        lastInfoTime = tick;
        dbg_print();
    }

    // We should not stop pondering until told so by the GUI
    if (ponder)
        return;

    if ((worker.limits.use_time_management() && (elapsed > tm.maximum() || stopOnPonderhit))
        || (worker.limits.movetime && elapsed >= worker.limits.movetime)
        || (worker.limits.nodes && worker.threads.nodes_searched() >= worker.limits.nodes))
        worker.threads.stop = true;
}

// Formats PV information according to the UCI protocol. UCI requires
// that all (if any) unsearched PV lines are sent using a previous search score.
static std::string format_pv(const Search::Worker& worker, Depth depth) {

    std::stringstream ss;
    TimePoint         elapsed = worker.elapsed_time() + 1;
    const auto&       rootMoves = worker.rootMoves;
    size_t            pvIdx   = worker.pvIdx;
    size_t            multiPV = std::min(size_t(worker.options["MultiPV"]), rootMoves.size());
    uint64_t          nodesSearched = worker.threads.nodes_searched();
    uint64_t          tbHits = worker.threads.tb_hits();

    for (size_t i = 0; i < multiPV; ++i)
    {
        bool updated = rootMoves[i].score != -VALUE_INFINITE;

        if (depth == 1 && !updated && i > 0)
            continue;

        Depth d = updated ? depth : std::max(1, depth - 1);
        Value v = updated ? rootMoves[i].uciScore : rootMoves[i].previousScore;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = TB::RootInTB && std::abs(v) < VALUE_TB_WIN_IN_MAX_PLY;
        v       = tb ? rootMoves[i].tbScore : v;

        ss << "info"
           << " depth " << d
           << " seldepth " << rootMoves[i].selDepth
           << " multipv " << i + 1
           << " score " << UCIEngine::value(v, worker.rootPos);

        if (worker.options["UCI_ShowWDL"])
            ss << UCIEngine::wdl(v, worker.rootPos, d);

        if (!tb && updated && i == pvIdx)
            ss << (rootMoves[i].scoreLowerbound
                     ? " lowerbound"
                     : rootMoves[i].scoreUpperbound ? " upperbound" : "");

        ss << " nodes " << nodesSearched << " nps " << nodesSearched * 1000 / elapsed
           << " hashfull " << worker.tt.hashfull() << " tbhits " << tbHits << " time " << elapsed
           << " pv";

        for (Move m : rootMoves[i].pv)
            ss << " " << UCIEngine::move(m, worker.rootPos.is_chess960());
    }

    return ss.str();
}

// Called in case we have no ponder move
// before exiting the search, for instance, in case we stop the search during a
// fail high at root. We try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' we have nothing to think about.
bool RootMove::extract_ponder_from_tt(const TranspositionTable& tt, Position& pos) {

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    assert(pv.size() == 1);

    if (pv[0] == Move::none())
        return false;

    pos.do_move(pv[0], st);
    TTEntry* tte = tt.probe(pos.key());

    if (tte)
    {
        Move m = tte->move();  // Local copy to be SMP safe
        if (MoveList<LEGAL>(pos).contains(m))
            pv.push_back(m);
    }

    pos.undo_move(pv[0]);
    return pv.size() > 1;
}

// Called at the end of the search to print final search information.
void SearchManager::pv(Search::Worker&           worker,
                       const ThreadPool&         threads,
                       const TranspositionTable& tt,
                       Depth                     depth) {

    const auto& rootMoves = worker.rootMoves;
    const auto  nodes     = threads.nodes_searched();
    const auto  time      = worker.elapsed_time();

    std::string pv_info;
    Depth       selDepth = 0;
    Value       alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;

    // If depth 1 search returns 0 in multipv,
    // we should not print it.
    const bool multipv_sprt_cond = depth > 1 || rootMoves[0].uciScore != -VALUE_INFINITE;

    if (int(worker.options["MultiPV"]) == 1 && !worker.limits.depth && multipv_sprt_cond)
    {
        pv_info   = format_pv(worker, depth);
        selDepth = rootMoves[0].selDepth;
    }
    // In MultiPV mode we print all the search information.
    else if (multipv_sprt_cond)
    {
        pv_info   = format_pv(worker, depth);
        selDepth = rootMoves[0].selDepth;
        for (size_t i = 1; i < std::min(size_t(worker.options["MultiPV"]), rootMoves.size()); ++i)
            selDepth = std::max(selDepth, rootMoves[i].selDepth);

        alpha = -VALUE_INFINITE;
        beta  = VALUE_INFINITE;
    }

    const auto hashfull = tt.hashfull();
    const auto tbhits   = threads.tb_hits();
    const auto wdl      = worker.options["UCI_ShowWDL"] ? UCIEngine::wdl(rootMoves[0].uciScore, worker.rootPos, depth) : "";
    const auto bound = rootMoves[0].scoreLowerbound ? "lowerbound" : rootMoves[0].scoreUpperbound ? "upperbound" : "";

    updates.onUpdateFull(
      {depth, rootMoves[0].uciScore, selDepth, size_t(worker.options["MultiPV"]), wdl, bound,
       size_t(time), nodes, nodes * 1000ULL / (time + 1), tbhits, pv_info, int(hashfull)});
}

// Updates various search statistics.
static void update_all_stats(const Position& pos,
                             Stack*          ss,
                             Search::Worker& workerThread,
                             Move            bestMove,
                             Square          prevSq,
                             SearchedList&   quietsSearched,
                             SearchedList&   capturesSearched,
                             Depth           depth,
                             Move            ttMove) {

    Color   us         = pos.side_to_move();
    Worker* thisWorker = &workerThread;

    CapturePieceToHistory& captureHistory = thisWorker->captureHistory;
    Piece                  moved          = pos.moved_piece(bestMove);
    PieceType              captured       = type_of(pos.piece_on(bestMove.to_sq()));
    int                    bonus1         = stat_bonus(depth + 1);

    if (!pos.capture_stage(bestMove))
    {
        int bonus2 = bestMove == ttMove ? bonus1               // Extra bonus for ttMove
                                        : stat_bonus(depth);   // Base bonus

        // Increase stats for the best move in case it was a quiet move
        update_quiet_histories(pos, ss, *thisWorker, bestMove, bonus2);

        // Decrease stats for all non-best quiet moves
        for (Move m : quietsSearched)
        {
            thisWorker->mainHistory[us][m.from_to()] << -bonus2 / 32;
            update_continuation_histories(ss, pos.moved_piece(m), m.to_sq(), -bonus2 / 32);
        }
    }
    else
    {
        // Increase stats for the best move in case it was a capture move
        captureHistory[moved][bestMove.to_sq()][captured] << bonus1;
    }

    // Extra penalty for a quiet early move that was not a TT move or
    // main killer move in previous ply when it gets refuted.
    if (prevSq != SQ_NONE
        && ((ss - 1)->moveCount == 1 + (ss - 1)->ttHit
            || ((ss - 1)->currentMove == (ss - 1)->killers[0]))
        && !pos.captured_piece())
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq, -bonus1);

    // Decrease stats for all non-best capture moves
    for (Move m : capturesSearched)
        captureHistory[pos.moved_piece(m)][m.to_sq()][type_of(pos.piece_on(m.to_sq()))] << -bonus1 / 16;
}

TimePoint Search::Worker::elapsed() const {
    return main_manager()->tm.elapsed(threads);
}

TimePoint Search::Worker::elapsed_time() const {
    return elapsed() + main_manager()->tm.elapsed_time_at_node_zero();
}

Value Search::Worker::evaluate(const Position& pos) {
    return Eval::evaluate(networks[numaAccessToken], pos, accumulatorStack, refreshTable,
                          optimism[pos.side_to_move()]);
}

int Search::Worker::reduction(bool i, Depth d, int mn, int delta) const {
    int reductionScale = reductions[d] * reductions[mn];
    return (reductionScale + 1346 - int(delta) * 896 / int(rootDelta)) / 1024 + (!i && reductionScale > 808);
}

void Search::Worker::clear() {

    counterMoves.fill(Move::none());
    mainHistory.fill(0);
    captureHistory.fill(0);
    lowPlyHistory.fill(0);

    for (bool inCheck : {false, true})
        for (StatsType c : {NoCaptures, Captures})
            for (auto& to : continuationHistory[inCheck][c])
                for (auto& h : to)
                    h->fill(-71);
    continuationCorrectionHistory.fill(0);

    for (size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = int((20.26 + std::log(double(i)) * std::log(double(i)) * 2.00) * int(SEARCH_STACK_SPLIT));
}

}  // namespace Stockfish