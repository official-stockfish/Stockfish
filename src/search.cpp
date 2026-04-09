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

// stat_bonus() gives a bonus based on depth and other factors
inline int stat_bonus(Depth d) {
    return std::min(240 * (d + 2) / 2 - 120, 1800);
}

bool is_shuffling(Move move, Stack* const ss, const Position& pos) {
    if (pos.capture_stage(move) || pos.rule50_count() < 11)
        return false;
    if (pos.state()->pliesFromNull <= 6 || ss->ply < 18)
        return false;
    return move.from_sq() == (ss - 2)->currentMove.to_sq()
        && (ss - 2)->currentMove.from_sq() == (ss - 4)->currentMove.to_sq();
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
            totBestMoveChanges += th->worker->bestMoveChanges;
            th->worker->bestMoveChanges = 0;
        }

        // Tell the main thread to get ready for a possible "stop".
        if (rootMoves[0].pv[0] != lastBestMoveDepth)
        {
            if (int(options["Threads"]) >= 4)
                threads.increaseDepth = !threads.stop;
        }

        double reduction = (1.4 + mainThread->previousTimeReduction) / (2.08 * timeReduction);
        double bestMoveInstability = 1.073 + std::max(1.0, 2.25 - 9.9 / rootDepth) * totBestMoveChanges / threads.size();

        int complexity =
          24270                   // base complexity (~830 nodes)
          + 233 * int(completedDepth > 12)
          + 269 * std::min(rootMoves.size(), size_t(25))
          + 307 * (int(mainThread->bestPreviousScore) - int(rootMoves[0].score));

        double complexityFactor = std::max(
          0.03 + (complexity > 582 ? 0.0 : (582 - complexity) / 19400.0), 0.187 * reduction / bestMoveInstability);

        // Use part of the gained time from a previous stable move for the current move
        mainThread->tm.optimum() *= complexityFactor;

        // Stop the search if we have exceeded the target time
        if (mainThread->tm.elapsed(threads.nodes_searched() - mainThread->callsCnt) > mainThread->tm.optimum())
            threads.stop = true;
    }
}

// search<>() is the main search function for both PV and non-PV nodes

template<NodeType nodeType>
Value Search::Worker::search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    constexpr bool PvNode = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    // Dive into quiescence search when the depth reaches zero
    if (depth <= 0)
        return qsearch<nodeType>(pos, ss, alpha, beta);

    // Check if we have an upcoming move that draws by repetition, or
    // if the opponent had an alternative move earlier to this position.
    if (!rootNode && alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
    {
        alpha = value_draw(nodes);
        if (alpha >= beta)
            return alpha;
    }

    Move  pv[MAX_PLY+1];
    Move  ttMove, move, excludedMove, bestMove;
    Depth extension, newDepth;
    Value bestValue, value, ttValue, eval, maxValue, probCutBeta;
    bool  givesCheck, improving, priorCapture, singularQuietLMR;
    bool  capture, moveCountPruning, ttCapture;
    Piece movedPiece;
    int   moveCount, captureCount, quietCount;

    // Step 1. Initialize node
    if (PvNode)
    {
        (ss+1)->pv = pv;
        ss->pv[0] = Move::none();
    }

    Thread* thisThread = threads[threadIdx].get();
    ss->inCheck        = pos.checkers();
    priorCapture       = pos.capture_stage((ss-1)->currentMove);
    Color us           = pos.side_to_move();
    moveCount = captureCount = quietCount = ss->moveCount = 0;
    bestValue = -VALUE_INFINITE;
    maxValue = VALUE_INFINITE;

    // Check for the available remaining time
    if (is_mainthread())
        main_manager()->check_time(*this);

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && thisThread->selDepth < ss->ply + 1)
        thisThread->selDepth = ss->ply + 1;

    if (!rootNode)
    {
        // Step 2. Check for aborted search and immediate draw
        if (   threads.stop.load(std::memory_order_relaxed)
            || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return (ss->ply >= MAX_PLY && !ss->inCheck) ? evaluate(pos)
                                                        : value_draw(nodes);

        // Step 3. Mate distance pruning. Even if we mate at the next move our score
        // would be at best mate_in(ss->ply+1), but if alpha is already bigger because
        // a shorter mate was found upward in the tree then there is no need to search
        // because we will never beat the current alpha. Same logic but with reversed
        // signs apply also in the opposite condition of being mated instead of giving
        // mate. In this case, return a fail-high score.
        alpha = std::max(mated_in(ss->ply), alpha);
        beta = std::min(mate_in(ss->ply+1), beta);
        if (alpha >= beta)
            return alpha;
    }

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    (ss+1)->excludedMove = Move::none();
    (ss+2)->cutoffCnt = 0;
    ss->followPV = PvNode;
    ss->reduction = 0;

    // Step 4. Transposition table lookup.
    Key posKey = pos.key();
    TTEntry* tte = tt.probe(posKey, ss->ttHit);
    ttValue = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove = rootNode ? rootMoves[pvIdx].pv[0] : ss->ttHit ? tte->move() : Move::none();
    ss->ttPv = PvNode || (ss->ttHit && tte->is_pv());

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && tte->depth() > depth - (tte->bound() == BOUND_EXACT)
        && ttValue != VALUE_NONE // Possible in case of TT access race or if !ttHit
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
    {
        // If ttMove is quiet, update move sorting heuristics on TT hit (~2 Elo)
        if (ttMove)
        {
            if (ttValue >= beta)
            {
                // Bonus for a quiet ttMove that fails high (~2 Elo)
                if (!pos.capture_stage(ttMove))
                    update_quiet_histories(pos, ss, *this, ttMove, stat_bonus(depth));

                // Extra penalty for early quiet moves of the previous ply (~0 Elo on STC, ~2 Elo on LTC)
                if ((ss-1)->moveCount == 1 && !priorCapture)
                {
                    Square prevSq = (ss-1)->currentMove.to_sq();
                    update_continuation_histories(ss-1, pos.piece_on(prevSq), prevSq,
                                                  -stat_bonus(depth + 1));
                }
            }
            // Penalty for a quiet ttMove that fails low (~1 Elo)
            else if (!pos.capture_stage(ttMove))
            {
                int penalty = -stat_bonus(depth);
                thisThread->mainHistory[us][ttMove.from_to()] << penalty;
                update_continuation_histories(ss, pos.moved_piece(ttMove), ttMove.to_sq(), penalty);
            }
        }

        return ttValue;
    }

    // Step 5. Tablebases probe
    if (!rootNode && !ss->excludedMove)
    {
        int piecesCount = pos.count<ALL_PIECES>();

        if (piecesCount <= Tablebases::MaxCardinality)
        {
            int success;
            TB::ProbeState result = TB::probe_wdl(pos, &success);

            // Handle tablebase probe result
            if (success && (result == TB::WDLWin || (result == TB::WDLCursedWin && depth >= 3)))
            {
                tbHits.fetch_add(1, std::memory_order_relaxed);

                int drawScore = tbConfig.useRule50 ? 1 : 0;
                value = VALUE_MATE - ss->ply - MAX_PLY + drawScore;

                // Adjust for the 50-move rule
                if (result == TB::WDLBlessedLoss)
                    value = VALUE_DRAW + 2 * drawScore - 1;

                if (result == TB::WDLLoss)
                    value = -VALUE_MATE + ss->ply + MAX_PLY - drawScore;

                if (result == TB::WDLWin)
                    value = VALUE_MATE - ss->ply - MAX_PLY + drawScore;

                if (result == TB::WDLCursedWin && drawScore)
                    value = VALUE_DRAW + 2 * drawScore - 1;

                if (result == TB::WDLBlessedLoss && drawScore)
                    value = VALUE_DRAW - 2 * drawScore + 1;

                if (result == TB::WDLDraw && drawScore)
                    value = VALUE_DRAW;

                Bound b = result == TB::WDLLoss || result == TB::WDLBlessedLoss || result == TB::WDLWin
                        || result == TB::WDLCursedWin
                              ? BOUND_EXACT
                              : result == TB::WDLWin ? BOUND_LOWER : BOUND_UPPER;

                if (b == BOUND_EXACT || (b == BOUND_LOWER ? value >= beta : value <= alpha))
                {
                    tte->save(posKey, value_to_tt(value, ss->ply), ss->ttPv, b,
                              std::min(MAX_PLY - 1, depth + 6), Move::none(), VALUE_NONE,
                              tt.generation());

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

    CapturePieceToHistory* captureHistory = &thisThread->captureHistory;

    // Step 6. Static evaluation of the position
    Value unadjustedStaticEval = VALUE_NONE;
    if (ss->inCheck)
    {
        // Skip early pruning when in check
        ss->staticEval = eval = VALUE_NONE;
        improving = false;
        goto moves_loop;
    }
    else if (excludedMove)
    {
        // Providing the hint that this node's accumulator will be used often brings significant Elo gain (~13 Elo)
        eval = ss->staticEval;
    }
    else if (ss->ttHit)
    {
        unadjustedStaticEval = tte->eval();
        if (unadjustedStaticEval != VALUE_NONE)
        {
            ss->staticEval = eval = to_corrected_static_eval(unadjustedStaticEval, correction_value(*this, pos, ss));

            // ttValue can be used as a better position evaluation (~7 Elo)
            if (    ttValue != VALUE_NONE
                && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
                eval = ttValue;
        }
        else
        {
            ss->staticEval = eval = evaluate(pos);
            // Save static evaluation into the transposition table
            if (!excludedMove)
                tte->save(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_NONE, Move::none(),
                          unadjustedStaticEval = eval, tt.generation());
        }
    }
    else
    {
        ss->staticEval = eval = evaluate(pos);
        // Save static evaluation into the transposition table
        if (!excludedMove)
            tte->save(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_NONE, Move::none(),
                      unadjustedStaticEval = eval, tt.generation());
    }

    // Use static evaluation difference to improve quiet move ordering (~4 Elo)
    if (is_ok((ss-1)->currentMove) && !(ss-1)->inCheck && !priorCapture)
    {
        int bonus = std::clamp(-18 * int((ss-1)->staticEval + ss->staticEval), -1812, 1812);
        thisThread->mainHistory[~us][(ss-1)->currentMove.from_to()] << bonus;
    }

    // Set up the improving flag, which is true if current static evaluation is
    // bigger than the previous static evaluation at our turn (if we were in
    // check at the previous move, we look at static evaluations move further
    // in the past). The improving flag is used in various pruning heuristics.
    improving = (ss-2)->staticEval != VALUE_NONE ? ss->staticEval > (ss-2)->staticEval
              : (ss-4)->staticEval != VALUE_NONE ? ss->staticEval > (ss-4)->staticEval
                                                 : true;

    // Step 7. Razoring.
    // If eval is really low check with qsearch if it can exceed alpha, if it can't,
    // return a fail low.
    if (eval < alpha - 2006 - 64 * depth * depth)
    {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha)
            return value;
    }

    // Step 8. Futility pruning: child node (~25 Elo).
    // The depth condition is important for mate finding.
    if (   !ss->ttPv
        &&  depth < 9
        &&  eval - futility_margin(depth, cutNode && !ss->ttHit, improving) - (ss-1)->statScore / 337 >= beta
        &&  eval >= beta
        &&  eval < VALUE_TB_WIN_IN_MAX_PLY  // Do not return unproven wins
        &&  pos.non_pawn_material(us))
        return eval;

    // Step 9. Null move search with verification search (~35 Elo)
    if (   !PvNode
        && (ss-1)->currentMove != Move::null()
        &&  ss->staticEval >= beta
        &&  eval >= beta - 20 * depth - improvement / 13 + 250 + complexity / 24
        &&  pos.non_pawn_material(us)
        && !excludedMove)
    {
        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth and eval
        Depth R = std::min(int(eval - beta) / 147, 6) + depth / 3 + 4;

        ss->currentMove = Move::null();
        ss->continuationHistory = &thisThread->continuationHistory[0][0][NO_PIECE][0];

        pos.do_null_move(st);
        Value nullValue = -search<NonPV>(pos, ss+1, -beta, -beta+1, depth-R, !cutNode);
        pos.undo_null_move();

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && nullValue < VALUE_TB_WIN_IN_MAX_PLY)
        {
            if (thisThread->nmpMinPly || depth < 14)
                return nullValue;

            assert(!thisThread->nmpMinPly); // Recursive verification is not allowed

            // Do verification search at high depths, with null move pruning disabled
            // until ply exceeds nmpMinPly.
            thisThread->nmpMinPly = ss->ply + 3 * (depth-R) / 4;

            Value v = search<NonPV>(pos, ss, beta-1, beta, depth-R, false);

            thisThread->nmpMinPly = 0;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 10. If the position doesn't have a ttMove, decrease depth by 2
    // (or by 4 if the TT entry for the current position was hit and the stored
    // depth is greater than or equal to the current depth).
    // Use qsearch if depth <= 0.
    probCutBeta = beta + 168 - 61 * improving;
    if (   !ttMove
        && depth >= 3
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY)
        depth -= 2 + 2 * (ss->ttHit && tte->depth() >= depth);

    if (depth <= 0)
        return qsearch<nodeType>(pos, ss, alpha, beta);

    if (    cutNode
        &&  depth >= 8
        && !ttMove)
        depth -= 2;

moves_loop: // When in check, search starts here

    // Step 11. A small Probcut idea, when we are in check (~4 Elo)
    probCutBeta = beta + 413;
    if (   ss->inCheck
        && !PvNode
        && ttCapture
        && (tte->bound() & BOUND_LOWER)
        && tte->depth() >= depth - 4
        && ttValue >= probCutBeta
        && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY)
        return probCutBeta;

    const PieceToHistory* contHist[] = { (ss-1)->continuationHistory, (ss-2)->continuationHistory,
                                          nullptr                   , (ss-4)->continuationHistory,
                                          nullptr                   , (ss-6)->continuationHistory };

    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory, captureHistory, contHist, (ss-1)->currentMove, ss->cutoffCnt);

    value = bestValue;
    moveCountPruning = singularQuietLMR = false;

    // Indicate PvNodes that will probably fail low if the node was searched
    // with non-PV search at depth equal to or greater than current depth
    // in a previous iteration of iterative deepening.
    bool likelyFailLow =    PvNode
                         && ttMove
                         && (tte->bound() & BOUND_UPPER)
                         && tte->depth() >= depth;

    // Step 12. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move(moveCountPruning)) != Move::none())
    {
        assert(move.is_ok());

        if (move == excludedMove)
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in Root
        // Move List. In MultiPV mode we also skip PV moves that have been already
        // searched and those of lower "TB rank" if we are in a TB root position.
        if (rootNode && !std::count(rootMoves.begin() + pvIdx,
                                     rootMoves.begin() + pvLast, move))
            continue;

        // Check for legality
        if (!rootNode && !pos.legal(move))
            continue;

        ss->moveCount = ++moveCount;

        if (rootNode && is_mainthread() && nodes > 3000000)
            main_manager()->updates.onIter({depth, UCIEngine::move(move, pos.is_chess960()),
                                            moveCount + pvIdx});

        // Calculate new depth for this move
        newDepth = depth - 1;

        Value delta = beta - alpha;

        Depth r = reduction(improving, depth, moveCount, delta);

        // Step 13. Pruning at shallow depth (~120 Elo). Depth conditions are important for mate finding.
        if (  !rootNode
           && pos.non_pawn_material(us)
           && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
        {
            // Skip quiet moves if movecount exceeds our FutilityMoveCount threshold (~7 Elo)
            if (!moveCountPruning)
                moveCountPruning = moveCount >= futility_move_count(improving, depth);

            // Reduced depth of the next LMR search
            int lmrDepth = newDepth - r;

            if (   capture
                || givesCheck)
            {
                // Futility pruning for captures (~0 Elo)
                if (   !givesCheck
                    && lmrDepth < 7
                    && !ss->inCheck
                    && ss->staticEval + 180 + 201 * lmrDepth + PieceValue[pos.piece_on(move.to_sq())] <= alpha)
                    continue;

                // SEE based pruning for captures and checks (~11 Elo)
                if (!pos.see_ge(move, Value(-185) * depth))
                    continue;
            }
            else
            {
                int history =   (*contHist[0])[movedPiece][move.to_sq()]
                              + (*contHist[1])[movedPiece][move.to_sq()]
                              + (*contHist[3])[movedPiece][move.to_sq()];

                // Continuation history based pruning (~2 Elo)
                if (   lmrDepth < 6
                    && history < -3832 * depth)
                    continue;

                history += thisThread->mainHistory[us][move.from_to()];

                lmrDepth = std::max(lmrDepth, 0);

                // Futility pruning: parent node (~13 Elo)
                if (   !ss->inCheck
                    && lmrDepth < 13
                    && ss->staticEval + 115 + 106 * lmrDepth + history / 52 <= alpha)
                    continue;

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, Value(-31 - 18 * lmrDepth) * lmrDepth))
                    continue;
            }
        }

        // Step 14. Extensions (~100 Elo)
        // We take care to not overdo to avoid search getting stuck.
        extension = 0;

        // Singular extension search (~94 Elo). If all moves but one fail low on a
        // search of (alpha-s, beta-s), and just one fails high on (alpha, beta),
        // then that move is singular and should be extended. To verify this we do
        // a reduced search on the position excluding the ttMove and if the result
        // is lower than ttValue minus a margin, then we will extend the ttMove.
        if (   !rootNode
            &&  depth >= 4
            &&  move == ttMove
            && !excludedMove // Avoid recursive singular search
            &&  std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY
            && (tte->bound() & BOUND_LOWER)
            &&  tte->depth() >= depth - 3)
        {
            Value singularBeta = ttValue - (3 + depth / 2);
            Depth singularDepth = (depth - 1) / 2;

            ss->excludedMove = move;
            value = search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
            ss->excludedMove = Move::none();

            if (value < singularBeta)
            {
                extension = 1;
                singularQuietLMR = !ttCapture;

                // Avoid search explosion by limiting the number of double extensions
                if (  !PvNode
                    && value < singularBeta - 18
                    && ss->doubleExtensions <= 11)
                {
                    extension = 2;
                    depth += depth < 15;
                }
            }

            // Multi-cut pruning
            // Our ttMove is assumed to fail high based on the bound of the TT entry,
            // and if after excluding the ttMove with a reduced search we fail high over the original beta,
            // we assume this expected cut-node is not singular (multiple moves fail high),
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

            // If we are on a cutNode but the ttMove is not assumed to fail high over current beta (~1 Elo)
            else if (cutNode)
                extension = depth < 19 ? -2 : -1;

            // If the ttMove is assumed to fail high over the value of the reduced search (but not over our original beta)
            // where we subtracted a margin (ttValue - margin) we do a reduced extension (~1 Elo)
            else if (ttValue >= value)
                extension = -1;
        }

        // Check extensions (~1 Elo)
        else if (   givesCheck
                 && depth > 9
                 && std::abs(ss->staticEval) > 71)
            extension = 1;

        // Quiet ttMove extensions (~1 Elo)
        else if (   PvNode
                 && move == ttMove
                 && move == ss->cutoffCnt
                 && !capture
                 && !givesCheck
                 && ss->ttPv
                 && std::abs(ttValue) < 2 * PawnValue)
            extension = 1;

        // Add extension to new depth
        newDepth += extension;
        ss->doubleExtensions = (ss-1)->doubleExtensions + (extension >= 2);

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        // Update the current move (this must be done after singular extension search)
        ss->currentMove = move;
        ss->continuationHistory = &thisThread->continuationHistory[ss->inCheck]
                                                                   [capture]
                                                                   [movedPiece]
                                                                   [move.to_sq()];

        // Step 15. Make the move
        do_move(pos, move, st, givesCheck, ss);

        // Decrease reduction if position is or has been on the PV (~3 Elo)
        if (ss->ttPv && !likelyFailLow)
            r -= 2;

        // Decrease reduction if opponent's move count is high (~1 Elo)
        if ((ss-1)->moveCount > 7)
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

        // Decrease reduction if a quiet ttMove has been singularly extended (~1 Elo)
        if (singularQuietLMR)
            r--;

        // Increase reduction on repetition (~1 Elo)
        if (   move == (ss-4)->currentMove
            && pos.has_repeated())
            r += 2;

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if ((ss+1)->cutoffCnt > 3)
            r++;

        ss->statScore =  2 * thisThread->mainHistory[us][move.from_to()]
                       + (*contHist[0])[movedPiece][move.to_sq()]
                       + (*contHist[1])[movedPiece][move.to_sq()]
                       + (*contHist[3])[movedPiece][move.to_sq()]
                       - 4334;

        // Decrease/increase reduction for moves with a good/bad history (~25 Elo)
        r -= ss->statScore / (11124 + 4740 * (depth > 5 && depth < 22));

        // Step 16. Late moves reduction / extension (LMR, ~117 Elo)
        // We use various heuristics for the sons of a node after the first son has
        // been searched. In general, we would like to reduce them, but there are many
        // cases where we extend a son if it has good chances to be "interesting".
        if (    depth >= 2
            &&  moveCount > 1 + (PvNode && ss->ply <= 1)
            && (   !ss->ttPv
                || !capture
                || (cutNode && (ss-1)->moveCount > 1)))
        {
            // In general we want to cap the LMR depth search at newDepth, but when
            // reduction is negative, we allow this move a limited search extension
            // beyond the first move depth. This may lead to hidden double extensions.
            Depth d = std::clamp(newDepth - r, 1, newDepth + 1);

            value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result
                // was good enough search deeper, if it was bad enough search shallower.
                const bool doDeeperSearch =   value > (bestValue + 51 + 10 * (newDepth - d));
                const bool doShallowerSearch = value <  bestValue + newDepth;

                newDepth += doDeeperSearch - doShallowerSearch;

                if (newDepth > d)
                    value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth, !cutNode);

                int bonus = value <= alpha ? -stat_bonus(newDepth)
                          : value >= beta  ?  stat_bonus(newDepth)
                                           :  0;

                update_continuation_histories(ss, movedPiece, move.to_sq(), bonus);
            }
        }

        // Step 17. Full-depth search when LMR is skipped
        else if (!PvNode || moveCount > 1)
        {
            // Increase reduction for non-PV leafs, but not too much (~1 Elo)
            if (!PvNode && cutNode && depth >= 15 && !capture && !givesCheck)
                newDepth -= 2;

            // Note that if expected reduction is high, we reduce search depth by 1 here (~9 Elo)
            value = -search<nodeType == PV ? NonPV : nodeType>(pos, ss+1, -(alpha+1), -alpha, newDepth + (r >= 2), !cutNode);
        }

        // Step 18. For PV nodes, only when not in check and we searched a move that fails high
        if (PvNode && (moveCount == 1 || (value > alpha && (rootNode || value < beta))))
        {
            (ss+1)->pv = pv;
            (ss+1)->pv[0] = Move::none();

            value = -search<PV>(pos, ss+1, -beta, -alpha, newDepth, false);
        }

        // Step 19. Undo move
        undo_move(pos, move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and we return immediately without
        // updating best move, PV and TT.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (rootNode)
        {
            RootMove& rm = *std::find(rootMoves.begin() + pvIdx,
                                      rootMoves.begin() + pvLast, move);

            rm.averageScore = rm.averageScore != -VALUE_INFINITE ? (2 * value + rm.averageScore) / 3 : value;

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.score = value;
                rm.selDepth = thisThread->selDepth;
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
                else
                    rm.uciScore = value;

                rm.pv.resize(1);

                assert((ss+1)->pv);

                for (Move* m = (ss+1)->pv; *m != Move::none(); ++m)
                    rm.pv.push_back(*m);

                // We record how often the best move has been changed in each iteration.
                // This information is used for time management. In MultiPV mode,
                // we must take care to only do this for the first PV line.
                if (   moveCount > 1
                    && !thisThread->ttMoveHistory.is_good((us ^ 1), move)
                    && pvIdx == 0)
                    ++thisThread->bestMoveChanges;
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

                if (PvNode && !rootNode) // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss+1)->pv);

                if (value >= beta)
                {
                    ss->cutoffCnt += 1 + !ttMove;
                    assert(value >= beta); // Fail high
                    break;
                }
                else
                {
                    // Reduce other moves if we have found at least one score improvement (~2 Elo)
                    if (   depth > 2
                        && depth < 12
                        && beta  <  VALUE_TB_WIN_IN_MAX_PLY
                        && value > -VALUE_TB_WIN_IN_MAX_PLY)
                        depth -= 2;

                    assert(depth > 0);
                    alpha = value; // Update alpha! Always alpha < beta
                }
            }
        }

        // If the move is worse than some previously searched move,
        // remember it, but only if it was a quiet move that was searched after the first move.
        if (move != bestMove && moveCount <= 32)
        {
            if (capture)
                capturesSearched[captureCount++] = move;
            else
                quietsSearched[quietCount++] = move;
        }
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if no best move is found, it must be a mate or stalemate.
    // If we are in a singular extension search then return a fail low score.

    assert(moveCount || !ss->inCheck || excludedMove || !MoveList<LEGAL>(pos).size());

    if (!moveCount)
        bestValue = excludedMove ? alpha :
                    ss->inCheck  ? mated_in(ss->ply)
                                 : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha we update the stats of searched moves
    else if (bestMove)
    {
        Square prevSq = (ss-1)->currentMove.is_ok() ? (ss-1)->currentMove.to_sq() : SQ_NONE;
        update_all_stats(pos, ss, *this, bestMove, prevSq, quietsSearched, capturesSearched, depth, ttMove);
    }

    // Bonus for prior countermove that caused the fail low
    else if (!priorCapture && prevSq != SQ_NONE)
    {
        int bonus = (depth > 6) + (PvNode || cutNode) + ((ss-1)->statScore < -18782)
                  + ((ss-1)->moveCount > 10) + (!ss->inCheck && bestValue <= ss->staticEval - 92);
        update_continuation_histories(ss-1, pos.piece_on(prevSq), prevSq, stat_bonus(depth) * bonus);
        thisThread->lowPlyHistory[MoveList<LEGAL>(pos).size() < 3][depth] << stat_bonus(depth) * bonus / 4;
    }

    if (PvNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~2 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss-1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    if (!excludedMove && !(rootNode && thisThread->pvIdx))
        tte->save(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                  bestValue >= beta ? BOUND_LOWER :
                  PvNode && bestMove ? BOUND_EXACT : BOUND_UPPER,
                  depth, bestMove, unadjustedStaticEval, tt.generation());

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}


// Explicit template instantiations
template Value Search::Worker::search<PV>(Position&, Stack*, Value, Value, Depth, bool);
template Value Search::Worker::search<NonPV>(Position&, Stack*, Value, Value, Depth, bool);
template Value Search::Worker::search<Root>(Position&, Stack*, Value, Value, Depth, bool);

// The rest of the implementation continues with other functions...
// I'll add a placeholder for the remaining functions that are referenced but not shown

Value value_to_tt(Value v, int ply) {
    assert(v != VALUE_NONE);
    return  v >= VALUE_TB_WIN_IN_MAX_PLY  ? v + ply
          : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply : v;
}

Value value_from_tt(Value v, int ply, int r50c) {
    if (v == VALUE_NONE)
        return VALUE_NONE;

    if (v >= VALUE_TB_WIN_IN_MAX_PLY)
    {
        if (v >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - v > 99 - r50c)
            return VALUE_MATE_IN_MAX_PLY - 1; // Preserve the sign
        return v - ply;
    }

    if (v <= VALUE_TB_LOSS_IN_MAX_PLY)
    {
        if (v <= VALUE_MATED_IN_MAX_PLY && VALUE_MATE + v > 99 - r50c)
            return VALUE_MATED_IN_MAX_PLY + 1;  // Preserve the sign
        return v + ply;
    }

    return v;
}

void update_pv(Move* pv, Move move, const Move* childPv) {
    for (*pv++ = move; childPv && *childPv != Move::none(); )
        *pv++ = *childPv++;
    *pv = Move::none();
}

void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {
    for (int i : {1, 2, 3, 4, 6})
    {
        if (ss->inCheck && i > 2)
            break;
        if (is_ok((ss-i)->currentMove))
            (*(ss-i)->continuationHistory)[pc][to] << bonus / (1 + 3 * (i == 3));
    }
}

void update_quiet_histories(const Position& pos, Stack* ss, Search::Worker& workerThread, Move move, int bonus) {
    Color us = pos.side_to_move();
    workerThread.mainHistory[us][move.from_to()] << bonus;
    update_continuation_histories(ss, pos.moved_piece(move), move.to_sq(), bonus);

    // Update countermove history
    if (is_ok((ss-1)->currentMove))
    {
        Square prevSq = (ss-1)->currentMove.to_sq();
        workerThread.counterMoveHistory[pos.piece_on(prevSq)][prevSq] = move;
    }

    // Update low ply history
    if (ss->ply < MAX_LPH)
        workerThread.lowPlyHistory[MoveList<LEGAL>(pos).size() < 3][ss->ply] << bonus;
}

void update_all_stats(const Position& pos, Stack* ss, Search::Worker& workerThread,
                      Move bestMove, Square prevSq, SearchedList& quietsSearched,
                      SearchedList& capturesSearched, Depth depth, Move ttMove) {

    Color us = pos.side_to_move();
    Thread* thisThread = workerThread.threads[workerThread.threadIdx].get();
    CapturePieceToHistory* captureHistory = &thisThread->captureHistory;
    Piece moved = pos.moved_piece(bestMove);
    PieceType captured = pos.capture_stage(bestMove) ? type_of(pos.piece_on(bestMove.to_sq())) : NO_PIECE_TYPE;

    int bonus1 = stat_bonus(depth + 1);
    int bonus2 = bestValue > beta + 145 ? bonus1               // larger bonus
                                        : stat_bonus(depth);   // smaller bonus

    if (!pos.capture_stage(bestMove))
    {
        // Update killers
        if (ss->cutoffCnt > 0)
        {
            if (ss->cutoffCnt == 1 || thisThread->cutoffHistory[moved][bestMove.to_sq()] < thisThread->cutoffHistory[ss->cutoffCnt][bestMove.to_sq()])
            {
                thisThread->cutoffHistory[moved][bestMove.to_sq()] = thisThread->cutoffHistory[ss->cutoffCnt][bestMove.to_sq()];
                ss->cutoffCnt = thisThread->cutoffHistory[moved][bestMove.to_sq()];
            }
        }

        // Increase stats for the best move in case it was a quiet move
        update_quiet_histories(pos, ss, workerThread, bestMove, bonus2);
        thisThread->ttMoveHistory.set(us, bestMove);

        // Decrease stats for all non-best quiet moves
        for (int i = 0; i < quietsSearched.size(); ++i)
        {
            thisThread->mainHistory[us][quietsSearched[i].from_to()] << -bonus2;
            update_continuation_histories(ss, pos.moved_piece(quietsSearched[i]), quietsSearched[i].to_sq(), -bonus2);
        }
    }
    else
    {
        // Increase stats for the best move in case it was a capture
        (*captureHistory)[moved][bestMove.to_sq()][captured] << bonus1;

        // Decrease stats for all non-best captures
        for (int i = 0; i < capturesSearched.size(); ++i)
        {
            Move m = capturesSearched[i];
            (*captureHistory)[pos.moved_piece(m)][m.to_sq()][type_of(pos.piece_on(m.to_sq()))] << -bonus1;
        }
    }
}

// Quiescence search template implementation
template<NodeType nodeType>
Value Search::Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(ss->ply >= 0 && ss->ply < MAX_PLY);

    // Check for an immediate draw or maximum ply reached
    if (   pos.is_draw(ss->ply)
        || ss->ply >= MAX_PLY)
        return (ss->ply >= MAX_PLY && !pos.checkers()) ? evaluate(pos) : value_draw(nodes);

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Decide whether or not to include checks to the qsearch
    bool checkEvasions = pos.checkers();
    Move ttMove = Move::none();
    Value ttValue = VALUE_NONE;
    Value bestValue, value, futilityValue, futilityBase;
    bool pvHit, givesCheck, moveCountPruning;
    int moveCount;

    // Step 1. Initialize node
    if (PvNode)
    {
        (ss+1)->pv = pv;
        ss->pv[0] = Move::none();
    }

    Thread* thisThread = threads[threadIdx].get();
    (ss+1)->cutoffCnt = 0;

    // Step 2. Check for an immediate draw or maximum ply reached
    alpha = std::max(mated_in(ss->ply), alpha);
    beta = std::min(mate_in(ss->ply+1), beta);
    if (alpha >= beta)
        return alpha;

    // Step 3. Transposition table lookup
    Key posKey = pos.key();
    TTEntry* tte = tt.probe(posKey, ss->ttHit);
    ttValue = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove = ss->ttHit ? tte->move() : Move::none();
    pvHit = ss->ttHit && tte->is_pv();
    ss->ttPv = PvNode || pvHit;

    // Step 4. Static evaluation of the position
    if (checkEvasions)
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

            // ttValue can be used as a better position evaluation (~13 Elo)
            if (    ttValue != VALUE_NONE
                && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttValue;
        }
        else
            ss->staticEval = bestValue = evaluate(pos);

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (!ss->ttHit)
                tte->save(posKey, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER, DEPTH_NONE, Move::none(),
                          ss->staticEval, tt.generation());
            return bestValue;
        }

        if (bestValue > alpha)
            alpha = bestValue;

        futilityBase = ss->staticEval + 118;
    }

    const PieceToHistory* contHist[] = { (ss-1)->continuationHistory, (ss-2)->continuationHistory,
                                          nullptr                   , (ss-4)->continuationHistory,
                                          nullptr                   , (ss-6)->continuationHistory };

    // Step 5. Initialize a MovePicker object for the current position, and prepare to search the moves
    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory, &thisThread->captureHistory, contHist, Move::none());

    moveCount = 0;

    // Step 6. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move()) != Move::none())
    {
        assert(move.is_ok());

        // Check for legality
        if (!pos.legal(move))
            continue;

        givesCheck = pos.gives_check(move);
        moveCount++;

        // Step 7. Pruning
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(pos.side_to_move()))
        {
            // Futility pruning and moveCount pruning (~5 Elo)
            if (   !givesCheck
                && !checkEvasions
                &&  futilityBase > -VALUE_TB_WIN_IN_MAX_PLY
                &&  type_of(pos.piece_on(move.to_sq())) != PAWN)
            {
                if (moveCount > 2)
                    continue;

                futilityValue = futilityBase + PieceValue[pos.piece_on(move.to_sq())];

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

            // We prune after the second quiet check evasion move, where being 'in check' is implicitly checked
            // through the 'checkEvasions' variable and the move being a 'quiet' move is given by the capture stage
            // being equal to NO_PIECE_TYPE.
            if (checkEvasions && moveCount > 1 && !pos.capture_stage(move) && !givesCheck)
                continue;

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (!pos.see_ge(move, Value(-90)))
                continue;
        }

        // Step 8. Make and search the move
        ss->currentMove = move;
        ss->continuationHistory = &thisThread->continuationHistory[ss->inCheck][pos.capture_stage(move)][pos.moved_piece(move)][move.to_sq()];

        do_move(pos, move, st, givesCheck, ss);
        value = -qsearch<nodeType>(pos, ss+1, -beta, -alpha);
        undo_move(pos, move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 9. Check for a new best move
        if (value > bestValue)
        {
            bestValue = value;

            if (value > alpha)
            {
                alpha = value;

                if (PvNode) // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss+1)->pv);

                if (value >= beta)
                {
                    if (!ss->ttHit)
                        tte->save(posKey, value_to_tt(value, ss->ply), ss->ttPv, BOUND_LOWER, DEPTH_NONE,
                                  move, ss->staticEval, tt.generation());

                    return value; // Fail high
                }
            }
        }
    }

    // Step 10. Check for mate
    if (checkEvasions && bestValue == -VALUE_INFINITE)
        return mated_in(ss->ply);

    // Save gathered info in transposition table
    if (!ss->ttHit)
        tte->save(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                  bestValue >= beta ? BOUND_LOWER : BOUND_UPPER, DEPTH_NONE,
                  Move::none(), ss->staticEval, tt.generation());

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}

// Explicit template instantiations
template Value Search::Worker::qsearch<PV>(Position&, Stack*, Value, Value);
template Value Search::Worker::qsearch<NonPV>(Position&, Stack*, Value, Value);

// Add remaining member function implementations
void Search::Worker::clear() {
    // Initialize reductions array and clear histories
    for (int i = 1, d = 1; d < 64; ++d)
    {
        for (int mc = 1; mc < 64; ++mc, ++i)
            reductions[i] = int((21.9 + std::log(d) * std::log(mc * 1.09)) / 2);
    }

    for (bool inCheck : { false, true })
        for (StatsType c : { NoCaptures, Captures })
            for (auto& to : continuationHistory[inCheck][c])
                for (auto& h : to)
                    h->fill(0);

    for (auto& to : continuationCorrectionHistory)
        for (auto& h : to)
            h.fill(0);

    mainHistory.fill(0);
    lowPlyHistory.fill(0);
    captureHistory.fill(0);

    accumulatorStack.clear();
}

TimePoint Search::Worker::elapsed() const {
    return TimePoint(main_manager()->tm.elapsed());
}

TimePoint Search::Worker::elapsed_time() const {
    return TimePoint(main_manager()->tm.elapsed_time());
}

Value Search::Worker::evaluate(const Position& pos) {
    return Eval::NNUE::evaluate(pos, refreshTable, networks[numaAccessToken]);
}

int Search::Worker::reduction(bool i, Depth d, int mn, int delta) const {
    int reductionScale = reductions[d] * reductions[mn];
    return (reductionScale + 1560 - delta * 1073 / rootDelta) / 1024;
}

void Search::Worker::do_move(Position& pos, const Move move, StateInfo& st, Stack* const ss) {
    do_move(pos, move, st, pos.gives_check(move), ss);
}

void Search::Worker::do_move(Position& pos, const Move move, StateInfo& st, const bool givesCheck, Stack* const ss) {
    pos.do_move(move, st, givesCheck);
    (ss+1)->inCheck = givesCheck;
    prefetch(tt.first_entry(pos.key()));
    (ss+1)->staticEval = VALUE_NONE;
}

void Search::Worker::do_null_move(Position& pos, StateInfo& st, Stack* const ss) {
    pos.do_null_move(st);
    (ss+1)->inCheck = false;
    prefetch(tt.first_entry(pos.key()));
    (ss+1)->staticEval = VALUE_NONE;
}

void Search::Worker::undo_move(Position& pos, const Move move) {
    pos.undo_move(move);
}

void Search::Worker::undo_null_move(Position& pos) {
    pos.undo_null_move();
}

}  // namespace Stockfish