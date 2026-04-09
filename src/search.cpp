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

// Check if the position is a KBNvK endgame (King + Bishop + Knight vs King)
// to prevent search explosion in these complex endgames
bool is_kbnvk_endgame(const Position& pos) {
    // Check if total pieces is exactly 4 (2 kings + 2 other pieces)
    if (popcount(pos.pieces()) != 4)
        return false;
    
    // Check both sides for KBN vs K pattern
    for (Color c : {WHITE, BLACK}) {
        if (pos.count<KING>(c) == 1 && pos.count<BISHOP>(c) == 1 && pos.count<KNIGHT>(c) == 1
            && pos.count<KING>(~c) == 1 && pos.count<ALL_PIECES>(~c) == 1) {
            return true;
        }
    }
    return false;
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

    // Check for KBNvK endgame to prevent search explosion
    bool isKBNvK = is_kbnvk_endgame(rootPos);
    const Depth kbnvkMaxDepth = 16;  // Limit depth for KBNvK endgames

    // Iterative deepening loop until requested to stop or the target depth is reached
    while (++rootDepth < MAX_PLY && !threads.stop
           && !(limits.depth && mainThread && rootDepth > limits.depth)
           && !(isKBNvK && rootDepth > kbnvkMaxDepth))  // Add KBNvK depth limit
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
            totBestMoveChanges += th->worker->bestMoveChanges;

        // Do we have time for the next iteration? Can we stop searching now?
        if (limits.use_time_management() && !threads.stop && !mainThread->stopOnPonderhit)
        {
            double fallingEval = (142 + 12 * (mainThread->bestPreviousAverageScore - bestValue)
                                       + 6 * (mainThread->iterValue[iterIdx] - bestValue))
                               / 825.0;
            fallingEval = std::clamp(fallingEval, 0.5, 1.5);

            // If the bestMove is stable over several iterations, reduce time accordingly
            timeReduction    = lastBestMoveDepth + 7 < completedDepth ? 1.92 : 0.95;
            double reduction = (1.4 + mainThread->previousTimeReduction) / (2.08 * timeReduction);
            double bestMoveInstability = 1 + 1.8 * totBestMoveChanges / threads.size();

            double totalTime =
              mainThread->tm.optimum() * fallingEval * reduction * bestMoveInstability;

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
            else if (!mainThread->ponder && elapsed() > totalTime * 0.50)
                threads.increaseDepth = false;
            else
                threads.increaseDepth = true;
        }

        mainThread->iterValue[iterIdx] = bestValue;
        iterIdx                        = (iterIdx + 1) & 3;
    }

    if (!mainThread)
        return;

    mainThread->previousTimeReduction = timeReduction;

    // If the skill level is enabled, choose the best move among a set of RootMoves
    // of similar strength instead of the best. We do this at the end instead of
    // during iterative deepening so we have the complete PV.
    if (skill.enabled())
        skill.pick_best(rootMoves, multiPV);
}

namespace {

// Adjusts a mate or TB score from "plies to mate from the root" to
// "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);

    return v >= VALUE_TB_WIN_IN_MAX_PLY  ? v + ply
         : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply
                                         : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score from the transposition
// table (which refers to the plies to mate/be mated from current position) to
// "plies to mate/be mated (TB win/loss) from the root". However, to avoid
// potentially false mate or TB scores related to the 50-move rule and the
// graph history interaction, we return an optimal TB score instead.
Value value_from_tt(Value v, int ply, int r50c) {

    if (v == VALUE_NONE)
        return VALUE_NONE;

    // handle TB win or better for us
    if (v >= VALUE_TB_WIN_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate score
        if (v >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - v > 99 - r50c)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        // Downgrade a potentially false TB score.
        if (VALUE_TB - v > 99 - r50c)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        return v - ply;
    }

    // handle TB loss or worse for us
    if (v <= VALUE_TB_LOSS_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate score.
        if (v <= VALUE_MATED_IN_MAX_PLY && VALUE_MATE + v > 99 - r50c)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        // Downgrade a potentially false TB score.
        if (VALUE_TB + v > 99 - r50c)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        return v + ply;
    }

    return v;
}

// Adds a move to the moves list
void update_pv(Move* pv, Move move, const Move* childPv) {

    for (*pv++ = move; childPv && *childPv != Move::none();)
        *pv++ = *childPv++;
    *pv = Move::none();
}

// Updates stats at the end of search() when a beta-cutoff occurs
void update_all_stats(const Position& pos,
                      Stack*          ss,
                      Search::Worker& workerThread,
                      Move            bestMove,
                      Square          prevSq,
                      SearchedList&   quietsSearched,
                      SearchedList&   capturesSearched,
                      Depth           depth,
                      Move            ttMove) {

    CapturePieceToHistory& captureHistory   = workerThread.captureHistory;
    Piece                  moved_piece      = pos.moved_piece(bestMove);
    PieceType              captured         = type_of(pos.piece_on(bestMove.to_sq()));
    int                    bonus1, bonus2;
    const Color            us               = pos.side_to_move();
    const bool             quiet            = !pos.capture_stage(bestMove);
    int                    bestMoveBonus    = stat_bonus(depth + 1);
    int                    quietMoveBonus   = bestMoveBonus;
    const Move             ttCapture        = ttMove && pos.capture_stage(ttMove) ? ttMove : Move::none();

    // Smaller bonus for TT move
    if (bestMove == ttMove)
        quietMoveBonus = bestMoveBonus / 2;

    // Increase stats for the best move in case it was a quiet move
    if (quiet)
    {
        update_quiet_histories(pos, ss, workerThread, bestMove, quietMoveBonus);

        // Decrease stats for all non-best quiet moves
        for (int i = 0; i < quietsSearched.size(); ++i)
        {
            bonus1 = -bestMoveBonus * 4 / (4 + quietsSearched[i].get_value() + 1);
            update_quiet_histories(pos, ss, workerThread, quietsSearched[i], bonus1);
        }
    }
    else
    {
        // Increase stats for the best move in case it was a capture move
        captureHistory[moved_piece][bestMove.to_sq()][captured] << bestMoveBonus;
    }

    // Extra penalty for a quiet early move that was not a TT move or
    // main killer when it gets refuted.
    if ((depth >= 4 || PvNode) && Move(ss->killers[0]) != bestMove && quiet && !ttMove
        && bestMove != ss->killers[0] && bestMove != ss->killers[1])
    {
        bonus2 = bestMoveBonus / 4;
        update_continuation_histories(ss, moved_piece, bestMove.to_sq(), bonus2);
    }

    // Decrease stats for all non-best capture moves
    for (int i = 0; i < capturesSearched.size(); ++i)
    {
        moved_piece = pos.moved_piece(capturesSearched[i]);
        captured    = type_of(pos.piece_on(capturesSearched[i].to_sq()));
        bonus1      = -bestMoveBonus * 4 / (4 + capturesSearched[i].get_value() + 1);
        captureHistory[moved_piece][capturesSearched[i].to_sq()][captured] << bonus1;
    }

    // Extra bonus for prior countermove that caused the fail low
    if (!pos.capture_stage((ss - 1)->currentMove))
    {
        bonus2 = bestMoveBonus;
        Square prevPrevSq = (ss - 2)->currentMove.is_ok() ? (ss - 2)->currentMove.to_sq() : SQ_NONE;
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq, bonus2);
        if (prevPrevSq != SQ_NONE)
            update_continuation_histories(ss - 2, pos.piece_on(prevPrevSq), prevPrevSq, bonus2 / 2);
    }

    // Set the double-extended search continuation history
    if (is_ok((ss - 1)->currentMove))
    {
        Square     prevSq = (ss - 1)->currentMove.to_sq();
        const auto pc     = pos.piece_on(prevSq);
        workerThread.ttMoveHistory[us][pc][prevSq] << stat_bonus(depth + 1) + 2 * bestMoveBonus + 64;
    }

    // Set the single-extended search continuation history
    if ((ss - 2)->currentMove.is_ok())
    {
        Square prevPrevSq = (ss - 2)->currentMove.to_sq();
        if (prevPrevSq != prevSq)
        {
            const auto pc = pos.piece_on(prevPrevSq);
            workerThread.ttMoveHistory[us][pc][prevPrevSq] << stat_bonus(depth) + bestMoveBonus + 32;
        }
    }
}

// Updates histories of the move pairs formed by moves at ply -1, -3, -4, and -6 with current move.
void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {

    for (int i : {1, 2, 4, 6})
    {
        // Only update the first 2 continuation histories if we are in check
        if (ss->inCheck && i > 2)
            break;
        if (((ss - i)->currentMove).is_ok())
            (*(ss - i)->continuationHistory)[pc][to] << bonus / (1 + 3 * (i == 4));
    }
}

// Updates move sorting heuristics
void update_quiet_histories(const Position& pos,
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
    Thread* thisThread = pos.this_thread();
    thisThread->mainHistory[us][move.from_to()] << bonus;
    update_continuation_histories(ss, pos.moved_piece(move), move.to_sq(), bonus);

    // Update countermove history
    if (((ss - 1)->currentMove).is_ok())
    {
        Square prevSq                                                         = (ss - 1)->currentMove.to_sq();
        thisThread->counterMoves[pos.piece_on(prevSq)][prevSq]                = move;
        thisThread->lowPlyHistory[ss->ply][move.from_to()]                   << bonus >> 4;
        thisThread->lowPlyHistory[ss->ply][((ss - 1)->currentMove).from_to()] << -bonus >> 4;

        // Update low ply history
        if (type_of(pos.moved_piece(move)) != PAWN && (!pos.capture_stage(move) || pos.see_ge(move, -62)))
            thisThread->lowPlyHistory[ss->ply][move.from_to()] << stat_bonus(5);
    }
}

}  // namespace

// When playing with strength handicap, choose the best move among the first
// 'level' moves (as ordered by search score), and return a random one among
// the best of these.
Move Skill::pick_best(const RootMoves& rootMoves, size_t multiPV) {

    const size_t variance = std::min(size_t(level), multiPV);
    const size_t strength = std::max<size_t>(variance, 1);

    // PRNG sequence should be deterministic
    PRNG rng(now() + rootMoves.size() + variance);

    // RootMoves are already sorted by score in descending order
    Value  topScore = rootMoves[0].score;
    int    delta    = std::min(topScore - rootMoves[strength - 1].score, PawnValue);
    Value  weakness = 120 - 2 * level;
    Value  maxScore = topScore - weakness * delta / 128;

    // Choose the best move. For each move's score, add two terms, both dependent
    // on weakness. One is a constant per move, the other is dependent on the
    // number of half-moves we have searched in the game. The former guarantees
    // that the AI will prefer shorter games, and the latter makes the AI play
    // slightly more consistently.
    best = rootMoves[0].pv[0];

    for (size_t i = 0; i < strength; ++i)
        if (rootMoves[i].score >= maxScore - weakness * int(rng.rand<unsigned>() % 50) / 16
                                    - weakness * rootMoves[0].pv.size() / 2)
            best = rootMoves[i].pv[0];

    return best;
}

void Search::clear() {
    for (auto&& th : threads)
        th->worker->clear();

    threads.main_thread()->worker->callsCnt = 0;
}

void Search::Worker::clear() {

    mainHistory.fill(0);
    captureHistory.fill(0);
    ttMoveHistory.fill(0);

    for (bool inCheck : {false, true})
        for (StatsType c : {NoCaptures, Captures})
            for (auto& to : continuationHistory[inCheck][c])
                for (auto& h : to)
                    h->fill(-88);

    continuationCorrectionHistory.fill(0);

    for (size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = int((19.8 + std::log(size_t(options["Threads"])) / 2) * std::log(i));
}

// Main search function for both PV and non-PV nodes
template<NodeType nodeType>
Value Search::Worker::search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    constexpr bool PvNode   = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    // Dive into quiescence search when the depth reaches zero
    if (depth <= 0)
        return qsearch<PvNode ? PV : NonPV>(pos, ss, alpha, beta);

    // Check if we have an upcoming move that draws by repetition ([…])
    // or if the maximum ply depth has been reached.
    if (!rootNode && (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY))
        return (ss->ply >= MAX_PLY && !ss->inCheck)
                 ? evaluate(pos)
                 : value_draw(nodes);

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(0 < depth && depth < MAX_PLY);
    assert(!(PvNode && cutNode));

    Move      pv[MAX_PLY + 1];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry* tte;
    Key      posKey;
    Move     ttMove, move, excludedMove, bestMove;
    Depth    extension, newDepth;
    Value    bestValue, value, ttValue, eval, maxValue, probCutBeta;
    bool     givesCheck, improving, priorCapture, singularQuietLMR;
    bool     capture, moveCountPruning, ttCapture;
    Piece    movedPiece;
    int      moveCount, captureCount, quietCount;
    int      reduction;
    Color    us = pos.side_to_move();
    auto [ttHit, ttData, ttWriter] = tt.probe(pos.key());

    // Step 1. Initialize node
    if constexpr (PvNode)
    {
        // pv nodes: set up the PV
        (ss + 1)->pv    = pv;
        ss->pv[0]       = Move::none();
        bestValue       = -VALUE_INFINITE;
        maxValue        = VALUE_INFINITE;
    }
    else
    {
        // non-pv nodes: set up alpha-beta bounds
        bestValue = alpha;
        maxValue  = beta;
    }

    // Step 2. Check for an immediate draw or maximum ply reached
    if (!rootNode && alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
    {
        alpha = value_draw(nodes);
        if (alpha >= beta)
            return alpha;
    }

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && threads.main_thread()->worker.get() == this && selDepth < ss->ply + 1)
        selDepth = ss->ply + 1;

    if (!rootNode)
    {
        // Step 3. Mate distance pruning. Even if we mate at the next move our score
        // would be at best mate_in(ss->ply+1), but if alpha is already bigger because
        // a shorter mate was found upward in the tree then there is no need to search
        // because we will never beat the current alpha. Same logic but with reversed
        // signs apply also in the opposite condition of being mated instead of giving
        // mate. In this case, return a fail-high.
        alpha = std::max(mated_in(ss->ply), alpha);
        beta  = std::min(mate_in(ss->ply + 1), beta);
        if (alpha >= beta)
            return alpha;
    }

    // Initialize a MovePicker object for the current position, and prepare to search the moves.
    // Because the depth is used for ordering captures and quiet moves, we only use the first
    // 3 bits (0-7) and saturate
    MovePicker mp(pos, ttMove, depth & 7, &captureHistory, &continuationHistory,
                  ss->killers);

    improving        = (ss - 2)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 2)->staticEval
                     : (ss - 4)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 4)->staticEval
                                                          : true;
    priorCapture     = pos.captured_piece();
    singularQuietLMR = move_count_pruning = false;
    ttCapture        = ttMove && pos.capture_stage(ttMove);

    // Indicate PvNodes that will probably fail low if the node was searched
    // with non-PV search at the same or higher depth to a beta that is no more than ttValue
    ss->ttPv = PvNode || (ttHit && tte->depth() >= depth && ttValue != VALUE_NONE
                          && ttValue < beta && tte->bound() & BOUND_UPPER);

    // Step 4. Transposition table lookup.
    excludedMove = ss->excludedMove;
    posKey       = pos.key();
    ttHit        = tte->key16 == (posKey >> 48);
    ttValue      = ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove       = rootNode ? rootMoves[pvIdx].pv[0] : ttHit ? tte->move() : Move::none();

    // At this point, if excluded, skip straight to step 6, static eval. Otherwise,
    // check for an early tt cutoff.
    if (!excludedMove)
        // Step 5. Tablebases lookup. We don't want the score of a partial
        // search to override a tablebase score.
        if (!rootNode && tbConfig.cardinality
            && (tbConfig.cardinality < TB_FILTER_NONE || depth >= TB_PROBE_DEPTH))
        {
            int piecesCount = pos.count<ALL_PIECES>();

            if (piecesCount <= tbConfig.cardinality
                && (piecesCount < TB_FILTER_NONE
                    || depth >= TB_PROBE_DEPTH))
            {
                TB::ProbeState err;
                TB::WDLScore   wdl = TB::probe_wdl(pos, &err);

                // Force check of time on the next occasion
                if (threads.main_thread()->worker.get() == this && ++nodes > TIME_INTERVAL)
                    manager->check_time(*this);

                if (err != TB::ProbeState::FAIL)
                {
                    tbHits.fetch_add(1, std::memory_order_relaxed);

                    int drawScore = tbConfig.use50MoveRule ? 1 : 0;

                    // use the range to decide whether a Wdl value is better than beta or worse than alpha
                    Value minValue = VALUE_NONE, maxValue = VALUE_NONE;

                    if (wdl == TB::WDLBlessedLoss)
                        maxValue = VALUE_DRAW - drawScore;
                    else if (wdl == TB::WDLLoss)
                        maxValue = -VALUE_TB_WIN + ss->ply;
                    else if (wdl == TB::WDLBlessedLoss)
                        maxValue = VALUE_DRAW - drawScore;
                    else if (wdl == TB::WDLDraw)
                        minValue = maxValue = VALUE_DRAW - drawScore;
                    else if (wdl == TB::WDLCursedWin)
                        minValue = VALUE_DRAW + drawScore;
                    else  // WDLWin
                        minValue = VALUE_TB_WIN - ss->ply;

                    if (maxValue != VALUE_NONE && maxValue <= alpha)
                        return maxValue;

                    if (minValue != VALUE_NONE && minValue >= beta)
                    {
                        // For mate use the hash bound.
                        Bound b = wdl == TB::WDLLoss || wdl == TB::WDLBlessedLoss || wdl == TB::WDLWin
                                || wdl == TB::WDLCursedWin
                                    ? BOUND_EXACT
                                    : BOUND_LOWER;

                        ttWriter.write(posKey, value_to_tt(minValue, ss->ply), ss->ttPv, b,
                                       std::min(MAX_PLY - 1, depth + 6), Move::none(), VALUE_NONE, tt.generation());

                        return minValue;
                    }

                    if constexpr (PvNode)
                    {
                        if (minValue != VALUE_NONE && minValue > alpha)
                            alpha = minValue;

                        if (maxValue != VALUE_NONE && maxValue < beta)
                            beta = maxValue;
                    }
                }
            }
        }

    tte = ttWriter.get();
    // If ttMove is quiet, update move sorting heuristics on TT hit (~2 Elo)
    if (ttHit)
    {
        // Note that in case of ttMove being an illegal move (which can happen e.g.
        // in case of a collision), the ttMove is set to Move::none() and ttHit to false.
        // In this case ttMove is ignored, but ttHit is set to true to avoid triggering
        // the adjustment of the clock time (see below).
        if (ttMove && pos.pseudo_legal(ttMove))
        {
            // Extra penalty for early quiet moves of the previous ply (~1 elo)
            if (ttValue >= beta && ttMove && !pos.capture_stage(ttMove) && !priorCapture
                && (ss - 1)->moveCount <= 2)
            {
                update_continuation_histories(ss - 1, pos.piece_on(ttMove.from_sq()),
                                              ttMove.from_sq(), -stat_bonus(depth + 1));
            }

            ss->ttHit = true;
        }
        else
        {
            ttMove = Move::none();
            ttHit  = false;
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (!excludedMove && tte->depth() >= depth && ttValue != VALUE_NONE
            && pos.rule50_count() < 90)
        {
            // ttValue will be altered by the bound type
            ttValue = tte->bound() & BOUND_EXACT
                        ? ttValue
                        : tte->bound() & BOUND_LOWER ? std::max(ttValue, alpha)
                                                     : std::min(ttValue, beta);

            // If ttValue is available, and if its bound type is compatible with current
            // search bound, then ttValue should be preferred as our search result.
            if (ttValue >= beta ? tte->bound() & BOUND_LOWER : tte->bound() & BOUND_UPPER)
            {
                // If ttMove is quiet, update move sorting heuristics on TT hit (~1 Elo)
                if (ttMove)
                {
                    if (ttValue >= beta && !pos.capture_stage(ttMove))
                        update_quiet_histories(pos, ss, *this, ttMove, stat_bonus(depth));
                        // Extra bonus for early quiet moves of the previous ply
                    else if (!pos.capture_stage(ttMove))
                        update_continuation_histories(ss - 1, pos.piece_on(ttMove.from_sq()),
                                                      ttMove.from_sq(), -stat_bonus(depth + 1));
                }

                // Partial workaround for the graph history interaction problem.
                // Cutoff if the transposition table value is a lower/upper bound and
                // if the depth is not 3 or less.
                if (depth > 3)
                    return ttValue;
            }
        }
    }

    // Step 6. Static evaluation of the position
    Value unadjustedStaticEval = VALUE_NONE;
    if (ss->inCheck)
    {
        // Skip early pruning when in check
        ss->staticEval = eval = VALUE_NONE;
        improving             = false;
        goto moves_loop;
    }
    else if (excludedMove)
    {
        // Providing the hint that this node's accumulator will be used often
        // brings significant Elo gain (~13 Elo).
        Eval::NNUE::hint_common_parent_position(pos, refreshTable, thread_idx);
        unadjustedStaticEval = eval = ss->staticEval;
    }
    else if (ttHit)
    {
        unadjustedStaticEval = tte->eval();
        ss->staticEval       = eval =
          unadjustedStaticEval != VALUE_NONE ? unadjustedStaticEval : evaluate(pos);

        // ttValue can be used as a better position evaluation (~7 Elo)
        if (ttValue != VALUE_NONE
            && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    }
    else
    {
        unadjustedStaticEval = ss->staticEval = eval = evaluate(pos);
        // Fresh ttEntry evaluation saved as tt value (~7 Elo)
        if (!ss->ttPv)
            ttWriter.write(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_UNSEARCHED,
                           Move::none(), unadjustedStaticEval, tt.generation());
    }

    // Use static evaluation difference to improve quiet move ordering (~9 Elo)
    if (((ss - 1)->currentMove).is_ok() && !(ss - 1)->inCheck && !priorCapture)
    {
        int bonus = std::clamp(-18 * int((ss - 1)->staticEval + ss->staticEval), -1812, 1812);
        thisThread->mainHistory[~us][((ss - 1)->currentMove).from_to()] << bonus;
    }

    // Set up the improving flag, which is true if current static evaluation is
    // bigger than the previous static evaluation at our turn (if we were in
    // check at previous move we look at static evaluation at move prior to it
    // and if we were in check at move prior to it flag is set to true).
    improving = (ss - 2)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 2)->staticEval
              : (ss - 4)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 4)->staticEval
                                                   : true;

    // Step 7. Razoring.
    // If eval is really low check with qsearch if it can exceed alpha, if it can't,
    // return a fail low.
    // (~2 Elo)
    if (!ss->ttPv && depth == 1 && eval <= alpha - 456)
        return qsearch<NonPV>(pos, ss, alpha, beta);

    // Step 8. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 9
        && eval - futility_margin(depth, cutNode && !ttMove, improving, opOnTtHit && !ttMove) >= beta
        && eval >= beta && eval < VALUE_TB_WIN_IN_MAX_PLY  // Do not return unproven wins
        && (!ttMove || pos.capture_stage(ttMove)))
        return (eval + beta) / 2;

    // Step 9. Null move search with verification search (~35 Elo)
    if (!PvNode && !excludedMove && !ss->inCheck && (ss - 1)->currentMove != Move::null()
        && (ss - 1)->statScore < 17329 && eval >= beta && eval >= ss->staticEval
        && ss->staticEval >= beta - 21 * depth + 258 && pos.non_pawn_material(us)
        && ss->ply >= nmpMinPly && beta > VALUE_TB_LOSS_IN_MAX_PLY)
    {
        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth and eval (Idea from Komodo and Stockfish's version)
        Depth R = std::min(int(eval - beta) / 152, 6) + depth / 3 + 4;

        // Zugzwang prone positions: when our prev move was null or current pos eval is much > beta;
        if (R >= 5 && !(ttHit && tte->depth() >= depth - R + 1 && ttValue <= beta))
            --R;

        do_null_move(pos, st, ss);
        ss->endMoves = (ss + 1)->endMoves = 0;
        (ss + 1)->skipEarlyPruning        = true;
        nullValue                         = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
        undo_null_move(pos);

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && nullValue < VALUE_TB_WIN_IN_MAX_PLY)
        {
            if (beta > VALUE_TB_LOSS_IN_MAX_PLY && (depth < 15 || (abs(beta) < 3 * PawnValue)))
                return nullValue;

            assert(!ttMove);  // Expecting no ttMove in case of null search fail high

            // Do verification search at high depths, with null move pruning disabled
            // until ply exceeds the current depth.
            nmpMinPly = ss->ply + 3 * (depth - R) / 4;

            Value v = search<NonPV>(pos, ss, beta - 1, beta, depth - R, false);

            nmpMinPly = 0;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 10. If we don't have a ttMove suggest a good one.
    // A good move could be a move returned by evaluation function.
    probCutBeta = beta + 168 - 61 * improving;

    // Step 11. ProbCut (~10 Elo)
    // If we have a good enough capture (or queen promotion) and a reduced search returns a value
    // much above beta, we can (almost) safely prune the previous move.
    if (!PvNode && !excludedMove && !(ss->ttHit && tte->depth() >= depth - 3 && ttValue < probCutBeta)
        && depth >= 4 && abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
        && !(ttHit && tte->depth() >= depth && ttValue != VALUE_NONE && ttValue < probCutBeta))
    {
        assert(probCutBeta < VALUE_INFINITE && probCutBeta > beta);

        MovePicker mp(pos, ttMove, probCutBeta - ss->staticEval, &captureHistory,
                      &continuationHistory, Move::none(), ss, depth);

        while ((move = mp.next_move()) != Move::none())
            if (move != excludedMove && pos.legal(move))
            {
                assert(pos.capture_stage(move) || move.promotion_type() == QUEEN);

                do_move(pos, move, st, ss, true);
                value = -search<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, depth - 4, !cutNode);
                undo_move(pos, move);

                if (value >= probCutBeta)
                {
                    //if (!(ttHit && tte->depth() >= depth - 3 && ttValue < probCutBeta))
                    ttWriter.write(posKey, value_to_tt(value, ss->ply), ss->ttPv, BOUND_LOWER,
                                   depth - 3, move, ss->staticEval, tt.generation());
                    return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probCutBeta - beta) : value;
                }
            }

        Eval::NNUE::hint_common_parent_position(pos, refreshTable, threadIdx);
    }

moves_loop:  // When in check, search starts here

    // Step 12. A small Probcut idea, when we are in check (~4 Elo)
    probCutBeta = beta + 425;
    if (ss->inCheck && !PvNode && ttCapture && (tte->bound() & BOUND_LOWER)
        && tte->depth() >= depth - 4 && ttValue >= probCutBeta
        && abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && abs(beta) < VALUE_TB_WIN_IN_MAX_PLY)
        return probCutBeta;

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory,
                                        (ss - 3)->continuationHistory,
                                        (ss - 4)->continuationHistory,
                                        nullptr,
                                        (ss - 6)->continuationHistory};

    Move     countermove = prevSq != SQ_NONE ? thisThread->counterMoves[pos.piece_on(prevSq)][prevSq] : Move::none();
    MovePicker mp(pos, ttMove, depth, &captureHistory, &continuationHistory, countermove, contHist, ss);

    value                = bestValue;
    moveCountPruning     = false;
    singularQuietLMR     = !ttMove || !pos.capture_stage(ttMove);
    ttCapture            = ttMove && pos.capture_stage(ttMove);

    // Indicate PvNodes that will probably fail low if the node was searched
    // with non-PV search at the same or higher depth to a beta that is no more than ttValue
    ss->ttPv = PvNode || (ss->ttHit && tte->depth() >= depth && ttValue != VALUE_NONE
                          && ttValue < beta && tte->bound() & BOUND_UPPER);
    ss->ttHit = ttHit;

    // Step 13. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::none())
    {
        assert(move.is_ok());

        if (move == excludedMove)
            continue;

        // Check for legality
        if (!rootNode && !pos.legal(move))
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in Root
        // Move List. In MultiPV mode we also skip PV moves that have been already
        // searched and those of lower "TB rank" if we are in a TB root position.
        if (rootNode
            && (std::count(limits.searchmoves.begin(), limits.searchmoves.end(), move)
                    && !limits.searchmoves.empty()
                || (pvIdx >= pvLast)))
            continue;

        ss->moveCount = ++moveCount;

        if (rootNode && is_mainthread() && nodes > TIME_INTERVAL)
        {
            main_manager()->pv(*this, threads, tt, rootDepth);
            main_manager()->updates.onIter({
              rootDepth, UCIEngine::move(move, pos.is_chess960()), moveCount
            });
        }

        if (PvNode)
            (ss + 1)->pv = nullptr;

        extension    = 0;
        capture      = pos.capture_stage(move);
        movedPiece   = pos.moved_piece(move);
        givesCheck   = pos.gives_check(move);
        priorCapture = capture;

        // Calculate new depth for this move
        newDepth = depth - 1;

        Value delta = beta - alpha;

        Depth r = reduction(improving, depth, moveCount, delta);

        // Step 14. Pruning at shallow depth (~120 Elo). Conditions:
        // a) not a ttMove
        // b) not a capture or promotion
        // c) not giving check
        // d) not improving position
        // e) move count pruning
        if (!ss->inCheck && bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(us))
        {
            // Move count pruning for quiet moves (~20 Elo)
            moveCountPruning = moveCount >= futility_move_count(improving, depth, ss->inCheck);

            // Continuation history pruning (~3 Elo)
            if (!capture && (*contHist[0])[movedPiece][move.to_sq()] < 0
                && (*contHist[1])[movedPiece][move.to_sq()] < 0)
                r++;

            // Reduced depth of the next LMR search
            int lmrDepth = newDepth - r;

            if (capture || givesCheck)
            {
                // Futility pruning for captures (~0 Elo)
                if (!givesCheck && lmrDepth < 7 && !ss->inCheck
                    && ss->staticEval + PieceValue[pos.piece_on(move.to_sq())] + 238 * lmrDepth
                         <= alpha)
                    continue;

                // SEE based pruning for captures and checks (~11 Elo)
                if (!pos.see_ge(move, -185 * depth))
                    continue;
            }
            else
            {
                int history =   (*contHist[0])[movedPiece][move.to_sq()]
                              + (*contHist[1])[movedPiece][move.to_sq()]
                              + (*contHist[3])[movedPiece][move.to_sq()]
                              + thisThread->pawnHistory[pawn_structure_index<Correction>(pos)][movedPiece][move.to_sq()];

                // Continuation history pruning (~3 Elo)
                if (lmrDepth < 6 && history < -3832 * (depth - 1))
                    continue;

                history += 2 * thisThread->mainHistory[us][move.from_to()];

                lmrDepth += history / 7011;
                lmrDepth = std::max(lmrDepth, -1);

                // Futility pruning: parent node (~13 Elo)
                if (!ss->inCheck && lmrDepth < 12
                    && ss->staticEval + (bestValue < ss->staticEval - 57 ? 123 : 77) + 118 * lmrDepth <= alpha)
                    continue;

                lmrDepth = std::max(lmrDepth, 0);

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, -31 * lmrDepth * lmrDepth))
                    continue;
            }
        }

        // Step 15. Extensions (~100 Elo)
        // We take care to not overdo to avoid search getting stuck.
        if (ss->ply < thisThread->rootDepth * 2)
        {
            // Singular extension search (~94 Elo). If all moves but one fail low on a
            // search of (alpha-s, beta-s), and just one fails high on (alpha, beta),
            // then that move is singular and should be extended. To verify this we do
            // a reduced search on all the other moves but the ttMove and if the result
            // is lower than ttValue minus a margin, then we will extend the ttMove. (~13 Elo)
            if (!rootNode && move == ttMove && !excludedMove  // Avoid recursive singular search
                && depth >= 4 && abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && (tte->bound() & BOUND_LOWER)
                && tte->depth() >= depth - 3)
            {
                Value singularBeta  = ttValue - (64 + 57 * (ss->ttPv && !PvNode)) * depth / 64;
                Depth singularDepth = newDepth / 2;

                ss->excludedMove = move;
                value            = search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
                ss->excludedMove = Move::none();

                if (value < singularBeta)
                {
                    extension        = 1;
                    singularQuietLMR = !ttCapture;
                }
                else if (singularBeta >= beta)
                    return singularBeta;
                else if (ttValue >= beta)
                    extension = -2;
                else if (cutNode)
                    extension = depth < 9 ? -1 : 0;
                else if (ttValue <= alpha)
                    extension = -1;
            }

            // Check extensions (~1 Elo)
            else if (givesCheck && depth > 9)
                extension = 1;

            // Quiet ttMove extensions (~1 Elo)
            else if (PvNode && move == ttMove && move != ss->killers[0]
                     && !pos.capture_stage(move))
                extension = 1;

            // Recapture extensions (~1 Elo)
            else if (PvNode && move == ttMove && pos.capture_stage(move)
                     && type_of(pos.piece_on(move.to_sq())) == type_of(pos.piece_on(move.from_sq()))
                     && captureCount == 1)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;
        ss->doubleExtensions = (ss - 1)->doubleExtensions + (extension >= 2);

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        // Update the current move (this must be done after singular extension search)
        ss->currentMove = move;
        ss->continuationHistory =
          &continuationHistory[ss->inCheck][priorCapture][movedPiece][move.to_sq()];

        uint64_t nodesBefore = 0;
        if (rootNode)
            nodesBefore = nodes;

        // Step 16. Make the move
        do_move(pos, move, st, ss, givesCheck);

        // Decrease reduction if position is or has been on the PV (~7 Elo)
        if (ss->ttPv && !likelyFailLow)
            r--;

        // Decrease reduction if opponent's move count is high (~1 Elo)
        if ((ss - 1)->moveCount > 8)
            r--;

        // Increase reduction for cut nodes (~3 Elo)
        if (cutNode)
            r++;

        // Increase reduction if ttMove is a capture (~3 Elo)
        if (ttCapture)
            r++;

        // Decrease reduction for PvNodes (~0 Elo on STC, ~2 Elo on LTC)
        if (PvNode)
            r--;

        // Decrease reduction if ttMove has been singularly extended (~1 Elo)
        if (singularQuietLMR)
            r--;

        // Increase reduction on repetitions (~1 Elo)
        if (!capture && pos.has_repeated())
            r++;

        // Increase reduction if move appears bad per history (~8 Elo)
        if (!capture && !givesCheck)
        {
            int history =   (*contHist[0])[movedPiece][move.to_sq()]
                          + (*contHist[1])[movedPiece][move.to_sq()]
                          + (*contHist[3])[movedPiece][move.to_sq()]
                          + thisThread->pawnHistory[pawn_structure_index<Correction>(pos)][movedPiece][move.to_sq()]
                          + thisThread->correctionHistory[us][pawn_structure_index<Correction>(pos)]
                          + thisThread->mainHistory[us][move.from_to()];

            if (history < -4506)
                r++;
            else if (history > 7849)
                r--;
        }

        Depth d = std::clamp(newDepth - r, 1, newDepth + 1);

        // Step 17. Late move reductions / extensions search (~126 Elo)
        if (depth >= 2 && moveCount > 1 + (PvNode && ss->ply <= 1))
        {
            // In general we want to cap the LMR depth search at newDepth, but when
            // reduction is negative, we allow this move a limited search extension
            // beyond the first move depth. This may lead to hidden multiple extensions.
            // To prevent problems when the maxply is reached and ensure a hard TB bound,
            // we cap this at MAX_PLY - 1.
            assert(d <= newDepth);

            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result was good enough
                // for the limited search to fail high then don't do an expensive full depth search.
                const bool doDeeperSearch     = value > (bestValue + 51 + 10 * (newDepth - d));
                const bool doShallowerSearch  = value < bestValue + newDepth;
                const bool doEvenShallowerSearch = value > alpha + 700 && ss->doubleExtensions <= 11;

                newDepth += doDeeperSearch - doShallowerSearch - doEvenShallowerSearch;

                if (newDepth > d)
                    value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                int bonus = value <= alpha ? -stat_bonus(newDepth)
                          : value >= beta  ? stat_bonus(newDepth)
                                           : 0;

                update_continuation_histories(ss, movedPiece, move.to_sq(), bonus);
            }
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        else if (PvNode)
            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth, false);

        else
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

        // Step 18. Undo move
        undo_move(pos, move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 19. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and we return immediately without
        // updating best move, PV and TT.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (rootNode)
        {
            RootMove& rm = *std::find(rootMoves.begin(), rootMoves.end(), move);
            rm.effort += nodes - nodesBefore;

            rm.averageScore =
              rm.averageScore != -VALUE_INFINITE ? (2 * rm.averageScore + value) / 3 : value;
            rm.meanSquaredScore = rm.meanSquaredScore != -VALUE_INFINITE
                                    ? (rm.meanSquaredScore + value * value) / 2
                                    : value * value;

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.score = rm.uciScore = value;
                rm.selDepth            = selDepth;
                rm.scoreLowerbound = rm.scoreUpperbound = false;

                if (moveCount > 1)
                    ++bestMoveChanges;

                rm.pv.resize(1);
                rm.pv[0] = move;

                assert((ss + 1)->pv);

                for (Move* m = (ss + 1)->pv; *m != Move::none(); ++m)
                    rm.pv.push_back(*m);

                // We record how often the best move has been changed in each iteration.
                // This information is used for time management. In MultiPV mode,
                // we must take care to only do this for the first PV line.
                if (moveCount > 1 && !thisThread)
                    ++bestMoveChanges;
            }
            else
                // All other moves but the PV, are set to the lowest value: this
                // is not a problem when sorting because the sort is stable and the
                // move position in the list is preserved - just the values change.
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

                    // Stores only a lower bound if we are not at a PV node
                    ttWriter.write(posKey, value_to_tt(value, ss->ply), ss->ttPv,
                                   value >= beta ? BOUND_LOWER : BOUND_EXACT, depth, move,
                                   unadjustedStaticEval, tt.generation());

                    // Update killers and history only if we are not capturing
                    if (!pos.capture_stage(move))
                        update_all_stats(pos, ss, *this, bestMove, prevSq, quietsSearched,
                                          capturesSearched, depth, ttMove);

                    break;
                }
                alpha = value;  // Update alpha! Always alpha < beta
            }
        }

        // If the move is worse than some previously searched move,
        // remember it, to update its stats later.
        if (move != bestMove)
        {
            if (capture)
                capturesSearched.add(move, captureCount);
            else
                quietsSearched.add(move, quietCount);
        }
    }

    // The following condition would detect a stop only after move loop completion, which
    // may be too late in some cases.
    if (threads.stop.load(std::memory_order_relaxed))
        return VALUE_ZERO;

    if (moveCount == 0)
        ss->inCheck ? bestValue = mated_in(ss->ply) : bestValue = VALUE_DRAW;

    // If there is a move that produces a search value > alpha we update the stats of searched moves
    else if (bestMove && !pos.capture_stage(bestMove))
        update_all_stats(pos, ss, *this, bestMove, prevSq, quietsSearched, capturesSearched,
                         depth, ttMove);

    else if (!bestMove)
        ss->inCheck ? bestValue = mated_in(ss->ply) : bestValue = VALUE_DRAW;

    // Adjust best value for fail low/high
    if (bestValue >= beta)
    {
        // Do not store in the hash table values which are larger than beta,
        // as they may be inconsistent with the hash table entry generated from a shallower
        // search that was aborted, or due to alpha-beta pruning, or terminal nodes.
        bestValue = beta;
    }
    else if (bestValue <= alpha)
    {
        // Do not store in the hash table values which are smaller than alpha,
        // as they may be inconsistent with the hash table entry generated from a shallower
        // search that was aborted, or due to alpha-beta pruning, or terminal nodes.
        bestValue = alpha;
        ss->ttPv      = ss->ttPv || ((ss + 1)->ttPv && depth > 3);

        // If we searched all the legal moves and none of them increased alpha, then this is a fail-low node.
        // However, if we are in a PV node, we may still wish to raise alpha in the calling parent.
        ttWriter.write(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                       bestValue <= alpha ? BOUND_UPPER : BOUND_EXACT, depth, bestMove,
                       unadjustedStaticEval, tt.generation());
    }

    // Write a transposition table entry. We don't write if the bestValue is -VALUE_INFINITE,
    // as this would require a tb score, but we haven't probed the tb.
    else if (bestValue != -VALUE_INFINITE)
    {
        // Stores the exact score if we searched all moves and bestValue > alpha
        ttWriter.write(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv, BOUND_EXACT, depth,
                       bestMove, unadjustedStaticEval, tt.generation());
    }

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}


// Quiescence search function, which is called by the main search function
// when the depth reaches zero (or a negative value).
template<NodeType nodeType>
Value Search::Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(ss->ply < MAX_PLY);

    // Check if we have an upcoming move that draws by repetition, or
    // if the opponent had an alternative move earlier to this position (excluding
    // the last ply and the move being made) then we terminate the search.
    if (alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
        return VALUE_DRAW;

    Move      pv[MAX_PLY + 1];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry* tte;
    Key      posKey;
    Move     ttMove, move, bestMove;
    Depth    ttDepth;
    Value    bestValue, value, ttValue, futilityValue, futilityBase;
    bool     pvHit, givesCheck, capture;
    int      moveCount;
    Color    us = pos.side_to_move();

    // Step 1. Initialize node
    if constexpr (PvNode)
    {
        (ss + 1)->pv = pv;
        ss->pv[0]    = Move::none();
    }

    (ss + 1)->ttHit        = false;
    (ss + 1)->inCheck      = false;
    bestMove               = Move::none();
    moveCount              = 0;

    // Step 2. Check for an immediate draw or maximum ply reached
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY)
        return (ss->ply >= MAX_PLY && !pos.checkers()) ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Decide whether or not to include checks: this fixes also the type of
    // TT entry depth that we are going to use. Note also that in qsearch we
    // use only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
    ttDepth = pos.checkers() ? DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;

    // Step 3. Transposition table lookup
    posKey  = pos.key();
    tte     = tt.probe(posKey, ss->ttHit);
    ttValue = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove  = ss->ttHit ? tte->move() : Move::none();
    pvHit   = ss->ttHit && tte->is_pv();

    // At this point, if excluded, skip straight to step 6, static eval. Otherwise,
    // check for an early tt cutoff
    if (!PvNode && ss->ttHit && tte->depth() >= ttDepth && ttValue != VALUE_NONE
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    // Step 4. Static evaluation of the position
    Value unadjustedStaticEval = VALUE_NONE;
    if (pos.checkers())
        bestValue = futilityBase = -VALUE_INFINITE;
    else
    {
        if (ss->ttHit)
        {
            unadjustedStaticEval = tte->eval();
            if (unadjustedStaticEval == VALUE_NONE)
                unadjustedStaticEval = evaluate(pos);
            bestValue = futilityBase = unadjustedStaticEval;

            // ttValue can be used as a better position evaluation (~13 Elo)
            if (ttValue != VALUE_NONE
                && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttValue;
        }
        else
        {
            // In case of null move search, use previous static eval with a different
            // sign if it is available.
            unadjustedStaticEval = bestValue = futilityBase =
              (ss - 1)->currentMove != Move::null() ? evaluate(pos) : -(ss - 1)->staticEval;
        }

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (!ss->ttHit)
                tte->save(posKey, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER,
                          DEPTH_UNSEARCHED, Move::none(), unadjustedStaticEval, tt.generation());

            return bestValue;
        }

        if (bestValue > alpha)
            alpha = bestValue;

        futilityValue = futilityBase + 200;
    }

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory,
                                        nullptr,
                                        (ss - 4)->continuationHistory,
                                        nullptr,
                                        (ss - 6)->continuationHistory};

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions, and other checks (only if depth >= DEPTH_QS_CHECKS)
    // will be generated.
    Square     prevSq = (ss - 1)->currentMove.to_sq();
    MovePicker mp(pos, ttMove, DEPTH_QS_CHECKS, &captureHistory, &continuationHistory,
                  prevSq);

    int quiet_count = 0;

    // Step 5. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::none())
    {
        assert(move.is_ok());

        // Check for legality
        if (!pos.legal(move))
            continue;

        givesCheck = pos.gives_check(move);
        capture    = pos.capture_stage(move);

        moveCount++;

        // Step 6. Pruning
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(us))
        {
            // Futility pruning and moveCount pruning (~10 Elo)
            if (!givesCheck && move.to_sq() != prevSq && futilityValue > -VALUE_TB_WIN_IN_MAX_PLY
                && type_of(pos.piece_on(move.to_sq())) != PAWN)
            {
                if (moveCount > 2)
                    continue;

                futilityValue = futilityBase + PieceValue[pos.piece_on(move.to_sq())];

                if (futilityValue <= alpha)
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }

                if (futilityValue < beta && pos.see_ge(move, 1))
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }
            }

            // We prune after the second quiet check evasion move, where being 'in check' is
            // implicitly checked through the counter, and being a 'quiet move' apart from
            // being a tt move is assumed after an increment because captures are ordered first.
            if (quietCount > 1)
                break;

            // Continuation history (~3 Elo)
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
          &continuationHistory[pos.checkers()][capture][pos.moved_piece(move)][move.to_sq()];

        quietCount += !capture && !givesCheck;

        // Step 7. Make and search the move
        do_move(pos, move, st, ss, givesCheck);
        value = -qsearch<nodeType>(pos, ss + 1, -beta, -alpha);
        undo_move(pos, move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 8. Check for a new best move
        if (value > bestValue)
        {
            bestValue = value;

            if (value > alpha)
            {
                bestMove = move;

                if constexpr (PvNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value < beta)  // Update alpha (alpha < beta)
                    alpha = value;
                else
                    break;  // Fail high
            }
        }
    }

    // Step 9. Check for mate
    // All legal moves have been searched and if there are no legal moves, it
    // must be a mate or a stalemate. If we are in a singular extension search then
    // return a fail low score.

    assert(moveCount || !pos.checkers() || MoveList<LEGAL>(pos).size() == 0);

    if (moveCount == 0)
        bestValue = pos.checkers() ? mated_in(ss->ply) : VALUE_DRAW;

    tte->save(posKey, value_to_tt(bestValue, ss->ply), pvHit, bestValue >= beta ? BOUND_LOWER : BOUND_UPPER,
              ttDepth, bestMove, unadjustedStaticEval, tt.generation());

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}

// Adjusts a mate or TB score from "plies to mate from the root" to
// "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);

    return v >= VALUE_TB_WIN_IN_MAX_PLY  ? v + ply
         : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply
                                         : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score from the transposition
// table (which refers to the plies to mate/be mated from current position) to
// "plies to mate/be mated (TB win/loss) from the root". However, to avoid
// potentially false mate or TB scores related to the 50-move rule and the
// graph history interaction, we return an optimal TB score instead.
Value value_from_tt(Value v, int ply, int r50c) {

    if (v == VALUE_NONE)
        return VALUE_NONE;

    // handle TB win or better for us
    if (v >= VALUE_TB_WIN_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate score
        if (v >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - v > 99 - r50c)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        // Downgrade a potentially false TB score.
        if (VALUE_TB - v > 99 - r50c)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        return v - ply;
    }

    // handle TB loss or worse for us
    if (v <= VALUE_TB_LOSS_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate score.
        if (v <= VALUE_MATED_IN_MAX_PLY && VALUE_MATE + v > 99 - r50c)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        // Downgrade a potentially false TB score.
        if (VALUE_TB + v > 99 - r50c)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        return v + ply;
    }

    return v;
}

TimePoint Search::Worker::elapsed() const {
    return main_manager()->tm.elapsed(threads.nodes_searched());
}

TimePoint Search::Worker::elapsed_time() const {
    return main_manager()->tm.elapsed_time();
}

}  // namespace Stockfish