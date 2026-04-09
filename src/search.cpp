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

// Bonus longest at ~200 depth, decays towards ~75 at depth 0
int stat_bonus(Depth d) { return std::min(168 * d - 100, 1718); }

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
            totBestMoveChanges += th->worker->bestMoveChanges;

        // Do we have time for the next iteration? Can we stop searching now?
        if (limits.use_time_management() && !threads.stop && !mainThread->stopOnPonderhit)
        {
            auto nodesEffort = threads.nodes_searched();

            double fallingEval =
              (66 + 12 * (mainThread->bestPreviousAverageScore - bestValue)
               + 6 * (mainThread->iterValue[iterIdx] - bestValue))
              / 616.6;
            fallingEval = std::clamp(fallingEval, 0.5, 1.5);

            // If the bestMove is stable over several iterations, reduce time accordingly
            timeReduction    = lastBestMoveDepth + 8 < completedDepth ? 1.57 : 0.65;
            double reduction = (1.56 + mainThread->previousTimeReduction) / (2.20 * timeReduction);
            double bestMoveInstability = 1.073 + std::max(1.0, 2.25 - 9.9 / rootDepth)
                                                   * totBestMoveChanges / threads.size();

            double totalTime = mainThread->tm.optimum() * fallingEval * reduction
                             * bestMoveInstability;

            // Cap used time in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = std::min(500.0, totalTime);

            auto elapsedTime = elapsed();

            if (completedDepth >= 10 && nodesEffort >= nodesEffort * 0.98
                && elapsedTime > totalTime * 0.739)
                threads.stop = true;

            // Stop the search if we have exceeded the maximum available time for this move,
            // and there is at least one legal move to play.
            if (elapsedTime > mainThread->tm.maximum())
                threads.stop = true;

            // Stop the search early if one move seems to be much better than the others
            if (rootMoves.size() >= 1 && !is_loss(bestValue) && elapsedTime > totalTime * 0.42)
            {
                double effort = double(rootMoves[0].effort) / nodesEffort;

                if (effort > 0.97)
                    threads.stop = true;
            }
        }

        mainThread->iterValue[iterIdx] = bestValue;
        iterIdx                        = (iterIdx + 1) & 3;
    }

    if (!mainThread)
        return;

    mainThread->previousTimeReduction = timeReduction;

    // If skill level is enabled, swap best PV line with the sub-optimal one
    if (skill.enabled())
        std::swap(rootMoves[0],
                  *std::find(rootMoves.begin(), rootMoves.end(),
                             skill.best != Move::none() ? skill.best : skill.pick_best(rootMoves, multiPV)));
}

void Search::Worker::do_move(Position& pos, const Move move, StateInfo& st, Stack* const ss) {
    pos.do_move(move, st, &tt);
    ss->currentMove = move;
}

void Search::Worker::do_move(
  Position& pos, const Move move, StateInfo& st, const bool givesCheck, Stack* const ss) {
    pos.do_move(move, st, givesCheck, st.dirtyPiece, st.dirtyThreats, &tt, &sharedHistory);
    ss->currentMove = move;
}

void Search::Worker::do_null_move(Position& pos, StateInfo& st, Stack* const ss) {
    pos.do_null_move(st);
    ss->currentMove = Move::null();
}

void Search::Worker::undo_move(Position& pos, const Move move) { pos.undo_move(move); }

void Search::Worker::undo_null_move(Position& pos) { pos.undo_null_move(); }

int Search::Worker::reduction(bool i, Depth d, int mn, int delta) const {
    int reductionScale = reductions[d] * reductions[mn];
    return (reductionScale + 1118 - int(i) * 974 + delta * 118 / rootDelta) / 1024;
}

TimePoint Search::Worker::elapsed() const {
    return main_manager()->tm.elapsed(
      [this]() { return threads.nodes_searched(); },
      [this]() { return elapsed_time(); });
}

TimePoint Search::Worker::elapsed_time() const { return now() - limits.startTime; }

Value Search::Worker::evaluate(const Position& pos) {
    Value v = Eval::evaluate(networks[numaAccessToken], pos, accumulatorStack, refreshTable,
                             optimism[pos.side_to_move()]);
    return v;
}

// Main search function for both PV and non-PV nodes
template<NodeType nodeType>
Value Search::Worker::search(
  Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    constexpr bool PvNode   = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    // Dive into quiescence search when the depth reaches zero
    if (depth <= 0)
        return qsearch<PvNode ? PV : NonPV>(pos, ss, alpha, beta);

    // Check if we have an upcoming move that draws by repetition
    if (!rootNode && alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
    {
        alpha = value_draw(nodes);
        if (alpha >= beta)
            return alpha;
    }

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(0 < depth && depth < MAX_PLY);
    assert(!(PvNode && cutNode));

    Move  pv[MAX_PLY + 1], capturesSearched[SEARCHEDLIST_CAPACITY],
      quietsSearched[SEARCHEDLIST_CAPACITY];
    StateInfo st;
    TTData    ttData;
    Key       posKey;
    Move      move, excludedMove, bestMove, ttMove;
    Depth     extension, newDepth;
    Value     bestValue, value, ttValue, eval, maxValue, probCutBeta;
    bool      givesCheck, improving, priorCapture, opponentWorsening;
    bool      capture, ttCapture;
    Piece     movedPiece;
    int       moveCount, captureCount, quietCount;

    // Initialize node
    Worker*  thisThread = this;
    bool     inCheck    = pos.checkers();
    Color    us         = pos.side_to_move();
    moveCount           = captureCount = quietCount = ss->moveCount = 0;
    bestValue                                                       = -VALUE_INFINITE;
    maxValue                                                        = VALUE_INFINITE;

    // Check for the available remaining time
    if (is_mainthread())
        main_manager()->check_time(*this);

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && thisThread->selDepth < ss->ply + 1)
        thisThread->selDepth = ss->ply + 1;

    if (!rootNode)
    {
        // Step 2. Check for aborted search and immediate draw
        if (threads.stop.load(std::memory_order_relaxed) || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return (ss->ply >= MAX_PLY && !inCheck)
                   ? evaluate(pos)
                   : value_draw(thisThread->nodes);

        // Step 3. Mate distance pruning. Even if we mate at the next move our score
        // would be at best mate_in(ss->ply + 1), but if alpha is already bigger because
        // a shorter mate was found upward in the tree then there is no need to search
        // because we will never beat the current alpha. Same logic but with reversed
        // signs apply also in the opposite condition of being mated instead of giving
        // mate. In this case, return a fail-high score.
        alpha = std::max(mated_in(ss->ply), alpha);
        beta  = std::min(mate_in(ss->ply + 1), beta);
        if (alpha >= beta)
            return alpha;
    }
    else
        thisThread->rootDelta = beta - alpha;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    bestMove    = Move::none();
    (ss + 2)->cutoffCnt = 0;
    ss->inCheck    = inCheck;
    priorCapture   = pos.captured_piece();
    Square prevSq  = ((ss - 1)->currentMove).is_ok() ? (ss - 1)->currentMove.to_sq() : SQ_NONE;

    // Initialize stats to zero for the grandchildren of the current position.
    // So stats is shared between all grandchildren and only the first grandchild starts
    // with stats = 0. Later grandchildren start with the last updated stats of the
    // previous grandchild. This is a small loss for the first grandchild but is useful
    // for the later grandchildren. It avoids a costly cache miss.
    (ss + 2)->statScore = 0;

    // Step 4. Transposition table lookup
    excludedMove = ss->excludedMove;
    posKey       = pos.key();
    auto [ttHit, ttDataCopy, ttWriter] = tt.probe(posKey);
    ttData   = ttDataCopy;
    ss->ttHit = ttHit;
    ttValue  = ttHit ? value_from_tt(ttData.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove   = rootNode  ? rootMoves[pvIdx].pv[0]
             : ttHit     ? ttData.move
                         : Move::none();
    ttCapture = ttMove && pos.capture_stage(ttMove);
    ss->ttPv   = excludedMove ? ss->ttPv : PvNode || (ttHit && ttData.is_pv);

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && !excludedMove && ttData.depth > depth - (ttData.bound == BOUND_EXACT)
        && ttValue != VALUE_NONE  // Can happen when !ttHit or when access to TT is racy
        && (ttData.bound & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
    {
        // If ttMove is quiet, update move sorting heuristics on TT hit (~2 Elo)
        if (ttMove)
        {
            if (ttValue >= beta)
            {
                // Bonus for a quiet ttMove that fails high (~2 Elo)
                if (!ttCapture)
                    update_quiet_histories(pos, ss, *thisThread, ttMove, stat_bonus(depth));

                // Extra penalty for early quiet moves of the previous ply (~0 Elo on STC, ~2 Elo on LTC)
                if (prevSq != SQ_NONE && (ss - 1)->moveCount <= 2 && !priorCapture)
                    update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                                  -stat_bonus(depth + 1));
            }
            // Penalty for a quiet ttMove that fails low (~1 Elo)
            else if (!ttCapture)
            {
                int penalty = -stat_bonus(depth);
                update_quiet_histories(pos, ss, *thisThread, ttMove, penalty);
            }
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < 90)
            return ttValue;
    }

    // Step 5. Tablebases probe
    if (!rootNode && !excludedMove && tbConfig.cardinality)
    {
        int piecesCount = pos.count<ALL_PIECES>();

        if (piecesCount <= tbConfig.cardinality
            && (piecesCount < tbConfig.cardinality || depth >= tbConfig.probeDepth)
            && pos.rule50_count() == 0 && !pos.can_castle(ANY_CASTLING))
        {
            TB::ProbeState err;
            TB::WDLScore   wdl = TB::probe_wdl(pos, &err);

            // Force check of time on the next occasion
            if (is_mainthread())
                main_manager()->callsCnt = 0;

            if (err != TB::FAIL)
            {
                thisThread->tbHits.fetch_add(1, std::memory_order_relaxed);

                int drawScore = tbConfig.useRule50 ? 1 : 0;

                Value tbValue = VALUE_TB - ss->ply;

                if (wdl == TB::WDLBlessedLoss)
                    tbValue = VALUE_DRAW - drawScore;

                if (wdl == TB::WDLLoss)
                    tbValue = -(VALUE_TB - ss->ply);

                if (wdl == TB::WDLWin)
                    tbValue = VALUE_TB - ss->ply;

                if (wdl == TB::WDLCursedWin && drawScore)
                    tbValue = VALUE_DRAW + drawScore;

                if (wdl == TB::WDLBlessedLoss && drawScore)
                    tbValue = VALUE_DRAW - drawScore;

                if (wdl == TB::WDLDraw && drawScore)
                    tbValue = VALUE_DRAW;

                Bound b = wdl == TB::WDLLoss || wdl == TB::WDLBlessedLoss   ? BOUND_UPPER
                        : wdl == TB::WDLWin || wdl == TB::WDLCursedWin      ? BOUND_LOWER
                                                                            : BOUND_EXACT;

                if (b == BOUND_EXACT || (b == BOUND_LOWER ? tbValue >= beta : tbValue <= alpha))
                {
                    ttWriter.write(posKey, value_to_tt(tbValue, ss->ply), ss->ttPv, b,
                                   std::min(MAX_PLY - 1, depth + 6), Move::none(), VALUE_NONE,
                                   tt.generation());

                    return tbValue;
                }

                if (PvNode)
                {
                    if (b == BOUND_LOWER)
                    {
                        bestValue = tbValue;
                        alpha     = std::max(alpha, bestValue);
                    }
                    else
                        maxValue = tbValue;
                }
            }
        }
    }

    // Step 6. Static evaluation of the position
    if (inCheck)
    {
        // Skip early pruning when in check
        ss->staticEval = eval = VALUE_NONE;
        improving              = false;
        goto moves_loop;
    }
    else if (excludedMove)
    {
        // Providing the hint that this node's accumulator will be used often.
        eval = ss->staticEval;
    }
    else if (ss->ttHit)
    {
        // Never assume anything about values stored in TT
        ss->staticEval = eval = ttData.eval != VALUE_NONE ? ttData.eval : evaluate(pos);

        int cv = correction_value(*thisThread, pos, ss);
        eval   = to_corrected_static_eval(eval, cv);

        // ttValue can be used as a better position evaluation (~7 Elo)
        if (ttValue != VALUE_NONE
            && (ttData.bound & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    }
    else
    {
        ss->staticEval = eval = evaluate(pos);

        int cv = correction_value(*thisThread, pos, ss);
        eval   = to_corrected_static_eval(eval, cv);

        // Static evaluation is saved as it was before adjustment by correction history
        ttWriter.write(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_UNSEARCHED, Move::none(),
                       ss->staticEval, tt.generation());
    }

    // Use static evaluation difference to improve quiet move ordering (~9 Elo)
    if (((ss - 1)->currentMove).is_ok() && !(ss - 1)->inCheck && !priorCapture)
    {
        int bonus = std::clamp(-18 * int((ss - 1)->staticEval + ss->staticEval), -1817, 1817);
        bonus     = bonus > 0 ? 2 * bonus : bonus / 2;
        thisThread->mainHistory[~us][(ss - 1)->currentMove.raw()] << bonus;
    }

    // Set up the improving flag, which is true if current static evaluation is
    // bigger than the previous static evaluation (from our point of view).
    // The previous static evaluation is at most 2 plies ago. Improving is set
    // to true when no suitable previous evaluation can be found.
    improving = (ss - 2)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 2)->staticEval
              : (ss - 4)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 4)->staticEval
                                                   : true;
    opponentWorsening = ss->staticEval + (ss - 1)->staticEval > 2;

    // Step 7. Razoring (~1 Elo)
    // If eval is really low, check with qsearch if it can exceed alpha. If it can't,
    // return a fail low.
    if (eval < alpha - 494 - 290 * depth * depth)
    {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
            return value;
    }

    // Step 8. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 11
        && eval - 67 * depth + 141 >= beta
        && eval >= beta && eval < VALUE_TB_WIN_IN_MAX_PLY && (!ttMove || ttCapture))
        return beta > VALUE_TB_LOSS_IN_MAX_PLY ? (eval + beta) / 2 : eval;

    // Step 9. Null move search with verification search (~35 Elo)
    if (!PvNode && (ss - 1)->currentMove != Move::null() && (ss - 1)->statScore < 16620
        && eval >= beta && eval >= ss->staticEval && ss->staticEval >= beta - 21 * depth + 390
        && !excludedMove && pos.non_pawn_material(us) && ss->ply >= thisThread->nmpMinPly
        && beta > VALUE_TB_LOSS_IN_MAX_PLY)
    {
        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth and eval
        Depth R = std::min(int(eval - beta) / 172, 6) + depth / 3 + 5;

        ss->currentMove         = Move::null();
        ss->continuationHistory = &thisThread->continuationHistory[0][0][NO_PIECE][0];
        ss->continuationCorrectionHistory =
          &thisThread->continuationCorrectionHistory[NO_PIECE][0];

        do_null_move(pos, st, ss);

        Value nullValue = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, false);

        undo_null_move(pos);

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && nullValue < VALUE_TB_WIN_IN_MAX_PLY)
        {
            if (thisThread->nmpMinPly || depth < 16)
                return nullValue;

            assert(!thisThread->nmpMinPly);  // Recursive verification is not allowed

            // Do verification search at high depths, with null move pruning disabled
            // until ply exceeds nmpMinPly.
            thisThread->nmpMinPly = ss->ply + 3 * (depth - R) / 4;

            Value v = search<NonPV>(pos, ss, beta - 1, beta, depth - R, false);

            thisThread->nmpMinPly = 0;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 10. Internal iterative reductions (~9 Elo)
    if (PvNode && !ttMove)
        depth -= 3;

    if (cutNode && depth >= 7 && !ttMove)
        depth -= 2;

    // Use qsearch if depth is non-positive after reductions
    if (depth <= 0)
        return qsearch<PvNode ? PV : NonPV>(pos, ss, alpha, beta);

    // Step 11. ProbCut (~10 Elo)
    // If we have a good enough capture (or queen promotion) and a reduced search returns a value
    // much above beta, we can (almost) safely prune the previous move.
    probCutBeta = beta + 174 - 64 * improving;
    if (
      !PvNode && depth > 3
      && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
      // If value from transposition table is lower than probCutBeta, don't attempt probCut
      // there and in further interactions with transposition table cutoff depth is set to depth - 3
      // because probCut search has depth set to depth - 4 but we also do a determine check
      && !(ttData.depth >= depth - 3 && ttValue != VALUE_NONE && ttValue < probCutBeta))
    {
        assert(googly-free);
        // `probCutBeta` threshold check is done here for completeness.
        // The MovePicker is initialized with threshold = probCutBeta - ss->staticEval.
        // The captures are scored, and those that don't pass see_ge(threshold) are filtered out.
        MovePicker mp(pos, ttMove, probCutBeta - ss->staticEval, &thisThread->captureHistory);

        while ((move = mp.next_move()) != Move::none())
        {
            if (move != excludedMove && pos.legal(move))
            {
                assert(pos.capture_stage(move));

                movedPiece = pos.moved_piece(move);
                givesCheck = pos.gives_check(move);

                // Prefetch the TT entry for the resulting position
                prefetch(tt.first_entry(pos.key_after(move)));

                ss->currentMove = move;
                ss->continuationHistory =
                  &thisThread
                     ->continuationHistory[ss->inCheck][true][movedPiece][move.to_sq()];
                ss->continuationCorrectionHistory =
                  &thisThread->continuationCorrectionHistory[movedPiece][move.to_sq()];

                thisThread->nodes.fetch_add(1, std::memory_order_relaxed);
                do_move(pos, move, st, givesCheck, ss);

                // Perform a preliminary qsearch to verify that the move holds
                value = -qsearch<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

                // If the qsearch held, perform the regular search
                if (value >= probCutBeta)
                    value = -search<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, depth - 4,
                                           !cutNode);

                undo_move(pos, move);

                if (value >= probCutBeta)
                {
                    // Save ProbCut data into transposition table
                    ttWriter.write(posKey, value_to_tt(value, ss->ply), ss->ttPv, BOUND_LOWER,
                                   depth - 3, move, ss->staticEval, tt.generation());
                    return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probCutBeta - beta)
                                                                     : value;
                }
            }
        }
    }

moves_loop:  // When in check, search starts here

    // Step 12. A]  Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    // B]  Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is higher than or equal to 1, only
    // captures and quiet checks are generated.
    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory,
                                        (ss - 3)->continuationHistory,
                                        (ss - 4)->continuationHistory,
                                        nullptr,
                                        (ss - 6)->continuationHistory};

    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory, &thisThread->lowPlyHistory,
                  &thisThread->captureHistory, contHist, &sharedHistory, ss->ply);

    int     lmpCount        = 0;
    bool    skipQuiets       = false;
    bool    ttMoveIsSingular = false;
    int     doubleExtensions = 0;

    SearchedList quietsSearchedList, capturesSearchedList;

    while ((move = mp.next_move(skipQuiets)) != Move::none())
    {
        assert(move.is_ok());

        if (move == excludedMove)
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in Root
        // Move List. In MultiPV mode we also skip PV moves that have been already
        // searched and those of lower "TB rank" if we are in a TB root position.
        if (rootNode
            && !std::count(rootMoves.begin() + pvIdx, rootMoves.begin() + pvLast, move))
            continue;

        // Check for legality
        if (!rootNode && !pos.legal(move))
            continue;

        ss->moveCount = ++moveCount;

        if (rootNode && is_mainthread() && elapsed_time() > 3000)
            main_manager()->updates.onIter(
              {rootDepth, UCIEngine::move(move, pos.is_chess960()), moveCount});

        if (PvNode)
            (ss + 1)->pv = nullptr;

        extension  = 0;
        capture    = pos.capture_stage(move);
        movedPiece = pos.moved_piece(move);
        givesCheck = pos.gives_check(move);

        // Calculate new depth for this move
        newDepth = depth - 1;

        int delta = beta - alpha;

        Depth r = reduction(improving, depth, moveCount, delta);

        // Step 13. Pruning at shallow depth (~120 Elo).
        // Depth conditions are important for mate finding.
        if (!rootNode && pos.non_pawn_material(us) && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
        {
            // Skip quiet moves if movecount exceeds our FutilityMoveCount threshold (~8 Elo)
            if (!skipQuiets)
                skipQuiets = moveCount >= (3 + depth * depth) / (2 - improving);

            // Reduced depth of the next LMR search
            int lmrDepth = newDepth - r;

            if (capture || givesCheck)
            {
                // Futility pruning for captures (~2 Elo)
                if (!givesCheck && lmrDepth < 7 && !ss->inCheck)
                {
                    Piece capturedPiece = pos.piece_on(move.to_sq());
                    int   futilityEval =
                      ss->staticEval + 238 + 305 * lmrDepth + PieceValue[capturedPiece]
                      + thisThread->captureHistory[movedPiece][move.to_sq()]
                                                  [type_of(capturedPiece)]
                          / 7;
                    if (futilityEval < alpha)
                        continue;
                }

                // SEE pruning for captures and checks (~11 Elo)
                int seeHist = std::max(0, thisThread->captureHistory[movedPiece][move.to_sq()]
                                            [type_of(pos.piece_on(move.to_sq()))]);
                if (!pos.see_ge(move, -50 * depth + seeHist / 14))
                    continue;
            }
            else
            {
                // Continuation history based pruning (~2 Elo)
                if (lmrDepth < 6 && (*contHist[0])[movedPiece][move.to_sq()] < -3752 * depth + 200)
                    continue;

                // Futility pruning: parent node (~13 Elo)
                if (!ss->inCheck && lmrDepth < 14
                    && ss->staticEval + (bestValue < ss->staticEval - 57 ? 124 : 71)
                           + 118 * lmrDepth
                         <= alpha)
                    continue;

                lmrDepth = std::max(lmrDepth, 0);

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, -24 * lmrDepth * lmrDepth - 15 * lmrDepth))
                    continue;
            }
        }

        // Step 14. Extensions (~66 Elo)

        // Singular extension search (~58 Elo), excludedMove applied similarly
        // to a null-window search around ttValue, for ttMove at higher depths.
        if (!rootNode && depth >= 4 + 2 * (PvNode && ttData.is_pv) && move == ttMove
            && !excludedMove
            && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && (ttData.bound & BOUND_LOWER)
            && ttData.depth >= depth - 3)
        {
            Value singularBeta  = ttValue - (58 + 53 * (ss->ttPv && !PvNode)) * depth / 64;
            Depth singularDepth = newDepth / 2;

            ss->excludedMove = move;
            value = search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
            ss->excludedMove = Move::none();

            if (value < singularBeta)
            {
                int doubleMargin = 262 * PvNode - 204 * !ttCapture;
                int tripleMargin = 97 + 265 * PvNode - 281 * !ttCapture + 159 * ss->ttPv;

                extension = 1;
                ttMoveIsSingular = true;

                if (!PvNode && value < singularBeta - doubleMargin && (ss - 1)->cutoffCnt <= 3)
                    extension = 2 + (value < singularBeta - tripleMargin);
            }

            // Multi-cut pruning
            // Our ttMove is assumed to fail high based on the bound of the TT entry,
            // and if after excluding the ttMove with a reduced search we fail high over the original beta,
            // we assume this expected cut-node is not singular (multiple moves fail high),
            // and we can prune the whole subtree by returning a softbound.
            else if (singularBeta >= beta)
                return singularBeta;

            // Negative extensions
            // If other moves are searched and the ttMove is not singular, reduce the ttMove
            else if (ttValue >= beta)
                extension = -2 - !PvNode;

            else if (cutNode)
                extension = depth < 19 ? -2 : -1;

            else if (ttValue <= value)
                extension = -1;
        }

        // Check extensions (~1 Elo)
        else if (givesCheck && depth > 9)
            extension = 1;

        // Quiet ttMove extensions (~1 Elo)
        else if (PvNode && move == ttMove && move == (ss - 2)->currentMove.raw()
                 && thisThread->ttMoveHistory << 0, ttData.depth >= depth - 3
                 && ss->ply < 2 * rootDepth)
            extension = 1;

        // Add extension to new depth
        newDepth += extension;
        ss->reduction = 0;

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        // Update the current move (this must be done after singular extension search)
        ss->currentMove = move;
        ss->continuationHistory =
          &thisThread->continuationHistory[ss->inCheck][capture][movedPiece][move.to_sq()];
        ss->continuationCorrectionHistory =
          &thisThread->continuationCorrectionHistory[movedPiece][move.to_sq()];

        thisThread->nodes.fetch_add(1, std::memory_order_relaxed);

        // Step 15. Make the move
        do_move(pos, move, st, givesCheck, ss);

        // Decrease reduction if position is or has been on the PV (~7 Elo)
        if (ss->ttPv)
            r -= 1 + (ttData.value > alpha) + (ttData.depth >= depth);

        // Decrease reduction for PvNodes (~0 Elo on STC, ~2 Elo on LTC)
        if (PvNode)
            r--;

        // These reduction adjustments have proven non-linear scaling.
        // They are optimized to time controls of 180 + 1.8 and longer so
        // changing them or adding conditions that are similar requires
        // tests at these types of time controls.

        if (capture)
        {
            // Increase reduction for captures that have a poor history (~5 Elo)
            r += !inCheck ? 2 : 1;
            r -= thisThread->captureHistory[movedPiece][move.to_sq()]
                                           [type_of(pos.captured_piece())]
               / 5304;
        }
        else
        {
            // Increase reduction if other moves have failed high recently,
            // indicating the current node is likely a cut-node (~3 Elo)
            if (cutNode)
                r += 1 + !ttMoveIsSingular;

            // Increase reduction if ttMove is a capture (~3 Elo)
            if (ttCapture)
                r++;

            // Decrease reduction for moves that give check and not an extension
            if (givesCheck)
                r--;

            ss->statScore = 2 * thisThread->mainHistory[us][move.raw()]
                          + (*contHist[0])[movedPiece][move.to_sq()]
                          + (*contHist[1])[movedPiece][move.to_sq()]
                          + (*contHist[3])[movedPiece][move.to_sq()]
                          - 4006;

            // Decrease/increase reduction for moves with a good/bad history (~8 Elo)
            r -= ss->statScore / 7801;

            // Step 16. Late moves reduction / extension (LMR, ~117 Elo)
            // Increase reduction for shuffling
            if (is_shuffling(move, ss, pos))
                r += 2;
        }

        // Step 16. Late moves reduction / extension (LMR, ~117 Elo)
        // If the move fails high it will be re-searched at full depth.
        if (depth >= 2 && moveCount > 1 + rootNode)
        {
            // In general we want to cap the LMR depth search at newDepth, but when
            // eval is not much above alpha we will also allow for a reduction of an extra ply.
            Depth d = std::max(1, std::min(newDepth - r, newDepth + !PvNode - 1));

            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result was
                // good enough search deeper, if it was bad enough search shallower.
                const bool doDeeperSearch    = value > (bestValue + 42 + 2 * newDepth);  // (~1 Elo)
                const bool doShallowerSearch = value < bestValue + newDepth;              // (~2 Elo)

                newDepth += doDeeperSearch - doShallowerSearch;

                if (newDepth > d)
                    value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                // Post LMR continuation history updates (~1 Elo)
                int bonus = value <= alpha ? -stat_bonus(newDepth)
                          : value >= beta ? stat_bonus(newDepth)
                                          : 0;

                update_continuation_histories(ss, movedPiece, move.to_sq(), bonus);
            }
        }

        // Step 17. Full-depth search when LMR is skipped.
        else if (!PvNode || moveCount > 1)
        {
            // Increase reduction for cut nodes without ttMove (~1 Elo)
            if (!ttMove && cutNode)
                r += 2;

            // Note that if expected reduction is high, the search is done at
            // low depth and target alpha, so result is unreliable. Placeholder.
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha,
                                   newDepth - (r > 3), !cutNode);
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if (PvNode && (moveCount == 1 || value > alpha))
        {
            (ss + 1)->pv    = pv;
            (ss + 1)->pv[0] = Move::none();

            // Aspiration window is narrower for PV searches at higher depths.
            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth, false);
        }

        // Step 18. Undo move
        undo_move(pos, move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 19. Check for a new best move.
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and we return immediately without
        // updating best move, PV and TT.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (rootNode)
        {
            RootMove& rm = *std::find(rootMoves.begin(), rootMoves.end(), move);

            rm.effort += nodes.load(std::memory_order_relaxed) - nodesBefore;
            rm.averageScore =
              rm.averageScore != -VALUE_INFINITE ? (2 * value + rm.averageScore) / 3 : value;
            rm.meanSquaredScore =
              rm.meanSquaredScore != -VALUE_INFINITE * VALUE_INFINITE
                ? (4 * int64_t(value) * value + rm.meanSquaredScore) / 5
                : value * value;

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.score = rm.uciScore = value;
                rm.scoreLowerbound = rm.scoreUpperbound = false;
                rm.selDepth                             = thisThread->selDepth;

                if (value >= beta)
                {
                    rm.scoreLowerbound = true;
                    rm.uciScore        = beta;
                }
                else if (value <= alpha)
                {
                    rm.scoreUpperbound = true;
                    rm.uciScore        = alpha;
                }

                rm.pv.resize(1);

                assert((ss + 1)->pv);

                for (Move* m = (ss + 1)->pv; *m != Move::none(); ++m)
                    rm.pv.push_back(*m);

                // We record how often the best move has been changed in each iteration.
                // This information is used for time management. In MultiPV mode,
                // we must take care to only do this for the first PV line.
                if (moveCount > 1 && !pvIdx)
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
                    if (depth > 2 && depth < 13 && beta < 13252 && value > -11059)
                        depth -= 2;

                    assert(depth > 0);
                    alpha = value;  // Update alpha! Always alpha < beta
                }
            }
        }

        // If the move is worse than some previously searched move,
        // remember it, to update its stats later.
        if (move != bestMove && moveCount <= SEARCHEDLIST_CAPACITY)
        {
            if (capture)
                capturesSearchedList.push_back(move);
            else
                quietsSearchedList.push_back(move);
        }
    }

    // Step 20. Check for mate and stalemate.
    // All legal moves have been searched and if there are no legal moves, it
    // must be a mate or a stalemate. If we are in a singular extension search then
    // return a fail low score.
    assert(moveCount || !inCheck || excludedMove || !MoveList<LEGAL>(pos).size());

    if (!moveCount)
        bestValue = excludedMove ? alpha : inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha,
    // we update the stats of searched moves.
    else if (bestMove)
        update_all_stats(pos, ss, *thisThread, bestMove, prevSq, quietsSearchedList,
                         capturesSearchedList, depth, ttMove);

    // Bonus for prior countermove that caused the fail low
    else if (!priorCapture && prevSq != SQ_NONE)
    {
        int bonus = (depth > 5) + (PvNode || cutNode) + ((ss - 1)->statScore < -14397)
                  + ((ss - 1)->moveCount > 10)
                  + (!inCheck && bestValue <= ss->staticEval - 100);
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                      stat_bonus(depth) * bonus);
        thisThread->mainHistory[~us][(ss - 1)->currentMove.raw()]
          << stat_bonus(depth) * bonus / 2;
    }

    if (PvNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the penalty for it is low.
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write to the transposition table
    if (!excludedMove && !(rootNode && thisThread->pvIdx))
        ttWriter.write(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                       bestValue >= beta    ? BOUND_LOWER
                       : PvNode && bestMove ? BOUND_EXACT
                                            : BOUND_UPPER,
                       depth, bestMove, ss->staticEval, tt.generation());

    // Adjust correction history
    if (!inCheck && (!bestMove || !pos.capture_stage(bestMove))
        && !(bestValue >= beta && bestValue <= ss->staticEval)
        && !(!bestMove && bestValue >= ss->staticEval))
    {
        int bonus =
          std::clamp(int(bestValue - ss->staticEval) * depth / 8, -CORRECTION_HISTORY_LIMIT / 4,
                     CORRECTION_HISTORY_LIMIT / 4);

        update_correction_history(pos, ss, *thisThread, bonus);
    }

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}

// Quiescence search function, which is called by the main search
template<NodeType nodeType>
Value Search::Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));

    // Check if we have an upcoming move that draws by repetition
    if (alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
    {
        alpha = value_draw(nodes);
        if (alpha >= beta)
            return alpha;
    }

    Move      pv[MAX_PLY + 1];
    StateInfo st;
    TTData    ttData;

    Key   posKey;
    Move  move, bestMove;
    Value bestValue, value, futilityBase;
    bool  pvHit;

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && selDepth < ss->ply + 1)
        selDepth = ss->ply + 1;

    bestMove  = Move::none();
    bestValue = -VALUE_INFINITE;

    // Step 1. Check for an immediate draw or maximum ply reached
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY)
        return (ss->ply >= MAX_PLY && !pos.checkers()) ? evaluate(pos)
                                                       : value_draw(nodes);

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    bool inCheck = pos.checkers();

    // Step 2. Transposition table lookup
    posKey                                 = pos.key();
    auto [ttHit, ttDataCopy, ttWriter]     = tt.probe(posKey);
    ttData                                 = ttDataCopy;
    ss->ttHit                              = ttHit;
    Value ttValue = ttHit ? value_from_tt(ttData.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move  ttMove  = ttHit ? ttData.move : Move::none();
    pvHit         = ttHit && ttData.is_pv;

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && ttData.depth >= DEPTH_QS && ttValue != VALUE_NONE
        && (ttData.bound & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    // Step 3. Static evaluation of the position
    if (inCheck)
    {
        bestValue = futilityBase = -VALUE_INFINITE;
    }
    else
    {
        if (ttHit)
        {
            // Never assume anything about values stored in TT
            if ((ss->staticEval = bestValue = ttData.eval) == VALUE_NONE)
                ss->staticEval = bestValue = evaluate(pos);

            int cv = correction_value(*this, pos, ss);
            bestValue = to_corrected_static_eval(bestValue, cv);

            // ttValue can be used as a better position evaluation
            if (ttValue != VALUE_NONE
                && (ttData.bound & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttValue;
        }
        else
        {
            ss->staticEval = bestValue = evaluate(pos);

            int cv = correction_value(*this, pos, ss);
            bestValue = to_corrected_static_eval(bestValue, cv);
        }

        if (bestValue >= beta)
        {
            if (!ttHit)
                ttWriter.write(posKey, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER,
                               DEPTH_UNSEARCHED, Move::none(), ss->staticEval, tt.generation());

            return bestValue;
        }

        if (bestValue > alpha)
            alpha = bestValue;

        futilityBase = ss->staticEval + 200;
    }

    // Initialize a MovePicker object for the current position, and prepare to search
    // the moves. We presently generate all captures incl. queen promotions, plus
    // knight promotions (which often enough are the best move in a position).
    Square     prevSq  = ((ss - 1)->currentMove).is_ok() ? (ss - 1)->currentMove.to_sq() : SQ_NONE;
    MovePicker mp(pos, ttMove, depth, &captureHistory);

    int moveCount = 0;

    // Step 4. Loop through all pseudo-legal moves until no moves remain or a beta cutoff
    while ((move = mp.next_move()) != Move::none())
    {
        assert(move.is_ok());

        // Check for legality
        if (!pos.legal(move))
            continue;

        moveCount++;

        givesCheck = pos.gives_check(move);
        capture    = pos.capture_stage(move);

        movedPiece = pos.moved_piece(move);

        // Step 5. Futility pruning and target moves
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && !givesCheck
            && futilityBase > VALUE_TB_LOSS_IN_MAX_PLY
            && move.type_of() != PROMOTION)
        {
            if (moveCount > 2)
                continue;

            Value futilityValue = futilityBase + PieceValue[pos.piece_on(move.to_sq())];

            // If the futility value is not sufficient, skip this move.
            if (futilityValue <= alpha)
            {
                bestValue = std::max(bestValue, futilityValue);
                continue;
            }

            // If the futility check doesn't fail, do the SEE check.
            if (futilityBase <= alpha && !pos.see_ge(move, 1))
            {
                bestValue = std::max(bestValue, futilityBase);
                continue;
            }

            // For moves that give check, we use a loosened SEE threshold
            if (futilityBase > alpha && !pos.see_ge(move, -71))
                continue;
        }

        // Do not search moves with bad enough SEE values (~5 Elo)
        if (!inCheck && !pos.see_ge(move, -77))
            continue;

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        ss->currentMove = move;
        ss->continuationHistory =
          &continuationHistory[ss->inCheck][capture][movedPiece][move.to_sq()];
        ss->continuationCorrectionHistory =
          &continuationCorrectionHistory[movedPiece][move.to_sq()];

        nodes.fetch_add(1, std::memory_order_relaxed);

        // Step 6. Make and search the move
        do_move(pos, move, st, givesCheck, ss);
        value = -qsearch<nodeType>(pos, ss + 1, -beta, -alpha);
        undo_move(pos, move);

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
    if (inCheck && bestValue == -VALUE_INFINITE)
    {
        assert(!MoveList<LEGAL>(pos).size());
        return mated_in(ss->ply);  // Plies to mate from the root
    }

    if (std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY && bestValue >= beta)
        bestValue = (3 * bestValue + beta) / 4;

    // Save gathered info in transposition table. The static evaluation
    // is saved as it was before adjustment by correction history.
    ttWriter.write(posKey, value_to_tt(bestValue, ss->ply), pvHit,
                   bestValue >= beta ? BOUND_LOWER : BOUND_UPPER, DEPTH_QS, bestMove,
                   ss->staticEval, tt.generation());

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}

namespace {

// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);

    return v >= VALUE_TB_WIN_IN_MAX_PLY ? v + ply : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score from the transposition table
// (which refers to the plies to mate/be mated from current position) to "plies to
// mate/be mated (TB) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, we return an estimate of VALUE_MATE_IN_MAX_PLY
// or VALUE_MATED_IN_MAX_PLY if the position has more than a threshold rule50 count.
Value value_from_tt(Value v, int ply, int r50c) {

    if (v == VALUE_NONE)
        return VALUE_NONE;

    // Handle win
    if (v >= VALUE_TB_WIN_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate score
        if (v >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - v > 100 - r50c)
            return VALUE_MATE_IN_MAX_PLY - 1;  // largest non-mate score

        // Downgrade a potentially false TB score
        if (VALUE_TB - v > 100 - r50c)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;  // largest non-TB score

        return v - ply;
    }

    // Handle loss
    if (v <= VALUE_TB_LOSS_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate score
        if (v <= VALUE_MATED_IN_MAX_PLY && VALUE_MATE + v > 100 - r50c)
            return VALUE_MATED_IN_MAX_PLY + 1;  // smallest non-mate score

        // Downgrade a potentially false TB score
        if (VALUE_TB + v > 100 - r50c)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;  // smallest non-TB score

        return v + ply;
    }

    return v;
}

// Appends the move and child pv
void update_pv(Move* pv, Move move, const Move* childPv) {

    for (*pv++ = move; childPv && *childPv != Move::none();)
        *pv++ = *childPv++;
    *pv = Move::none();
}

// Updates histories of the move pairs formed by moves
// at ply -1, -2, -3, -4, and -6 with current move.
void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {

    static const ConthistBonus conthistBonuses[] = {
      {1, 1}, {2, 1}, {3, 1}, {4, 1}, {6, 1},
    };

    for (const auto& [i, w] : conthistBonuses)
    {
        if (((ss - i)->currentMove).is_ok())
            (*(ss - i)->continuationHistory)[pc][to] << bonus * w;
    }
}

// Updates move sorting heuristics
void update_quiet_histories(
  const Position& pos, Stack* ss, Search::Worker& workerThread, Move move, int bonus) {

    Color us = pos.side_to_move();
    workerThread.mainHistory[us][move.raw()] << bonus;

    update_continuation_histories(ss, pos.moved_piece(move), move.to_sq(), bonus);

    int pBonus = std::clamp(bonus, -222, 365);
    workerThread.sharedHistory.pawn_entry(pos)[pos.moved_piece(move)][move.to_sq()] << pBonus;
}

// Updates stats at the end of search() when a bestMove is found
void update_all_stats(const Position& pos,
                      Stack*          ss,
                      Search::Worker& workerThread,
                      Move            bestMove,
                      Square          prevSq,
                      SearchedList&   quietsSearched,
                      SearchedList&   capturesSearched,
                      Depth           depth,
                      Move            ttMove) {

    Color                    us             = pos.side_to_move();
    bool                     quiet          = !pos.capture_stage(bestMove);
    int                      bestMoveBonus  = stat_bonus(depth + 1);
    PieceType                captured;

    if (quiet)
    {
        update_quiet_histories(pos, ss, workerThread, bestMove, bestMoveBonus);

        // Decrease stats for all non-best quiet moves
        for (int i = 0; i < quietsSearched.ssize(); ++i)
        {
            int bonus1 = -bestMoveBonus;
            update_quiet_histories(pos, ss, workerThread, quietsSearched[i], bonus1);
        }
    }
    else
    {
        // Increase stats for the best move in case it is a capture move
        captured = type_of(pos.piece_on(bestMove.to_sq()));
        workerThread.captureHistory[pos.moved_piece(bestMove)][bestMove.to_sq()][captured]
          << bestMoveBonus;
    }

    // Extra penalty for a quiet early move that was not a TT move in
    // previous ply when it gets refuted.
    if (prevSq != SQ_NONE && ((ss - 1)->moveCount == 1 + (ttMove != Move::none()))
        && !pos.captured_piece())
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                      -stat_bonus(depth + 1));

    // Decrease stats for all non-best capture moves
    for (int i = 0; i < capturesSearched.ssize(); ++i)
    {
        Move     m  = capturesSearched[i];
        captured    = type_of(pos.piece_on(m.to_sq()));
        workerThread.captureHistory[pos.moved_piece(m)][m.to_sq()][captured]
          << -bestMoveBonus;
    }

    // TTMove history update
    if (prevSq != SQ_NONE && (ss - 1)->currentMove.is_ok())
    {
        Piece pc = pos.piece_on(prevSq);
        workerThread.ttMoveHistory << stat_bonus(depth + 1) + 2 * bestMoveBonus + 64;
    }
}

}  // namespace

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move Skill::pick_best(const RootMoves& rootMoves, size_t multiPV) {

    static PRNG rng(now());  // PRNG sequence should be non-deterministic

    // RootMoves are already sorted by score in descending order
    Value  topScore = rootMoves[0].score;
    int    delta    = std::min(topScore - rootMoves[multiPV - 1].score, PawnValue);
    int    maxScore = -VALUE_INFINITE;
    double weakness = 120 - 2 * level;

    // Choose best move. For each move score we add two terms, both dependent on
    // weakness. One is deterministic and bigger for weaker levels, and one is
    // random. Then we choose the move with the highest resulting score.
    for (size_t i = 0; i < multiPV; ++i)
    {
        // This is our magic formula
        int push = int((weakness * int(topScore - rootMoves[i].score)
                        + delta * (rng.rand<unsigned>() % int(weakness)))
                       / 128);

        if (rootMoves[i].score + push >= maxScore)
        {
            maxScore = rootMoves[i].score + push;
            best     = rootMoves[i].pv[0];
        }
    }

    return best;
}

// Used to print debug info and, more importantly, to detect
// when we are out of available time and thus stop the search.
void SearchManager::check_time(Search::Worker& worker) {
    if (--callsCnt > 0)
        return;

    // When using nodes, ensure checking rate is not lower than 0.1% of nodes
    callsCnt = worker.limits.nodes ? std::min(512, int(worker.limits.nodes / 1024)) : 512;

    static TimePoint lastInfoTime = now();

    TimePoint elapsed = worker.elapsed();
    TimePoint tick    = worker.limits.startTime + std::min(elapsed, TimePoint(100));

    if (tick < lastInfoTime + 1000)
        return;

    lastInfoTime = tick;

    dbg_print();

    // Poll for stop on ponder
    if (ponder)
        return;

    if ((worker.limits.use_time_management() && (elapsed > tm.maximum() || stopOnPonderhit))
        || (worker.limits.movetime && elapsed >= worker.limits.movetime)
        || (worker.limits.nodes && worker.threads.nodes_searched() >= worker.limits.nodes))
        worker.threads.stop = true;
}

void SearchManager::pv(Search::Worker&           worker,
                       const ThreadPool&         threads,
                       const TranspositionTable& tt,
                       Depth                     depth) {

    const auto  nodes     = threads.nodes_searched();
    const auto  tb_hits   = threads.tb_hits();
    const auto  hashfull  = tt.hashfull(worker.completedDepth);
    const auto  time      = std::max(TimePoint(1), worker.elapsed());
    const auto  multiPV   = size_t(worker.options["MultiPV"]);
    const auto  showWDL   = bool(worker.options["UCI_ShowWDL"]);

    for (size_t i = 0; i < multiPV; ++i)
    {
        bool updated = worker.rootMoves[i].score != -VALUE_INFINITE;

        if (depth == 1 && !updated && i > 0)
            continue;

        Depth d = updated ? depth : std::max(1, depth - 1);

        Value v = updated ? worker.rootMoves[i].uciScore : worker.rootMoves[i].previousScore;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool isExact    = !worker.rootMoves[i].scoreLowerbound && !worker.rootMoves[i].scoreUpperbound;
        auto bound_flag = worker.rootMoves[i].scoreLowerbound ? " lowerbound"
                        : worker.rootMoves[i].scoreUpperbound ? " upperbound"
                                                              : "";

        // Potentially correct and extend the PV, and in exceptional cases
        // (e.g., stopped search) the score. This is not done at lower MultiPV lines as
        // those are less critical.
        if (i == 0 && updated)
            syzygy_extend_pv(worker.options, worker.limits, worker.rootPos, worker.rootMoves[0],
                             v);

        std::string pv;
        for (auto m : worker.rootMoves[i].pv)
            pv += UCIEngine::move(m, worker.rootPos.is_chess960()) + " ";

        // remove last whitespace
        if (!pv.empty())
            pv.pop_back();

        auto wdl = showWDL ? UCIEngine::wdl(v, worker.rootPos) : "";

        updates.onUpdateFull({d, {v, worker.rootPos}, worker.rootMoves[i].selDepth, i + 1, wdl,
                              bound_flag, size_t(time), nodes, nodes / time, tb_hits, pv,
                              hashfull});
    }
}

// Called in case we have no ponder move before exiting the search,
// for instance, in case we stop the search during a fail high at root.
// We try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' we have nothing to think about.
bool RootMove::extract_ponder_from_tt(const TranspositionTable& tt, Position& pos) {
    StateInfo st;
    assert(pv.size() == 1);

    if (pv[0] == Move::none())
        return false;

    pos.do_move(pv[0], st);

    auto [ttHit, ttData, ttWriter] = tt.probe(pos.key());

    pos.undo_move(pv[0]);

    if (ttHit)
    {
        Move m = ttData.move;
        if (MoveList<LEGAL>(pos).contains(m))
            return pv.push_back(m), true;
    }

    return false;
}

// Used by search to reset histories, usually before a new game
void Search::Worker::clear() {
    mainHistory.fill(0);
    lowPlyHistory.fill(0);
    captureHistory.fill(-700);
    ttMoveHistory = 0;

    for (bool inCheck : {false, true})
        for (StatsType c : {NoCaptures, Captures})
            for (auto& to : continuationHistory[inCheck][c])
                for (auto& h : to)
                    h.fill(-58);

    continuationCorrectionHistory.fill(0);

    sharedHistory.correctionHistory.clear_range(0, numaThreadIdx, numaTotal);
    sharedHistory.pawnHistory.clear_range(0, numaThreadIdx, numaTotal);

    for (size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = int(19.80 * std::log(i));
}

void syzygy_extend_pv(const OptionsMap&         options,
                      const Search::LimitsType& limits,
                      Stockfish::Position&      pos,
                      Search::RootMove&         rootMove,
                      Value&                    v) {

    auto dominated = [](const Search::RootMove& rm) {
        return rm.score != -VALUE_INFINITE
            && is_decisive(rm.score);
    };

    // Do not extend in time-limited searches
    if (limits.time[WHITE] || limits.time[BLACK])
        return;

    // Do not extend when we have a mate score
    if (std::abs(v) >= VALUE_MATE_IN_MAX_PLY)
        return;

    int  pieces    = pos.count<ALL_PIECES>();
    bool dominated_result = dominated(rootMove);

    // Only extend if we are in a TB position with few pieces
    if (pieces > int(options["SyzygyProbeLimit"]) || !dominated_result)
        return;

    // Use Syzygy tables to extend the PV
    // Get the rest of the PV from DTZ tables
    StateListPtr states(new std::deque<StateInfo>);

    for (auto m : rootMove.pv)
    {
        states->emplace_back();
        pos.do_move(m, states->back());
    }

    // Let probe the DTZ tables
    bool keepGoing = true;
    while (keepGoing && !pos.is_draw(0))
    {
        TB::ProbeState result;
        int            dtz = TB::probe_dtz(pos, &result);

        if (result == TB::FAIL || result == TB::CHANGE_STM)
            break;

        // Extract the best move from the DTZ tables
        auto rootMoves = Search::RootMoves{};
        for (auto m : MoveList<LEGAL>(pos))
            rootMoves.emplace_back(m);

        keepGoing = TB::root_probe_wdl(pos, rootMoves, options["Syzygy50MoveRule"]);

        if (keepGoing && rootMoves.size())
        {
            Move bestDTZMove = rootMoves[0].pv[0];
            rootMove.pv.push_back(bestDTZMove);

            states->emplace_back();
            pos.do_move(bestDTZMove, states->back());
        }
        else
            break;
    }

    // Undo all moves to restore the original position
    for (int i = rootMove.pv.size() - 1; i >= 0; --i)
        pos.undo_move(rootMove.pv[i]);
}

}  // namespace Stockfish
