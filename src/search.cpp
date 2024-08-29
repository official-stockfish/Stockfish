/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_common.h"
#include "nnue/nnue_misc.h"
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

using Eval::evaluate;
using namespace Search;

namespace {

// Futility margin
Value futility_margin(Depth d, bool noTtCutNode, bool improving, bool oppWorsening) {
    Value futilityMult       = 122 - 37 * noTtCutNode;
    Value improvingDeduction = improving * futilityMult * 2;
    Value worseningDeduction = oppWorsening * futilityMult / 3;

    return futilityMult * d - improvingDeduction - worseningDeduction;
}

constexpr int futility_move_count(bool improving, Depth depth) {
    return (3 + depth * depth) / (2 - improving);
}

// Add correctionHistory value to raw staticEval and guarantee evaluation
// does not hit the tablebase range.
Value to_corrected_static_eval(Value v, const Worker& w, const Position& pos) {
    auto cv = w.correctionHistory[pos.side_to_move()][pawn_structure_index<Correction>(pos)];
    v += 66 * cv / 512;
    return std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}

// History and stats update bonus, based on depth
int stat_bonus(Depth d) { return std::min(190 * d - 108, 1596); }

// History and stats update malus, based on depth
int stat_malus(Depth d) { return std::min(736 * d - 268, 2044); }

// Add a small random component to draw evaluations to avoid 3-fold blindness
Value value_draw(size_t nodes) { return VALUE_DRAW - 1 + Value(nodes & 0x2); }
Value value_to_tt(Value v, int ply);
Value value_from_tt(Value v, int ply, int r50c);
void  update_pv(Move* pv, Move move, const Move* childPv);
void  update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus);
void  update_quiet_histories(
   const Position& pos, Stack* ss, Search::Worker& workerThread, Move move, int bonus);
void update_all_stats(const Position&      pos,
                      Stack*               ss,
                      Search::Worker&      workerThread,
                      Move                 bestMove,
                      Square               prevSq,
                      ValueList<Move, 32>& quietsSearched,
                      ValueList<Move, 32>& capturesSearched,
                      Depth                depth);

}  // namespace

Search::Worker::Worker(SharedState&                    sharedState,
                       std::unique_ptr<ISearchManager> sm,
                       size_t                          threadId,
                       NumaReplicatedAccessToken       token) :
    // Unpack the SharedState struct into member variables
    threadIdx(threadId),
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

    if (int(options["MultiPV"]) == 1 && !limits.depth && !limits.mate && !skill.enabled()
        && rootMoves[0].pv[0] != Move::none())
        bestThread = threads.get_best_thread()->worker.get();

    main_manager()->bestPreviousScore        = bestThread->rootMoves[0].score;
    main_manager()->bestPreviousAverageScore = bestThread->rootMoves[0].averageScore;

    // Send again PV info if we have a new best thread
    if (bestThread != this)
        main_manager()->pv(*bestThread, threads, tt, bestThread->completedDepth);

    std::string ponder;

    if (bestThread->rootMoves[0].pv.size() > 1
        || bestThread->rootMoves[0].extract_ponder_from_tt(tt, rootPos))
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
    Value lastBestScore     = -VALUE_INFINITE;
    auto  lastBestPV        = std::vector{Move::none()};

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
          &this->continuationHistory[0][0][NO_PIECE][0];  // Use as a sentinel
        (ss - i)->staticEval = VALUE_NONE;
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
            Value avg = rootMoves[pvIdx].averageScore;
            delta     = 5 + avg * avg / 13424;
            alpha     = std::max(avg - delta, -VALUE_INFINITE);
            beta      = std::min(avg + delta, VALUE_INFINITE);

            // Adjust optimism based on root move's averageScore (~4 Elo)
            optimism[us]  = 125 * avg / (std::abs(avg) + 89);
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
                    beta  = (alpha + beta) / 2;
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

                delta += delta / 3;

                assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
            }

            // Sort the PV lines searched so far and update the GUI
            std::stable_sort(rootMoves.begin() + pvFirst, rootMoves.begin() + pvIdx + 1);

            if (mainThread
                && (threads.stop || pvIdx + 1 == multiPV || nodes > 10000000)
                // A thread that aborted search can have mated-in/TB-loss PV and
                // score that cannot be trusted, i.e. it can be delayed or refuted
                // if we would have had time to fully search other root-moves. Thus
                // we suppress this output and below pick a proven score/PV for this
                // thread (from the previous iteration).
                && !(threads.abortedSearch && rootMoves[0].uciScore <= VALUE_TB_LOSS_IN_MAX_PLY))
                main_manager()->pv(*this, threads, tt, rootDepth);

            if (threads.stop)
                break;
        }

        if (!threads.stop)
            completedDepth = rootDepth;

        // We make sure not to pick an unproven mated-in score,
        // in case this thread prematurely stopped search (aborted-search).
        if (threads.abortedSearch && rootMoves[0].score != -VALUE_INFINITE
            && rootMoves[0].score <= VALUE_TB_LOSS_IN_MAX_PLY)
        {
            // Bring the last best move to the front for best thread selection.
            Utility::move_to_front(rootMoves, [&lastBestPV = std::as_const(lastBestPV)](
                                                const auto& rm) { return rm == lastBestPV[0]; });
            rootMoves[0].pv    = lastBestPV;
            rootMoves[0].score = rootMoves[0].uciScore = lastBestScore;
        }
        else if (rootMoves[0].pv[0] != lastBestPV[0])
        {
            lastBestPV        = rootMoves[0].pv;
            lastBestScore     = rootMoves[0].score;
            lastBestMoveDepth = rootDepth;
        }

        if (!mainThread)
            continue;

        // Have we found a "mate in x"?
        if (limits.mate && rootMoves[0].score == rootMoves[0].uciScore
            && ((rootMoves[0].score >= VALUE_MATE_IN_MAX_PLY
                 && VALUE_MATE - rootMoves[0].score <= 2 * limits.mate)
                || (rootMoves[0].score != -VALUE_INFINITE
                    && rootMoves[0].score <= VALUE_MATED_IN_MAX_PLY
                    && VALUE_MATE + rootMoves[0].score <= 2 * limits.mate)))
            threads.stop = true;

        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (skill.enabled() && skill.time_to_pick(rootDepth))
            skill.pick_best(rootMoves, multiPV);

        // Use part of the gained time from a previous stable move for the current move
        for (auto&& th : threads)
        {
            totBestMoveChanges += th->worker->bestMoveChanges;
            th->worker->bestMoveChanges = 0;
        }

        // Do we have time for the next iteration? Can we stop searching now?
        if (limits.use_time_management() && !threads.stop && !mainThread->stopOnPonderhit)
        {
            int nodesEffort = rootMoves[0].effort * 100 / std::max(size_t(1), size_t(nodes));

            double fallingEval = (1067 + 223 * (mainThread->bestPreviousAverageScore - bestValue)
                                  + 97 * (mainThread->iterValue[iterIdx] - bestValue))
                               / 10000.0;
            fallingEval = std::clamp(fallingEval, 0.580, 1.667);

            // If the bestMove is stable over several iterations, reduce time accordingly
            timeReduction    = lastBestMoveDepth + 8 < completedDepth ? 1.495 : 0.687;
            double reduction = (1.48 + mainThread->previousTimeReduction) / (2.17 * timeReduction);
            double bestMoveInstability = 1 + 1.88 * totBestMoveChanges / threads.size();
            double recapture           = limits.capSq == rootMoves[0].pv[0].to_sq() ? 0.955 : 1.005;

            double totalTime =
              mainThread->tm.optimum() * fallingEval * reduction * bestMoveInstability * recapture;

            // Cap used time in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = std::min(500.0, totalTime);

            auto elapsedTime = elapsed();

            if (completedDepth >= 10 && nodesEffort >= 97 && elapsedTime > totalTime * 0.739
                && !mainThread->ponder)
                threads.stop = true;

            // Stop the search if we have exceeded the totalTime
            if (elapsedTime > totalTime)
            {
                // If we are allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainThread->ponder)
                    mainThread->stopOnPonderhit = true;
                else
                    threads.stop = true;
            }
            else
                threads.increaseDepth = mainThread->ponder || elapsedTime <= totalTime * 0.506;
        }

        mainThread->iterValue[iterIdx] = bestValue;
        iterIdx                        = (iterIdx + 1) & 3;
    }

    if (!mainThread)
        return;

    mainThread->previousTimeReduction = timeReduction;

    // If the skill level is enabled, swap the best PV line with the sub-optimal one
    if (skill.enabled())
        std::swap(rootMoves[0],
                  *std::find(rootMoves.begin(), rootMoves.end(),
                             skill.best ? skill.best : skill.pick_best(rootMoves, multiPV)));
}

// Reset histories, usually before a new game
void Search::Worker::clear() {
    mainHistory.fill(0);
    captureHistory.fill(-700);
    pawnHistory.fill(-1188);
    correctionHistory.fill(0);

    for (bool inCheck : {false, true})
        for (StatsType c : {NoCaptures, Captures})
            for (auto& to : continuationHistory[inCheck][c])
                for (auto& h : to)
                    h->fill(-658);

    for (size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = int((18.62 + std::log(size_t(options["Threads"])) / 2) * std::log(i));

    refreshTable.clear(networks[numaAccessToken]);
}


// Main search function for both PV and non-PV nodes
template<NodeType nodeType>
Value Search::Worker::search(
  Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    constexpr bool PvNode   = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;
    const bool     allNode  = !(PvNode || cutNode);

    // Dive into quiescence search when the depth reaches zero
    if (depth <= 0)
        return qsearch < PvNode ? PV : NonPV > (pos, ss, alpha, beta);

    // Limit the depth if extensions made it too large
    depth = std::min(depth, MAX_PLY - 1);

    // Check if we have an upcoming move that draws by repetition
    if (!rootNode && alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
    {
        alpha = value_draw(this->nodes);
        if (alpha >= beta)
            return alpha;
    }

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(0 < depth && depth < MAX_PLY);
    assert(!(PvNode && cutNode));

    Move      pv[MAX_PLY + 1];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    Key   posKey;
    Move  move, excludedMove, bestMove;
    Depth extension, newDepth;
    Value bestValue, value, eval, maxValue, probCutBeta;
    bool  givesCheck, improving, priorCapture, opponentWorsening;
    bool  capture, ttCapture;
    Piece movedPiece;

    ValueList<Move, 32> capturesSearched;
    ValueList<Move, 32> quietsSearched;

    // Step 1. Initialize node
    Worker* thisThread = this;
    ss->inCheck        = pos.checkers();
    priorCapture       = pos.captured_piece();
    Color us           = pos.side_to_move();
    ss->moveCount      = 0;
    bestValue          = -VALUE_INFINITE;
    maxValue           = VALUE_INFINITE;

    // Check for the available remaining time
    if (is_mainthread())
        main_manager()->check_time(*thisThread);

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && thisThread->selDepth < ss->ply + 1)
        thisThread->selDepth = ss->ply + 1;

    if (!rootNode)
    {
        // Step 2. Check for aborted search and immediate draw
        if (threads.stop.load(std::memory_order_relaxed) || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return (ss->ply >= MAX_PLY && !ss->inCheck)
                   ? evaluate(networks[numaAccessToken], pos, refreshTable,
                              thisThread->optimism[us])
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

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    bestMove            = Move::none();
    (ss + 2)->cutoffCnt = 0;
    Square prevSq = ((ss - 1)->currentMove).is_ok() ? ((ss - 1)->currentMove).to_sq() : SQ_NONE;
    ss->statScore = 0;

    // Step 4. Transposition table lookup
    excludedMove                   = ss->excludedMove;
    posKey                         = pos.key();
    auto [ttHit, ttData, ttWriter] = tt.probe(posKey);
    // Need further processing of the saved data
    ss->ttHit    = ttHit;
    ttData.move  = rootNode ? thisThread->rootMoves[thisThread->pvIdx].pv[0]
                 : ttHit    ? ttData.move
                            : Move::none();
    ttData.value = ttHit ? value_from_tt(ttData.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ss->ttPv     = excludedMove ? ss->ttPv : PvNode || (ttHit && ttData.is_pv);
    ttCapture    = ttData.move && pos.capture_stage(ttData.move);

    // At this point, if excluded, skip straight to step 6, static eval. However,
    // to save indentation, we list the condition in all code between here and there.

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && !excludedMove && ttData.depth > depth - (ttData.value <= beta)
        && ttData.value != VALUE_NONE  // Can happen when !ttHit or when access race in probe()
        && (ttData.bound & (ttData.value >= beta ? BOUND_LOWER : BOUND_UPPER)))
    {
        // If ttMove is quiet, update move sorting heuristics on TT hit (~2 Elo)
        if (ttData.move && ttData.value >= beta)
        {
            // Bonus for a quiet ttMove that fails high (~2 Elo)
            if (!ttCapture)
                update_quiet_histories(pos, ss, *this, ttData.move, stat_bonus(depth));

            // Extra penalty for early quiet moves of
            // the previous ply (~1 Elo on STC, ~2 Elo on LTC)
            if (prevSq != SQ_NONE && (ss - 1)->moveCount <= 2 && !priorCapture)
                update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                              -stat_malus(depth + 1));
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < 90)
            return ttData.value;
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
            TB::WDLScore   wdl = Tablebases::probe_wdl(pos, &err);

            // Force check of time on the next occasion
            if (is_mainthread())
                main_manager()->callsCnt = 0;

            if (err != TB::ProbeState::FAIL)
            {
                thisThread->tbHits.fetch_add(1, std::memory_order_relaxed);

                int drawScore = tbConfig.useRule50 ? 1 : 0;

                Value tbValue = VALUE_TB - ss->ply;

                // Use the range VALUE_TB to VALUE_TB_WIN_IN_MAX_PLY to score
                value = wdl < -drawScore ? -tbValue
                      : wdl > drawScore  ? tbValue
                                         : VALUE_DRAW + 2 * wdl * drawScore;

                Bound b = wdl < -drawScore ? BOUND_UPPER
                        : wdl > drawScore  ? BOUND_LOWER
                                           : BOUND_EXACT;

                if (b == BOUND_EXACT || (b == BOUND_LOWER ? value >= beta : value <= alpha))
                {
                    ttWriter.write(posKey, value_to_tt(value, ss->ply), ss->ttPv, b,
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

    // Step 6. Static evaluation of the position
    Value unadjustedStaticEval = VALUE_NONE;
    if (ss->inCheck)
    {
        // Skip early pruning when in check
        ss->staticEval = eval = (ss - 2)->staticEval;
        improving             = false;
        goto moves_loop;
    }
    else if (excludedMove)
    {
        // Providing the hint that this node's accumulator will be used often
        // brings significant Elo gain (~13 Elo).
        Eval::NNUE::hint_common_parent_position(pos, networks[numaAccessToken], refreshTable);
        unadjustedStaticEval = eval = ss->staticEval;
    }
    else if (ss->ttHit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = ttData.eval;
        if (unadjustedStaticEval == VALUE_NONE)
            unadjustedStaticEval =
              evaluate(networks[numaAccessToken], pos, refreshTable, thisThread->optimism[us]);
        else if (PvNode)
            Eval::NNUE::hint_common_parent_position(pos, networks[numaAccessToken], refreshTable);

        ss->staticEval = eval = to_corrected_static_eval(unadjustedStaticEval, *thisThread, pos);

        // ttValue can be used as a better position evaluation (~7 Elo)
        if (ttData.value != VALUE_NONE
            && (ttData.bound & (ttData.value > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttData.value;
    }
    else
    {
        unadjustedStaticEval =
          evaluate(networks[numaAccessToken], pos, refreshTable, thisThread->optimism[us]);
        ss->staticEval = eval = to_corrected_static_eval(unadjustedStaticEval, *thisThread, pos);

        // Static evaluation is saved as it was before adjustment by correction history
        ttWriter.write(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_UNSEARCHED, Move::none(),
                       unadjustedStaticEval, tt.generation());
    }

    // Use static evaluation difference to improve quiet move ordering (~9 Elo)
    if (((ss - 1)->currentMove).is_ok() && !(ss - 1)->inCheck && !priorCapture)
    {
        int bonus = std::clamp(-10 * int((ss - 1)->staticEval + ss->staticEval), -1664, 1471) + 752;
        thisThread->mainHistory[~us][((ss - 1)->currentMove).from_to()] << bonus;
        if (type_of(pos.piece_on(prevSq)) != PAWN && ((ss - 1)->currentMove).type_of() != PROMOTION)
            thisThread->pawnHistory[pawn_structure_index(pos)][pos.piece_on(prevSq)][prevSq]
              << bonus / 2;
    }

    // Set up the improving flag, which is true if current static evaluation is
    // bigger than the previous static evaluation at our turn (if we were in
    // check at our previous move we go back until we weren't in check) and is
    // false otherwise. The improving flag is used in various pruning heuristics.
    improving = ss->staticEval > (ss - 2)->staticEval;

    opponentWorsening = ss->staticEval + (ss - 1)->staticEval > 2;

    // Step 7. Razoring (~1 Elo)
    // If eval is really low, check with qsearch if we can exceed alpha. If the
    // search suggests we cannot exceed alpha, return a speculative fail low.
    if (eval < alpha - 494 - 290 * depth * depth)
    {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
            return value;
    }

    // Step 8. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 13
        && eval - futility_margin(depth, cutNode && !ss->ttHit, improving, opponentWorsening)
               - (ss - 1)->statScore / 260
             >= beta
        && eval >= beta && (!ttData.move || ttCapture) && beta > VALUE_TB_LOSS_IN_MAX_PLY
        && eval < VALUE_TB_WIN_IN_MAX_PLY)
        return beta + (eval - beta) / 3;

    // Step 9. Null move search with verification search (~35 Elo)
    if (cutNode && (ss - 1)->currentMove != Move::null() && (ss - 1)->statScore < 14389
        && eval >= beta && ss->staticEval >= beta - 21 * depth + 390 && !excludedMove
        && pos.non_pawn_material(us) && ss->ply >= thisThread->nmpMinPly
        && beta > VALUE_TB_LOSS_IN_MAX_PLY)
    {
        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth and eval
        Depth R = std::min(int(eval - beta) / 202, 6) + depth / 3 + 5;

        ss->currentMove         = Move::null();
        ss->continuationHistory = &thisThread->continuationHistory[0][0][NO_PIECE][0];

        pos.do_null_move(st, tt);

        Value nullValue = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, false);

        pos.undo_null_move();

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
    // For PV nodes without a ttMove, we decrease depth.
    if (PvNode && !ttData.move)
        depth -= 3;

    // Use qsearch if depth <= 0
    if (depth <= 0)
        return qsearch<PV>(pos, ss, alpha, beta);

    // For cutNodes, if depth is high enough, decrease depth by 2 if there is no ttMove,
    // or by 1 if there is a ttMove with an upper bound.
    if (cutNode && depth >= 7 && (!ttData.move || ttData.bound == BOUND_UPPER))
        depth -= 1 + !ttData.move;

    // Step 11. ProbCut (~10 Elo)
    // If we have a good enough capture (or queen promotion) and a reduced search
    // returns a value much above beta, we can (almost) safely prune the previous move.
    probCutBeta = beta + 184 - 53 * improving;
    if (!PvNode && depth > 3
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
        // If value from transposition table is lower than probCutBeta, don't attempt
        // probCut there and in further interactions with transposition table cutoff
        // depth is set to depth - 3 because probCut search has depth set to depth - 4
        // but we also do a move before it. So effective depth is equal to depth - 3.
        && !(ttData.depth >= depth - 3 && ttData.value != VALUE_NONE && ttData.value < probCutBeta))
    {
        assert(probCutBeta < VALUE_INFINITE && probCutBeta > beta);

        MovePicker mp(pos, ttData.move, probCutBeta - ss->staticEval, &thisThread->captureHistory);
        Piece      captured;

        while ((move = mp.next_move()) != Move::none())
        {
            assert(move.is_ok());

            if (move == excludedMove)
                continue;

            if (!pos.legal(move))
                continue;

            assert(pos.capture_stage(move));

            movedPiece = pos.moved_piece(move);
            captured   = pos.piece_on(move.to_sq());


            // Prefetch the TT entry for the resulting position
            prefetch(tt.first_entry(pos.key_after(move)));

            ss->currentMove = move;
            ss->continuationHistory =
              &this->continuationHistory[ss->inCheck][true][pos.moved_piece(move)][move.to_sq()];

            thisThread->nodes.fetch_add(1, std::memory_order_relaxed);
            pos.do_move(move, st);

            // Perform a preliminary qsearch to verify that the move holds
            value = -qsearch<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (value >= probCutBeta)
                value =
                  -search<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, depth - 4, !cutNode);

            pos.undo_move(move);

            if (value >= probCutBeta)
            {
                thisThread->captureHistory[movedPiece][move.to_sq()][type_of(captured)]
                  << stat_bonus(depth - 2);

                // Save ProbCut data into transposition table
                ttWriter.write(posKey, value_to_tt(value, ss->ply), ss->ttPv, BOUND_LOWER,
                               depth - 3, move, unadjustedStaticEval, tt.generation());
                return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probCutBeta - beta)
                                                                 : value;
            }
        }

        Eval::NNUE::hint_common_parent_position(pos, networks[numaAccessToken], refreshTable);
    }

moves_loop:  // When in check, search starts here

    // Step 12. A small Probcut idea (~4 Elo)
    probCutBeta = beta + 390;
    if ((ttData.bound & BOUND_LOWER) && ttData.depth >= depth - 4 && ttData.value >= probCutBeta
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
        && std::abs(ttData.value) < VALUE_TB_WIN_IN_MAX_PLY)
        return probCutBeta;

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory,
                                        (ss - 3)->continuationHistory,
                                        (ss - 4)->continuationHistory,
                                        nullptr,
                                        (ss - 6)->continuationHistory};


    MovePicker mp(pos, ttData.move, depth, &thisThread->mainHistory, &thisThread->captureHistory,
                  contHist, &thisThread->pawnHistory);

    value = bestValue;

    int  moveCount        = 0;
    bool moveCountPruning = false;

    // Step 13. Loop through all pseudo-legal moves until no moves remain
    // or a beta cutoff occurs.
    while ((move = mp.next_move(moveCountPruning)) != Move::none())
    {
        assert(move.is_ok());

        if (move == excludedMove)
            continue;

        // Check for legality
        if (!pos.legal(move))
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in Root
        // Move List. In MultiPV mode we also skip PV moves that have been already
        // searched and those of lower "TB rank" if we are in a TB root position.
        if (rootNode
            && !std::count(thisThread->rootMoves.begin() + thisThread->pvIdx,
                           thisThread->rootMoves.begin() + thisThread->pvLast, move))
            continue;

        ss->moveCount = ++moveCount;

        if (rootNode && is_mainthread() && nodes > 10000000)
        {
            main_manager()->updates.onIter(
              {depth, UCIEngine::move(move, pos.is_chess960()), moveCount + thisThread->pvIdx});
        }
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

        // Step 14. Pruning at shallow depth (~120 Elo).
        // Depth conditions are important for mate finding.
        if (!rootNode && pos.non_pawn_material(us) && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
        {
            // Skip quiet moves if movecount exceeds our FutilityMoveCount threshold (~8 Elo)
            moveCountPruning = moveCount >= futility_move_count(improving, depth);

            // Reduced depth of the next LMR search
            int lmrDepth = newDepth - r;

            if (capture || givesCheck)
            {
                Piece capturedPiece = pos.piece_on(move.to_sq());
                int   captHist =
                  thisThread->captureHistory[movedPiece][move.to_sq()][type_of(capturedPiece)];

                // Futility pruning for captures (~2 Elo)
                if (!givesCheck && lmrDepth < 7 && !ss->inCheck)
                {
                    Value futilityValue = ss->staticEval + 285 + 251 * lmrDepth
                                        + PieceValue[capturedPiece] + captHist / 7;
                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures and checks (~11 Elo)
                int seeHist = std::clamp(captHist / 32, -182 * depth, 166 * depth);
                if (!pos.see_ge(move, -168 * depth - seeHist))
                    continue;
            }
            else
            {
                int history =
                  (*contHist[0])[movedPiece][move.to_sq()]
                  + (*contHist[1])[movedPiece][move.to_sq()]
                  + thisThread->pawnHistory[pawn_structure_index(pos)][movedPiece][move.to_sq()];

                // Continuation history based pruning (~2 Elo)
                if (history < -4165 * depth)
                    continue;

                history += 2 * thisThread->mainHistory[us][move.from_to()];

                lmrDepth += history / 3853;

                Value futilityValue =
                  ss->staticEval + (bestValue < ss->staticEval - 51 ? 143 : 52) + 135 * lmrDepth;

                // Futility pruning: parent node (~13 Elo)
                if (!ss->inCheck && lmrDepth < 12 && futilityValue <= alpha)
                {
                    if (bestValue <= futilityValue && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY
                        && futilityValue < VALUE_TB_WIN_IN_MAX_PLY)
                        bestValue = futilityValue;
                    continue;
                }

                lmrDepth = std::max(lmrDepth, 0);

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, -24 * lmrDepth * lmrDepth))
                    continue;
            }
        }

        // Step 15. Extensions (~100 Elo)
        // We take care to not overdo to avoid search getting stuck.
        if (ss->ply < thisThread->rootDepth * 2)
        {
            // Singular extension search (~76 Elo, ~170 nElo). If all moves but one
            // fail low on a search of (alpha-s, beta-s), and just one fails high on
            // (alpha, beta), then that move is singular and should be extended. To
            // verify this we do a reduced search on the position excluding the ttMove
            // and if the result is lower than ttValue minus a margin, then we will
            //  extend the ttMove. Recursive singular search is avoided.

            // Note: the depth margin and singularBeta margin are known for having
            // non-linear scaling. Their values are optimized to time controls of
            // 180+1.8 and longer so changing them requires tests at these types of
            // time controls. Generally, higher singularBeta (i.e closer to ttValue)
            // and lower extension margins scale well.

            if (!rootNode && move == ttData.move && !excludedMove
                && depth >= 4 - (thisThread->completedDepth > 36) + ss->ttPv
                && std::abs(ttData.value) < VALUE_TB_WIN_IN_MAX_PLY && (ttData.bound & BOUND_LOWER)
                && ttData.depth >= depth - 3)
            {
                Value singularBeta  = ttData.value - (54 + 76 * (ss->ttPv && !PvNode)) * depth / 64;
                Depth singularDepth = newDepth / 2;

                ss->excludedMove = move;
                value =
                  search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
                ss->excludedMove = Move::none();

                if (value < singularBeta)
                {
                    int doubleMargin = 293 * PvNode - 195 * !ttCapture;
                    int tripleMargin = 107 + 259 * PvNode - 260 * !ttCapture + 98 * ss->ttPv;

                    extension = 1 + (value < singularBeta - doubleMargin)
                              + (value < singularBeta - tripleMargin);

                    depth += ((!PvNode) && (depth < 16));
                }

                // Multi-cut pruning
                // Our ttMove is assumed to fail high based on the bound of the TT entry,
                // and if after excluding the ttMove with a reduced search we fail high
                // over the original beta, we assume this expected cut-node is not
                // singular (multiple moves fail high), and we can prune the whole
                // subtree by returning a softbound.
                else if (value >= beta && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
                    return value;

                // Negative extensions
                // If other moves failed high over (ttValue - margin) without the
                // ttMove on a reduced search, but we cannot do multi-cut because
                // (ttValue - margin) is lower than the original beta, we do not know
                // if the ttMove is singular or can do a multi-cut, so we reduce the
                // ttMove in favor of other moves based on some conditions:

                // If the ttMove is assumed to fail high over current beta (~7 Elo)
                else if (ttData.value >= beta)
                    extension = -3;

                // If we are on a cutNode but the ttMove is not assumed to fail high
                // over current beta (~1 Elo)
                else if (cutNode)
                    extension = -2;
            }

            // Extension for capturing the previous moved piece (~1 Elo at LTC)
            else if (PvNode && move.to_sq() == prevSq
                     && thisThread->captureHistory[movedPiece][move.to_sq()]
                                                  [type_of(pos.piece_on(move.to_sq()))]
                          > 3994)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        // Update the current move (this must be done after singular extension search)
        ss->currentMove = move;
        ss->continuationHistory =
          &thisThread->continuationHistory[ss->inCheck][capture][movedPiece][move.to_sq()];

        uint64_t nodeCount = rootNode ? uint64_t(nodes) : 0;

        // Step 16. Make the move
        thisThread->nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);

        // These reduction adjustments have proven non-linear scaling.
        // They are optimized to time controls of 180 + 1.8 and longer,
        // so changing them or adding conditions that are similar requires
        // tests at these types of time controls.

        // Decrease reduction if position is or has been on the PV (~7 Elo)
        if (ss->ttPv)
            r -= 1 + (ttData.value > alpha) + (ttData.depth >= depth);

        // Decrease reduction for PvNodes (~0 Elo on STC, ~2 Elo on LTC)
        if (PvNode)
            r--;

        // These reduction adjustments have no proven non-linear scaling

        // Increase reduction for cut nodes (~4 Elo)
        if (cutNode)
            r += 2 - (ttData.depth >= depth && ss->ttPv);

        // Increase reduction if ttMove is a capture (~3 Elo)
        if (ttCapture)
            r++;

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if ((ss + 1)->cutoffCnt > 3)
            r += 1 + allNode;

        // For first picked move (ttMove) reduce reduction, but never allow
        // reduction to go below 0 (~3 Elo)
        else if (move == ttData.move)
            r = std::max(0, r - 2);

        ss->statScore = 2 * thisThread->mainHistory[us][move.from_to()]
                      + (*contHist[0])[movedPiece][move.to_sq()]
                      + (*contHist[1])[movedPiece][move.to_sq()] - 4664;

        // Decrease/increase reduction for moves with a good/bad history (~8 Elo)
        r -= ss->statScore / 10898;

        // Step 17. Late moves reduction / extension (LMR, ~117 Elo)
        if (depth >= 2 && moveCount > 1 + (rootNode && depth < 10))
        {
            // In general we want to cap the LMR depth search at newDepth, but when
            // reduction is negative, we allow this move a limited search extension
            // beyond the first move depth.
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth d = std::max(1, std::min(newDepth - r, newDepth + !allNode));

            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result was
                // good enough search deeper, if it was bad enough search shallower.
                const bool doDeeperSearch    = value > (bestValue + 35 + 2 * newDepth);  // (~1 Elo)
                const bool doShallowerSearch = value < bestValue + 8;                    // (~2 Elo)

                newDepth += doDeeperSearch - doShallowerSearch;

                if (newDepth > d)
                    value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                // Post LMR continuation history updates (~1 Elo)
                int bonus = value >= beta ? stat_bonus(newDepth) : -stat_malus(newDepth);

                update_continuation_histories(ss, movedPiece, move.to_sq(), bonus);
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PvNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present (~6 Elo)
            if (!ttData.move)
                r += 2;

            // Note that if expected reduction is high, we reduce search depth by 1 here (~9 Elo)
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth - (r > 3), !cutNode);
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if (PvNode && (moveCount == 1 || value > alpha))
        {
            (ss + 1)->pv    = pv;
            (ss + 1)->pv[0] = Move::none();

            // Extend move from transposition table if we are about to dive into qsearch.
            if (move == ttData.move && ss->ply <= thisThread->rootDepth * 2)
                newDepth = std::max(newDepth, 1);

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth, false);
        }

        // Step 19. Undo move
        pos.undo_move(move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and we return immediately without updating
        // best move, principal variation nor transposition table.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (rootNode)
        {
            RootMove& rm =
              *std::find(thisThread->rootMoves.begin(), thisThread->rootMoves.end(), move);

            rm.effort += nodes - nodeCount;

            rm.averageScore =
              rm.averageScore != -VALUE_INFINITE ? (value + rm.averageScore) / 2 : value;

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.score = rm.uciScore = value;
                rm.selDepth            = thisThread->selDepth;
                rm.scoreLowerbound = rm.scoreUpperbound = false;

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
                if (moveCount > 1 && !thisThread->pvIdx)
                    ++thisThread->bestMoveChanges;
            }
            else
                // All other moves but the PV, are set to the lowest value: this
                // is not a problem when sorting because the sort is stable and the
                // move position in the list is preserved - just the PV is pushed up.
                rm.score = -VALUE_INFINITE;
        }

        // In case we have an alternative move equal in eval to the current bestmove,
        // promote it to bestmove by pretending it just exceeds alpha (but not beta).
        int inc =
          (value == bestValue && (int(nodes) & 15) == 0 && ss->ply + 2 >= thisThread->rootDepth
           && std::abs(value) + 1 < VALUE_TB_WIN_IN_MAX_PLY);

        if (value + inc > bestValue)
        {
            bestValue = value;

            if (value + inc > alpha)
            {
                bestMove = move;

                if (PvNode && !rootNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                {
                    ss->cutoffCnt += !ttData.move + (extension < 2);
                    assert(value >= beta);  // Fail high
                    break;
                }
                else
                {
                    // Reduce other moves if we have found at least one score improvement (~2 Elo)
                    if (depth > 2 && depth < 14 && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
                        depth -= 2;

                    assert(depth > 0);
                    alpha = value;  // Update alpha! Always alpha < beta
                }
            }
        }

        // If the move is worse than some previously searched move,
        // remember it, to update its stats later.
        if (move != bestMove && moveCount <= 32)
        {
            if (capture)
                capturesSearched.push_back(move);
            else
                quietsSearched.push_back(move);
        }
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it
    // must be a mate or a stalemate. If we are in a singular extension search then
    // return a fail low score.

    assert(moveCount || !ss->inCheck || excludedMove || !MoveList<LEGAL>(pos).size());

    // Adjust best value for fail high cases at non-pv nodes
    if (!PvNode && bestValue >= beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY && std::abs(alpha) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (bestValue * depth + beta) / (depth + 1);

    if (!moveCount)
        bestValue = excludedMove ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha,
    // we update the stats of searched moves.
    else if (bestMove)
        update_all_stats(pos, ss, *this, bestMove, prevSq, quietsSearched, capturesSearched, depth);

    // Bonus for prior countermove that caused the fail low
    else if (!priorCapture && prevSq != SQ_NONE)
    {
        int bonus = (122 * (depth > 5) + 39 * !allNode + 165 * ((ss - 1)->moveCount > 8)
                     + 107 * (!ss->inCheck && bestValue <= ss->staticEval - 98)
                     + 134 * (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->staticEval - 91));

        // Proportional to "how much damage we have to undo"
        bonus += std::clamp(-(ss - 1)->statScore / 100, -94, 304);

        bonus = std::max(bonus, 0);

        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                      stat_bonus(depth) * bonus / 116);
        thisThread->mainHistory[~us][((ss - 1)->currentMove).from_to()]
          << stat_bonus(depth) * bonus / 180;


        if (type_of(pos.piece_on(prevSq)) != PAWN && ((ss - 1)->currentMove).type_of() != PROMOTION)
            thisThread->pawnHistory[pawn_structure_index(pos)][pos.piece_on(prevSq)][prevSq]
              << stat_bonus(depth) * bonus / 25;
    }

    // Bonus when search fails low and there is a TT move
    else if (moveCount > 1 && ttData.move && !allNode)
        thisThread->mainHistory[us][ttData.move.from_to()] << stat_bonus(depth) / 4;

    if (PvNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~7 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write gathered information in transposition table. Note that the
    // static evaluation is saved as it was before correction history.
    if (!excludedMove && !(rootNode && thisThread->pvIdx))
        ttWriter.write(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                       bestValue >= beta    ? BOUND_LOWER
                       : PvNode && bestMove ? BOUND_EXACT
                                            : BOUND_UPPER,
                       depth, bestMove, unadjustedStaticEval, tt.generation());

    // Adjust correction history
    if (!ss->inCheck && (!bestMove || !pos.capture(bestMove))
        && !(bestValue >= beta && bestValue <= ss->staticEval)
        && !(!bestMove && bestValue >= ss->staticEval))
    {
        auto bonus = std::clamp(int(bestValue - ss->staticEval) * depth / 8,
                                -CORRECTION_HISTORY_LIMIT / 4, CORRECTION_HISTORY_LIMIT / 4);
        thisThread->correctionHistory[us][pawn_structure_index<Correction>(pos)] << bonus;
    }

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}


// Quiescence search function, which is called by the main search function with
// depth zero, or recursively with further decreasing depth. With depth <= 0, we
// "should" be using static eval only, but tactical moves may confuse the static eval.
// To fight this horizon effect, we implement this qsearch of tactical moves (~155 Elo).
// See https://www.chessprogramming.org/Horizon_Effect
// and https://www.chessprogramming.org/Quiescence_Search
template<NodeType nodeType>
Value Search::Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));

    // Check if we have an upcoming move that draws by repetition (~1 Elo)
    if (alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
    {
        alpha = value_draw(this->nodes);
        if (alpha >= beta)
            return alpha;
    }

    Move      pv[MAX_PLY + 1];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    Key   posKey;
    Move  move, bestMove;
    Value bestValue, value, futilityBase;
    bool  pvHit, givesCheck, capture;
    int   moveCount;
    Color us = pos.side_to_move();

    // Step 1. Initialize node
    if (PvNode)
    {
        (ss + 1)->pv = pv;
        ss->pv[0]    = Move::none();
    }

    Worker* thisThread = this;
    bestMove           = Move::none();
    ss->inCheck        = pos.checkers();
    moveCount          = 0;

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PvNode && thisThread->selDepth < ss->ply + 1)
        thisThread->selDepth = ss->ply + 1;

    // Step 2. Check for an immediate draw or maximum ply reached
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY)
        return (ss->ply >= MAX_PLY && !ss->inCheck)
               ? evaluate(networks[numaAccessToken], pos, refreshTable, thisThread->optimism[us])
               : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Step 3. Transposition table lookup
    posKey                         = pos.key();
    auto [ttHit, ttData, ttWriter] = tt.probe(posKey);
    // Need further processing of the saved data
    ss->ttHit    = ttHit;
    ttData.move  = ttHit ? ttData.move : Move::none();
    ttData.value = ttHit ? value_from_tt(ttData.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    pvHit        = ttHit && ttData.is_pv;

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && ttData.depth >= DEPTH_QS
        && ttData.value != VALUE_NONE  // Can happen when !ttHit or when access race in probe()
        && (ttData.bound & (ttData.value >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttData.value;

    // Step 4. Static evaluation of the position
    Value unadjustedStaticEval = VALUE_NONE;
    if (ss->inCheck)
        bestValue = futilityBase = -VALUE_INFINITE;
    else
    {
        if (ss->ttHit)
        {
            // Never assume anything about values stored in TT
            unadjustedStaticEval = ttData.eval;
            if (unadjustedStaticEval == VALUE_NONE)
                unadjustedStaticEval =
                  evaluate(networks[numaAccessToken], pos, refreshTable, thisThread->optimism[us]);
            ss->staticEval = bestValue =
              to_corrected_static_eval(unadjustedStaticEval, *thisThread, pos);

            // ttValue can be used as a better position evaluation (~13 Elo)
            if (std::abs(ttData.value) < VALUE_TB_WIN_IN_MAX_PLY
                && (ttData.bound & (ttData.value > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttData.value;
        }
        else
        {
            // In case of null move search, use previous static eval with opposite sign
            unadjustedStaticEval =
              (ss - 1)->currentMove != Move::null()
                ? evaluate(networks[numaAccessToken], pos, refreshTable, thisThread->optimism[us])
                : -(ss - 1)->staticEval;
            ss->staticEval = bestValue =
              to_corrected_static_eval(unadjustedStaticEval, *thisThread, pos);
        }

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
                bestValue = (3 * bestValue + beta) / 4;
            if (!ss->ttHit)
                ttWriter.write(posKey, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER,
                               DEPTH_UNSEARCHED, Move::none(), unadjustedStaticEval,
                               tt.generation());
            return bestValue;
        }

        if (bestValue > alpha)
            alpha = bestValue;

        futilityBase = ss->staticEval + 299;
    }

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory};

    Square prevSq = ((ss - 1)->currentMove).is_ok() ? ((ss - 1)->currentMove).to_sq() : SQ_NONE;

    // Initialize a MovePicker object for the current position, and prepare to search
    // the moves. We presently use two stages of move generator in quiescence search:
    // captures, or evasions only when in check.
    MovePicker mp(pos, ttData.move, DEPTH_QS, &thisThread->mainHistory, &thisThread->captureHistory,
                  contHist, &thisThread->pawnHistory);

    // Step 5. Loop through all pseudo-legal moves until no moves remain or a beta
    // cutoff occurs.
    while ((move = mp.next_move()) != Move::none())
    {
        assert(move.is_ok());

        if (!pos.legal(move))
            continue;

        givesCheck = pos.gives_check(move);
        capture    = pos.capture_stage(move);

        moveCount++;

        // Step 6. Pruning
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(us))
        {
            // Futility pruning and moveCount pruning (~10 Elo)
            if (!givesCheck && move.to_sq() != prevSq && futilityBase > VALUE_TB_LOSS_IN_MAX_PLY
                && move.type_of() != PROMOTION)
            {
                if (moveCount > 2)
                    continue;

                Value futilityValue = futilityBase + PieceValue[pos.piece_on(move.to_sq())];

                // If static eval + value of piece we are going to capture is
                // much lower than alpha, we can prune this move. (~2 Elo)
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }

                // If static eval is much lower than alpha and move is
                // not winning material, we can prune this move. (~2 Elo)
                if (futilityBase <= alpha && !pos.see_ge(move, 1))
                {
                    bestValue = std::max(bestValue, futilityBase);
                    continue;
                }

                // If static exchange evaluation is much worse than what
                // is needed to not fall below alpha, we can prune this move.
                if (futilityBase > alpha && !pos.see_ge(move, (alpha - futilityBase) * 4))
                {
                    bestValue = alpha;
                    continue;
                }
            }

            // Continuation history based pruning (~3 Elo)
            if (!capture
                && (*contHist[0])[pos.moved_piece(move)][move.to_sq()]
                       + (*contHist[1])[pos.moved_piece(move)][move.to_sq()]
                       + thisThread->pawnHistory[pawn_structure_index(pos)][pos.moved_piece(move)]
                                                [move.to_sq()]
                     <= 4643)
                continue;

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (!pos.see_ge(move, -83))
                continue;
        }

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        // Update the current move
        ss->currentMove = move;
        ss->continuationHistory =
          &thisThread
             ->continuationHistory[ss->inCheck][capture][pos.moved_piece(move)][move.to_sq()];

        // Step 7. Make and search the move
        thisThread->nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);
        value = -qsearch<nodeType>(pos, ss + 1, -beta, -alpha);
        pos.undo_move(move);

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 8. Check for a new best move
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

    // Step 9. Check for mate
    // All legal moves have been searched. A special case: if we are
    // in check and no legal moves were found, it is checkmate.
    if (ss->inCheck && bestValue == -VALUE_INFINITE)
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
                   unadjustedStaticEval, tt.generation());

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}

Depth Search::Worker::reduction(bool i, Depth d, int mn, int delta) const {
    int reductionScale = reductions[d] * reductions[mn];
    return (reductionScale + 1274 - delta * 746 / rootDelta) / 1024 + (!i && reductionScale > 1293);
}

// elapsed() returns the time elapsed since the search started. If the
// 'nodestime' option is enabled, it will return the count of nodes searched
// instead. This function is called to check whether the search should be
// stopped based on predefined thresholds like time limits or nodes searched.
//
// elapsed_time() returns the actual time elapsed since the start of the search.
// This function is intended for use only when printing PV outputs, and not used
// for making decisions within the search algorithm itself.
TimePoint Search::Worker::elapsed() const {
    return main_manager()->tm.elapsed([this]() { return threads.nodes_searched(); });
}

TimePoint Search::Worker::elapsed_time() const { return main_manager()->tm.elapsed_time(); }


namespace {
// Adjusts a mate or TB score from "plies to mate from the root" to
// "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);
    return v >= VALUE_TB_WIN_IN_MAX_PLY ? v + ply : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply : v;
}


// Inverse of value_to_tt(): it adjusts a mate or TB score from the transposition
// table (which refers to the plies to mate/be mated from current position) to
// "plies to mate/be mated (TB win/loss) from the root". However, to avoid
// potentially false mate or TB scores related to the 50 moves rule and the
// graph history interaction, we return the highest non-TB score instead.
Value value_from_tt(Value v, int ply, int r50c) {

    if (v == VALUE_NONE)
        return VALUE_NONE;

    // handle TB win or better
    if (v >= VALUE_TB_WIN_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate score
        if (v >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - v > 100 - r50c)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        // Downgrade a potentially false TB score.
        if (VALUE_TB - v > 100 - r50c)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        return v - ply;
    }

    // handle TB loss or worse
    if (v <= VALUE_TB_LOSS_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate score.
        if (v <= VALUE_MATED_IN_MAX_PLY && VALUE_MATE + v > 100 - r50c)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        // Downgrade a potentially false TB score.
        if (VALUE_TB + v > 100 - r50c)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        return v + ply;
    }

    return v;
}


// Adds current move and appends child pv[]
void update_pv(Move* pv, Move move, const Move* childPv) {

    for (*pv++ = move; childPv && *childPv != Move::none();)
        *pv++ = *childPv++;
    *pv = Move::none();
}


// Updates stats at the end of search() when a bestMove is found
void update_all_stats(const Position&      pos,
                      Stack*               ss,
                      Search::Worker&      workerThread,
                      Move                 bestMove,
                      Square               prevSq,
                      ValueList<Move, 32>& quietsSearched,
                      ValueList<Move, 32>& capturesSearched,
                      Depth                depth) {

    CapturePieceToHistory& captureHistory = workerThread.captureHistory;
    Piece                  moved_piece    = pos.moved_piece(bestMove);
    PieceType              captured;

    int quietMoveBonus = stat_bonus(depth);
    int quietMoveMalus = stat_malus(depth);

    if (!pos.capture_stage(bestMove))
    {
        update_quiet_histories(pos, ss, workerThread, bestMove, quietMoveBonus);

        // Decrease stats for all non-best quiet moves
        for (Move move : quietsSearched)
            update_quiet_histories(pos, ss, workerThread, move, -quietMoveMalus);
    }
    else
    {
        // Increase stats for the best move in case it was a capture move
        captured = type_of(pos.piece_on(bestMove.to_sq()));
        captureHistory[moved_piece][bestMove.to_sq()][captured] << quietMoveBonus;
    }

    // Extra penalty for a quiet early move that was not a TT move in
    // previous ply when it gets refuted.
    if (prevSq != SQ_NONE && ((ss - 1)->moveCount == 1 + (ss - 1)->ttHit) && !pos.captured_piece())
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq, -quietMoveMalus);

    // Decrease stats for all non-best capture moves
    for (Move move : capturesSearched)
    {
        moved_piece = pos.moved_piece(move);
        captured    = type_of(pos.piece_on(move.to_sq()));
        captureHistory[moved_piece][move.to_sq()][captured] << -quietMoveMalus;
    }
}


// Updates histories of the move pairs formed by moves
// at ply -1, -2, -3, -4, and -6 with current move.
void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {

    bonus = bonus * 52 / 64;

    for (int i : {1, 2, 3, 4, 6})
    {
        // Only update the first 2 continuation histories if we are in check
        if (ss->inCheck && i > 2)
            break;
        if (((ss - i)->currentMove).is_ok())
            (*(ss - i)->continuationHistory)[pc][to] << bonus / (1 + (i == 3));
    }
}

// Updates move sorting heuristics

void update_quiet_histories(
  const Position& pos, Stack* ss, Search::Worker& workerThread, Move move, int bonus) {

    Color us = pos.side_to_move();
    workerThread.mainHistory[us][move.from_to()] << bonus;

    update_continuation_histories(ss, pos.moved_piece(move), move.to_sq(), bonus);

    int pIndex = pawn_structure_index(pos);
    workerThread.pawnHistory[pIndex][pos.moved_piece(move)][move.to_sq()] << bonus / 2;
}

}

// When playing with strength handicap, choose the best move among a set of
// RootMoves using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move Skill::pick_best(const RootMoves& rootMoves, size_t multiPV) {
    static PRNG rng(now());  // PRNG sequence should be non-deterministic

    // RootMoves are already sorted by score in descending order
    Value  topScore = rootMoves[0].score;
    int    delta    = std::min(topScore - rootMoves[multiPV - 1].score, int(PawnValue));
    int    maxScore = -VALUE_INFINITE;
    double weakness = 120 - 2 * level;

    // Choose best move. For each move score we add two terms, both dependent on
    // weakness. One is deterministic and bigger for weaker levels, and one is
    // random. Then we choose the move with the resulting highest score.
    for (size_t i = 0; i < multiPV; ++i)
    {
        // This is our magic formula
        int push = (weakness * int(topScore - rootMoves[i].score)
                    + delta * (rng.rand<unsigned>() % int(weakness)))
                 / 128;

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

    TimePoint elapsed = tm.elapsed([&worker]() { return worker.threads.nodes_searched(); });
    TimePoint tick    = worker.limits.startTime + elapsed;

    if (tick - lastInfoTime >= 1000)
    {
        lastInfoTime = tick;
        dbg_print();
    }

    // We should not stop pondering until told so by the GUI
    if (ponder)
        return;

    if (
      // Later we rely on the fact that we can at least use the mainthread previous
      // root-search score and PV in a multithreaded environment to prove mated-in scores.
      worker.completedDepth >= 1
      && ((worker.limits.use_time_management() && (elapsed > tm.maximum() || stopOnPonderhit))
          || (worker.limits.movetime && elapsed >= worker.limits.movetime)
          || (worker.limits.nodes && worker.threads.nodes_searched() >= worker.limits.nodes)))
        worker.threads.stop = worker.threads.abortedSearch = true;
}

// Used to correct and extend PVs for moves that have a TB (but not a mate) score.
// Keeps the search based PV for as long as it is verified to maintain the game
// outcome, truncates afterwards. Finally, extends to mate the PV, providing a
// possible continuation (but not a proven mating line).
void syzygy_extend_pv(const OptionsMap&         options,
                      const Search::LimitsType& limits,
                      Position&                 pos,
                      RootMove&                 rootMove,
                      Value&                    v) {

    auto t_start      = std::chrono::steady_clock::now();
    int  moveOverhead = int(options["Move Overhead"]);

    // Do not use more than moveOverhead / 2 time, if time management is active
    auto time_abort = [&t_start, &moveOverhead, &limits]() -> bool {
        auto t_end = std::chrono::steady_clock::now();
        return limits.use_time_management()
            && 2 * std::chrono::duration<double, std::milli>(t_end - t_start).count()
                 > moveOverhead;
    };

    std::list<StateInfo> sts;

    // Step 0, do the rootMove, no correction allowed, as needed for MultiPV in TB.
    auto& stRoot = sts.emplace_back();
    pos.do_move(rootMove.pv[0], stRoot);
    int ply = 1;

    // Step 1, walk the PV to the last position in TB with correct decisive score
    while (size_t(ply) < rootMove.pv.size())
    {
        Move& pvMove = rootMove.pv[ply];

        RootMoves legalMoves;
        for (const auto& m : MoveList<LEGAL>(pos))
            legalMoves.emplace_back(m);

        Tablebases::Config config = Tablebases::rank_root_moves(options, pos, legalMoves);
        RootMove&          rm     = *std::find(legalMoves.begin(), legalMoves.end(), pvMove);

        if (legalMoves[0].tbRank != rm.tbRank)
            break;

        ply++;

        auto& st = sts.emplace_back();
        pos.do_move(pvMove, st);

        // Do not allow for repetitions or drawing moves along the PV in TB regime
        if (config.rootInTB && pos.is_draw(ply))
        {
            pos.undo_move(pvMove);
            ply--;
            break;
        }

        // Full PV shown will thus be validated and end in TB.
        // If we cannot validate the full PV in time, we do not show it.
        if (config.rootInTB && time_abort())
            break;
    }

    // Resize the PV to the correct part
    rootMove.pv.resize(ply);

    // Step 2, now extend the PV to mate, as if the user explored syzygy-tables.info
    // using top ranked moves (minimal DTZ), which gives optimal mates only for simple
    // endgames e.g. KRvK.
    while (!pos.is_draw(0))
    {
        if (time_abort())
            break;

        RootMoves legalMoves;
        for (const auto& m : MoveList<LEGAL>(pos))
        {
            auto&     rm = legalMoves.emplace_back(m);
            StateInfo tmpSI;
            pos.do_move(m, tmpSI);
            // Give a score of each move to break DTZ ties restricting opponent mobility,
            // but not giving the opponent a capture.
            for (const auto& mOpp : MoveList<LEGAL>(pos))
                rm.tbRank -= pos.capture(mOpp) ? 100 : 1;
            pos.undo_move(m);
        }

        // Mate found
        if (legalMoves.size() == 0)
            break;

        // Sort moves according to their above assigned rank.
        // This will break ties for moves with equal DTZ in rank_root_moves.
        std::stable_sort(
          legalMoves.begin(), legalMoves.end(),
          [](const Search::RootMove& a, const Search::RootMove& b) { return a.tbRank > b.tbRank; });

        // The winning side tries to minimize DTZ, the losing side maximizes it
        Tablebases::Config config = Tablebases::rank_root_moves(options, pos, legalMoves, true);

        // If DTZ is not available we might not find a mate, so we bail out
        if (!config.rootInTB || config.cardinality > 0)
            break;

        ply++;

        Move& pvMove = legalMoves[0].pv[0];
        rootMove.pv.push_back(pvMove);
        auto& st = sts.emplace_back();
        pos.do_move(pvMove, st);
    }

    // Finding a draw in this function is an exceptional case, that cannot happen
    // during engine game play, since we have a winning score, and play correctly
    // with TB support. However, it can be that a position is draw due to the 50 move
    // rule if it has been been reached on the board with a non-optimal 50 move counter
    // (e.g. 8/8/6k1/3B4/3K4/4N3/8/8 w - - 54 106 ) which TB with dtz counter rounding
    // cannot always correctly rank. See also
    // https://github.com/official-stockfish/Stockfish/issues/5175#issuecomment-2058893495
    // We adjust the score to match the found PV. Note that a TB loss score can be
    // displayed if the engine did not find a drawing move yet, but eventually search
    // will figure it out (e.g. 1kq5/q2r4/5K2/8/8/8/8/7Q w - - 96 1 )
    if (pos.is_draw(0))
        v = VALUE_DRAW;

    // Undo the PV moves
    for (auto it = rootMove.pv.rbegin(); it != rootMove.pv.rend(); ++it)
        pos.undo_move(*it);

    // Inform if we couldn't get a full extension in time
    if (time_abort())
        sync_cout
          << "info string Syzygy based PV extension requires more time, increase Move Overhead as needed."
          << sync_endl;
}

void SearchManager::pv(Search::Worker&           worker,
                       const ThreadPool&         threads,
                       const TranspositionTable& tt,
                       Depth                     depth) {

    const auto nodes     = threads.nodes_searched();
    auto&      rootMoves = worker.rootMoves;
    auto&      pos       = worker.rootPos;
    size_t     pvIdx     = worker.pvIdx;
    size_t     multiPV   = std::min(size_t(worker.options["MultiPV"]), rootMoves.size());
    uint64_t   tbHits    = threads.tb_hits() + (worker.tbConfig.rootInTB ? rootMoves.size() : 0);

    for (size_t i = 0; i < multiPV; ++i)
    {
        bool updated = rootMoves[i].score != -VALUE_INFINITE;

        if (depth == 1 && !updated && i > 0)
            continue;

        Depth d = updated ? depth : std::max(1, depth - 1);
        Value v = updated ? rootMoves[i].uciScore : rootMoves[i].previousScore;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = worker.tbConfig.rootInTB && std::abs(v) <= VALUE_TB;
        v       = tb ? rootMoves[i].tbScore : v;

        bool isExact = i != pvIdx || tb || !updated;  // tablebase- and previous-scores are exact

        // Potentially correct and extend the PV, and in exceptional cases v
        if (std::abs(v) >= VALUE_TB_WIN_IN_MAX_PLY && std::abs(v) < VALUE_MATE_IN_MAX_PLY
            && ((!rootMoves[i].scoreLowerbound && !rootMoves[i].scoreUpperbound) || isExact))
            syzygy_extend_pv(worker.options, worker.limits, pos, rootMoves[i], v);

        std::string pv;
        for (Move m : rootMoves[i].pv)
            pv += UCIEngine::move(m, pos.is_chess960()) + " ";

        // Remove last whitespace
        if (!pv.empty())
            pv.pop_back();

        auto wdl   = worker.options["UCI_ShowWDL"] ? UCIEngine::wdl(v, pos) : "";
        auto bound = rootMoves[i].scoreLowerbound
                     ? "lowerbound"
                     : (rootMoves[i].scoreUpperbound ? "upperbound" : "");

        InfoFull info;

        info.depth    = d;
        info.selDepth = rootMoves[i].selDepth;
        info.multiPV  = i + 1;
        info.score    = {v, pos};
        info.wdl      = wdl;

        if (!isExact)
            info.bound = bound;

        TimePoint time = tm.elapsed_time() + 1;
        info.timeMs    = time;
        info.nodes     = nodes;
        info.nps       = nodes * 1000 / time;
        info.tbHits    = tbHits;
        info.pv        = pv;
        info.hashfull  = tt.hashfull();

        updates.onUpdateFull(info);
    }
}

// Called in case we have no ponder move before exiting the search,
// for instance, in case we stop the search during a fail high at root.
// We try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' we have nothing to think about.
bool RootMove::extract_ponder_from_tt(const TranspositionTable& tt, Position& pos) {

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    assert(pv.size() == 1);
    if (pv[0] == Move::none())
        return false;

    pos.do_move(pv[0], st);

    auto [ttHit, ttData, ttWriter] = tt.probe(pos.key());
    if (ttHit)
    {
        if (MoveList<LEGAL>(pos).contains(ttData.move))
            pv.push_back(ttData.move);
    }

    pos.undo_move(pv[0]);
    return pv.size() > 1;
}


}  // namespace Stockfish
