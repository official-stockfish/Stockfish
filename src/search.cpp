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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <utility>
#include <sstream>

#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "movepick.h"
#include "nnue/evaluate_nnue.h"
#include "nnue/nnue_common.h"
#include "position.h"
#include "syzygy/tbprobe.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include "uci.h"
#include "ucioption.h"

namespace Stockfish {

namespace TB = Tablebases;

using Eval::evaluate;
using namespace Search;

namespace {


// Futility margin
Value futility_margin(Depth d, bool noTtCutNode, bool improving) {
    Value futilityMult = 117 - 44 * noTtCutNode;
    return (futilityMult * d - 3 * futilityMult / 2 * improving);
}

constexpr int futility_move_count(bool improving, Depth depth) {
    return improving ? (3 + depth * depth) : (3 + depth * depth) / 2;
}

// Add correctionHistory value to raw staticEval and guarantee evaluation does not hit the tablebase range
Value to_corrected_static_eval(Value v, const Worker& w, const Position& pos) {
    auto cv = w.correctionHistory[pos.side_to_move()][pawn_structure_index<Correction>(pos)];
    v += cv * std::abs(cv) / 12475;
    return std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}

// History and stats update bonus, based on depth
int stat_bonus(Depth d) { return std::min(246 * d - 351, 1136); }

// History and stats update malus, based on depth
int stat_malus(Depth d) { return std::min(519 * d - 306, 1258); }

// Add a small random component to draw evaluations to avoid 3-fold blindness
Value value_draw(size_t nodes) { return VALUE_DRAW - 1 + Value(nodes & 0x2); }

// Skill structure is used to implement strength limit. If we have a UCI_Elo,
// we convert it to an appropriate skill level, anchored to the Stash engine.
// This method is based on a fit of the Elo results for games played between
// Stockfish at various skill levels and various versions of the Stash engine.
// Skill 0 .. 19 now covers CCRL Blitz Elo from 1320 to 3190, approximately
// Reference: https://github.com/vondele/Stockfish/commit/a08b8d4e9711c2
struct Skill {
    Skill(int skill_level, int uci_elo) {
        if (uci_elo)
        {
            double e = double(uci_elo - 1320) / (3190 - 1320);
            level = std::clamp((((37.2473 * e - 40.8525) * e + 22.2943) * e - 0.311438), 0.0, 19.0);
        }
        else
            level = double(skill_level);
    }
    bool enabled() const { return level < 20.0; }
    bool time_to_pick(Depth depth) const { return depth == 1 + int(level); }
    Move pick_best(const RootMoves&, size_t multiPV);

    double level;
    Move   best = Move::none();
};

Value value_to_tt(Value v, int ply);
Value value_from_tt(Value v, int ply, int r50c);
void  update_pv(Move* pv, Move move, const Move* childPv);
void  update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus);
void  update_quiet_stats(
   const Position& pos, Stack* ss, Search::Worker& workerThread, Move move, int bonus);
void update_all_stats(const Position& pos,
                      Stack*          ss,
                      Search::Worker& workerThread,
                      Move            bestMove,
                      Value           bestValue,
                      Value           beta,
                      Square          prevSq,
                      Move*           quietsSearched,
                      int             quietCount,
                      Move*           capturesSearched,
                      int             captureCount,
                      Depth           depth);

}  // namespace

Search::Worker::Worker(SharedState&                    sharedState,
                       std::unique_ptr<ISearchManager> sm,
                       size_t                          thread_id) :
    // Unpack the SharedState struct into member variables
    thread_idx(thread_id),
    manager(std::move(sm)),
    options(sharedState.options),
    threads(sharedState.threads),
    tt(sharedState.tt) {
    clear();
}

void Search::Worker::start_searching() {
    // Non-main threads go directly to iterative_deepening()
    if (!is_mainthread())
    {
        iterative_deepening();
        return;
    }

    main_manager()->tm.init(limits, rootPos.side_to_move(), rootPos.game_ply(), options);
    tt.new_search();

    if (rootMoves.empty())
    {
        rootMoves.emplace_back(Move::none());
        sync_cout << "info depth 0 score "
                  << UCI::value(rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW) << sync_endl;
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
    // "ponderhit" just reset threads.ponder).
    threads.stop = true;

    // Wait until all threads have finished
    threads.wait_for_search_finished();

    // When playing in 'nodes as time' mode, subtract the searched nodes from
    // the available ones before exiting.
    if (limits.npmsec)
        main_manager()->tm.advance_nodes_time(limits.inc[rootPos.side_to_move()]
                                              - threads.nodes_searched());

    Worker* bestThread = this;
    Skill   skill =
      Skill(options["Skill Level"], options["UCI_LimitStrength"] ? int(options["UCI_Elo"]) : 0);

    if (int(options["MultiPV"]) == 1 && !limits.depth && !skill.enabled()
        && rootMoves[0].pv[0] != Move::none())
        bestThread = threads.get_best_thread()->worker.get();

    main_manager()->bestPreviousScore        = bestThread->rootMoves[0].score;
    main_manager()->bestPreviousAverageScore = bestThread->rootMoves[0].averageScore;

    // Send again PV info if we have a new best thread
    if (bestThread != this)
        sync_cout << main_manager()->pv(*bestThread, threads, tt, bestThread->completedDepth)
                  << sync_endl;

    sync_cout << "bestmove " << UCI::move(bestThread->rootMoves[0].pv[0], rootPos.is_chess960());

    if (bestThread->rootMoves[0].pv.size() > 1
        || bestThread->rootMoves[0].extract_ponder_from_tt(tt, rootPos))
        std::cout << " ponder " << UCI::move(bestThread->rootMoves[0].pv[1], rootPos.is_chess960());

    std::cout << sync_endl;
}

// Main iterative deepening loop. It calls search()
// repeatedly with increasing depth until the allocated thinking time has been
// consumed, the user stops the search, or the maximum search depth is reached.
void Search::Worker::iterative_deepening() {

    SearchManager* mainThread = (thread_idx == 0 ? main_manager() : nullptr);

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
    // (ss + 2) is needed for initialization of cutOffCnt and killers.
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
        for (pvIdx = 0; pvIdx < multiPV && !threads.stop; ++pvIdx)
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
            delta     = 9 + avg * avg / 12487;
            alpha     = std::max(avg - delta, -VALUE_INFINITE);
            beta      = std::min(avg + delta, VALUE_INFINITE);

            // Adjust optimism based on root move's averageScore (~4 Elo)
            optimism[us]  = 134 * avg / (std::abs(avg) + 97);
            optimism[~us] = -optimism[us];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, re-search with a bigger window until we don't fail
            // high/low anymore.
            int failedHighCnt = 0;
            while (true)
            {
                // Adjust the effective depth searched, but ensure at least one effective increment
                // for every four searchAgain steps (see issue #2717).
                Depth adjustedDepth =
                  std::max(1, rootDepth - failedHighCnt - 3 * (searchAgainCounter + 1) / 4);
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

                // When failing high/low give some update (without cluttering
                // the UI) before a re-search.
                if (mainThread && multiPV == 1 && (bestValue <= alpha || bestValue >= beta)
                    && mainThread->tm.elapsed(threads.nodes_searched()) > 3000)
                    sync_cout << main_manager()->pv(*this, threads, tt, rootDepth) << sync_endl;

                // In case of failing low/high increase aspiration window and
                // re-search, otherwise exit the loop.
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
                && (threads.stop || pvIdx + 1 == multiPV
                    || mainThread->tm.elapsed(threads.nodes_searched()) > 3000)
                // A thread that aborted search can have mated-in/TB-loss PV and score
                // that cannot be trusted, i.e. it can be delayed or refuted if we would have
                // had time to fully search other root-moves. Thus we suppress this output and
                // below pick a proven score/PV for this thread (from the previous iteration).
                && !(threads.abortedSearch && rootMoves[0].uciScore <= VALUE_TB_LOSS_IN_MAX_PLY))
                sync_cout << main_manager()->pv(*this, threads, tt, rootDepth) << sync_endl;
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

        // Have we found a "mate in x"?
        if (limits.mate && bestValue >= VALUE_MATE_IN_MAX_PLY
            && VALUE_MATE - bestValue <= 2 * limits.mate)
            threads.stop = true;

        if (!mainThread)
            continue;

        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (skill.enabled() && skill.time_to_pick(rootDepth))
            skill.pick_best(rootMoves, multiPV);

        // Use part of the gained time from a previous stable move for the current move
        for (Thread* th : threads)
        {
            totBestMoveChanges += th->worker->bestMoveChanges;
            th->worker->bestMoveChanges = 0;
        }

        // Do we have time for the next iteration? Can we stop searching now?
        if (limits.use_time_management() && !threads.stop && !mainThread->stopOnPonderhit)
        {
            auto bestmove    = rootMoves[0].pv[0];
            int  nodesEffort = effort[bestmove.from_sq()][bestmove.to_sq()] * 100
                            / std::max(size_t(1), size_t(nodes));

            double fallingEval = (66 + 14 * (mainThread->bestPreviousAverageScore - bestValue)
                                  + 6 * (mainThread->iterValue[iterIdx] - bestValue))
                               / 616.6;
            fallingEval = std::clamp(fallingEval, 0.51, 1.51);

            // If the bestMove is stable over several iterations, reduce time accordingly
            timeReduction    = lastBestMoveDepth + 8 < completedDepth ? 1.56 : 0.69;
            double reduction = (1.4 + mainThread->previousTimeReduction) / (2.17 * timeReduction);
            double bestMoveInstability = 1 + 1.79 * totBestMoveChanges / threads.size();

            double totalTime =
              mainThread->tm.optimum() * fallingEval * reduction * bestMoveInstability;

            // Cap used time in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = std::min(500.0, totalTime);

            if (completedDepth >= 10 && nodesEffort >= 95
                && mainThread->tm.elapsed(threads.nodes_searched()) > totalTime * 3 / 4
                && !mainThread->ponder)
            {
                threads.stop = true;
            }

            // Stop the search if we have exceeded the totalTime
            if (mainThread->tm.elapsed(threads.nodes_searched()) > totalTime)
            {
                // If we are allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainThread->ponder)
                    mainThread->stopOnPonderhit = true;
                else
                    threads.stop = true;
            }
            else if (!mainThread->ponder
                     && mainThread->tm.elapsed(threads.nodes_searched()) > totalTime * 0.50)
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

    // If the skill level is enabled, swap the best PV line with the sub-optimal one
    if (skill.enabled())
        std::swap(rootMoves[0],
                  *std::find(rootMoves.begin(), rootMoves.end(),
                             skill.best ? skill.best : skill.pick_best(rootMoves, multiPV)));
}

void Search::Worker::clear() {
    counterMoves.fill(Move::none());
    mainHistory.fill(0);
    captureHistory.fill(0);
    pawnHistory.fill(0);
    correctionHistory.fill(0);

    for (bool inCheck : {false, true})
        for (StatsType c : {NoCaptures, Captures})
            for (auto& to : continuationHistory[inCheck][c])
                for (auto& h : to)
                    h->fill(-71);

    for (size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = int((18.79 + std::log(size_t(options["Threads"])) / 2) * std::log(i));
}


// Main search function for both PV and non-PV nodes.
template<NodeType nodeType>
Value Search::Worker::search(
  Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    constexpr bool PvNode   = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    // Dive into quiescence search when the depth reaches zero
    if (depth <= 0)
        return qsearch < PvNode ? PV : NonPV > (pos, ss, alpha, beta);

    // Check if we have an upcoming move that draws by repetition, or
    // if the opponent had an alternative move earlier to this position.
    if (!rootNode && alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
    {
        alpha = value_draw(this->nodes);
        if (alpha >= beta)
            return alpha;
    }

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(0 < depth && depth < MAX_PLY);
    assert(!(PvNode && cutNode));

    Move      pv[MAX_PLY + 1], capturesSearched[32], quietsSearched[32];
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    TTEntry* tte;
    Key      posKey;
    Move     ttMove, move, excludedMove, bestMove;
    Depth    extension, newDepth;
    Value    bestValue, value, ttValue, eval, maxValue, probCutBeta;
    bool     givesCheck, improving, priorCapture;
    bool     capture, moveCountPruning, ttCapture;
    Piece    movedPiece;
    int      moveCount, captureCount, quietCount;

    // Step 1. Initialize node
    Worker* thisThread = this;
    ss->inCheck        = pos.checkers();
    priorCapture       = pos.captured_piece();
    Color us           = pos.side_to_move();
    moveCount = captureCount = quietCount = ss->moveCount = 0;
    bestValue                                             = -VALUE_INFINITE;
    maxValue                                              = VALUE_INFINITE;

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
            return (ss->ply >= MAX_PLY && !ss->inCheck) ? evaluate(pos, thisThread->optimism[us])
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

    (ss + 1)->excludedMove = bestMove = Move::none();
    (ss + 2)->killers[0] = (ss + 2)->killers[1] = Move::none();
    (ss + 2)->cutoffCnt                         = 0;
    ss->multipleExtensions                      = (ss - 1)->multipleExtensions;
    Square prevSq = ((ss - 1)->currentMove).is_ok() ? ((ss - 1)->currentMove).to_sq() : SQ_NONE;
    ss->statScore = 0;

    // Step 4. Transposition table lookup.
    excludedMove = ss->excludedMove;
    posKey       = pos.key();
    tte          = tt.probe(posKey, ss->ttHit);
    ttValue   = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove    = rootNode  ? thisThread->rootMoves[thisThread->pvIdx].pv[0]
              : ss->ttHit ? tte->move()
                          : Move::none();
    ttCapture = ttMove && pos.capture_stage(ttMove);

    // At this point, if excluded, skip straight to step 6, static eval. However,
    // to save indentation, we list the condition in all code between here and there.
    if (!excludedMove)
        ss->ttPv = PvNode || (ss->ttHit && tte->is_pv());

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && !excludedMove && tte->depth() > depth
        && ttValue != VALUE_NONE  // Possible in case of TT access race or if !ttHit
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
    {
        // If ttMove is quiet, update move sorting heuristics on TT hit (~2 Elo)
        if (ttMove)
        {
            if (ttValue >= beta)
            {
                // Bonus for a quiet ttMove that fails high (~2 Elo)
                if (!ttCapture)
                    update_quiet_stats(pos, ss, *this, ttMove, stat_bonus(depth));

                // Extra penalty for early quiet moves of
                // the previous ply (~0 Elo on STC, ~2 Elo on LTC).
                if (prevSq != SQ_NONE && (ss - 1)->moveCount <= 2 && !priorCapture)
                    update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                                  -stat_malus(depth + 1));
            }
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < 90)
            return ttValue >= beta && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY
                   ? (ttValue * 3 + beta) / 4
                   : ttValue;
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

                // use the range VALUE_TB to VALUE_TB_WIN_IN_MAX_PLY to score
                value = wdl < -drawScore ? -tbValue
                      : wdl > drawScore  ? tbValue
                                         : VALUE_DRAW + 2 * wdl * drawScore;

                Bound b = wdl < -drawScore ? BOUND_UPPER
                        : wdl > drawScore  ? BOUND_LOWER
                                           : BOUND_EXACT;

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
        Eval::NNUE::hint_common_parent_position(pos);
        unadjustedStaticEval = eval = ss->staticEval;
    }
    else if (ss->ttHit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = tte->eval();
        if (unadjustedStaticEval == VALUE_NONE)
            unadjustedStaticEval = evaluate(pos, thisThread->optimism[us]);
        else if (PvNode)
            Eval::NNUE::hint_common_parent_position(pos);

        ss->staticEval = eval = to_corrected_static_eval(unadjustedStaticEval, *thisThread, pos);

        // ttValue can be used as a better position evaluation (~7 Elo)
        if (ttValue != VALUE_NONE && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    }
    else
    {
        unadjustedStaticEval = evaluate(pos, thisThread->optimism[us]);
        ss->staticEval = eval = to_corrected_static_eval(unadjustedStaticEval, *thisThread, pos);

        // Static evaluation is saved as it was before adjustment by correction history
        tte->save(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_NONE, Move::none(),
                  unadjustedStaticEval, tt.generation());
    }

    // Use static evaluation difference to improve quiet move ordering (~9 Elo)
    if (((ss - 1)->currentMove).is_ok() && !(ss - 1)->inCheck && !priorCapture)
    {
        int bonus = std::clamp(-14 * int((ss - 1)->staticEval + ss->staticEval), -1723, 1455);
        bonus     = bonus > 0 ? 2 * bonus : bonus / 2;
        thisThread->mainHistory[~us][((ss - 1)->currentMove).from_to()] << bonus;
        if (type_of(pos.piece_on(prevSq)) != PAWN && ((ss - 1)->currentMove).type_of() != PROMOTION)
            thisThread->pawnHistory[pawn_structure_index(pos)][pos.piece_on(prevSq)][prevSq]
              << bonus / 4;
    }

    // Set up the improving flag, which is true if current static evaluation is
    // bigger than the previous static evaluation at our turn (if we were in
    // check at our previous move we look at static evaluation at move prior to it
    // and if we were in check at move prior to it flag is set to true) and is
    // false otherwise. The improving flag is used in various pruning heuristics.
    improving = (ss - 2)->staticEval != VALUE_NONE
                ? ss->staticEval > (ss - 2)->staticEval
                : (ss - 4)->staticEval != VALUE_NONE && ss->staticEval > (ss - 4)->staticEval;

    // Step 7. Razoring (~1 Elo)
    // If eval is really low check with qsearch if it can exceed alpha, if it can't,
    // return a fail low.
    // Adjust razor margin according to cutoffCnt. (~1 Elo)
    if (eval < alpha - 438 - (332 - 154 * ((ss + 1)->cutoffCnt > 3)) * depth * depth)
    {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha)
            return value;
    }

    // Step 8. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 11
        && eval - futility_margin(depth, cutNode && !ss->ttHit, improving)
               - (ss - 1)->statScore / 314
             >= beta
        && eval >= beta && eval < 30016  // smaller than TB wins
        && (!ttMove || ttCapture))
        return beta > VALUE_TB_LOSS_IN_MAX_PLY ? (eval + beta) / 2 : eval;

    // Step 9. Null move search with verification search (~35 Elo)
    if (!PvNode && (ss - 1)->currentMove != Move::null() && (ss - 1)->statScore < 16620
        && eval >= beta && eval >= ss->staticEval && ss->staticEval >= beta - 21 * depth + 330
        && !excludedMove && pos.non_pawn_material(us) && ss->ply >= thisThread->nmpMinPly
        && beta > VALUE_TB_LOSS_IN_MAX_PLY)
    {
        assert(eval - beta >= 0);

        // Null move dynamic reduction based on depth and eval
        Depth R = std::min(int(eval - beta) / 154, 6) + depth / 3 + 4;

        ss->currentMove         = Move::null();
        ss->continuationHistory = &thisThread->continuationHistory[0][0][NO_PIECE][0];

        pos.do_null_move(st, tt);

        Value nullValue = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);

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
    // For PV nodes without a ttMove, we decrease depth by 2,
    // or by 4 if the current position is present in the TT and
    // the stored depth is greater than or equal to the current depth.
    // Use qsearch if depth <= 0.
    if (PvNode && !ttMove)
        depth -= 2 + 2 * (ss->ttHit && tte->depth() >= depth);

    if (depth <= 0)
        return qsearch<PV>(pos, ss, alpha, beta);

    // For cutNodes without a ttMove, we decrease depth by 2 if depth is high enough.
    if (cutNode && depth >= 8 && !ttMove)
        depth -= 2;

    // Step 11. ProbCut (~10 Elo)
    // If we have a good enough capture (or queen promotion) and a reduced search returns a value
    // much above beta, we can (almost) safely prune the previous move.
    probCutBeta = beta + 181 - 68 * improving;
    if (
      !PvNode && depth > 3
      && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
      // If value from transposition table is lower than probCutBeta, don't attempt probCut
      // there and in further interactions with transposition table cutoff depth is set to depth - 3
      // because probCut search has depth set to depth - 4 but we also do a move before it
      // So effective depth is equal to depth - 3
      && !(tte->depth() >= depth - 3 && ttValue != VALUE_NONE && ttValue < probCutBeta))
    {
        assert(probCutBeta < VALUE_INFINITE && probCutBeta > beta);

        MovePicker mp(pos, ttMove, probCutBeta - ss->staticEval, &thisThread->captureHistory);

        while ((move = mp.next_move()) != Move::none())
            if (move != excludedMove && pos.legal(move))
            {
                assert(pos.capture_stage(move));

                // Prefetch the TT entry for the resulting position
                prefetch(tt.first_entry(pos.key_after(move)));

                ss->currentMove = move;
                ss->continuationHistory =
                  &this
                     ->continuationHistory[ss->inCheck][true][pos.moved_piece(move)][move.to_sq()];

                thisThread->nodes.fetch_add(1, std::memory_order_relaxed);
                pos.do_move(move, st);

                // Perform a preliminary qsearch to verify that the move holds
                value = -qsearch<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

                // If the qsearch held, perform the regular search
                if (value >= probCutBeta)
                    value = -search<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, depth - 4,
                                           !cutNode);

                pos.undo_move(move);

                if (value >= probCutBeta)
                {
                    // Save ProbCut data into transposition table
                    tte->save(posKey, value_to_tt(value, ss->ply), ss->ttPv, BOUND_LOWER, depth - 3,
                              move, unadjustedStaticEval, tt.generation());
                    return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probCutBeta - beta)
                                                                     : value;
                }
            }

        Eval::NNUE::hint_common_parent_position(pos);
    }

moves_loop:  // When in check, search starts here

    // Step 12. A small Probcut idea, when we are in check (~4 Elo)
    probCutBeta = beta + 452;
    if (ss->inCheck && !PvNode && ttCapture && (tte->bound() & BOUND_LOWER)
        && tte->depth() >= depth - 4 && ttValue >= probCutBeta
        && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY)
        return probCutBeta;

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory,
                                        (ss - 3)->continuationHistory,
                                        (ss - 4)->continuationHistory,
                                        nullptr,
                                        (ss - 6)->continuationHistory};

    Move countermove =
      prevSq != SQ_NONE ? thisThread->counterMoves[pos.piece_on(prevSq)][prevSq] : Move::none();

    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory, &thisThread->captureHistory,
                  contHist, &thisThread->pawnHistory, countermove, ss->killers);

    value            = bestValue;
    moveCountPruning = false;

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

        if (rootNode && is_mainthread()
            && main_manager()->tm.elapsed(threads.nodes_searched()) > 3000)
            sync_cout << "info depth " << depth << " currmove "
                      << UCI::move(move, pos.is_chess960()) << " currmovenumber "
                      << moveCount + thisThread->pvIdx << sync_endl;
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
            if (!moveCountPruning)
                moveCountPruning = moveCount >= futility_move_count(improving, depth);

            // Reduced depth of the next LMR search
            int lmrDepth = newDepth - r;

            if (capture || givesCheck)
            {
                // Futility pruning for captures (~2 Elo)
                if (!givesCheck && lmrDepth < 7 && !ss->inCheck)
                {
                    Piece capturedPiece = pos.piece_on(move.to_sq());
                    int   futilityEval =
                      ss->staticEval + 277 + 292 * lmrDepth + PieceValue[capturedPiece]
                      + thisThread->captureHistory[movedPiece][move.to_sq()][type_of(capturedPiece)]
                          / 7;
                    if (futilityEval < alpha)
                        continue;
                }

                // SEE based pruning for captures and checks (~11 Elo)
                if (!pos.see_ge(move, -197 * depth))
                    continue;
            }
            else
            {
                int history =
                  (*contHist[0])[movedPiece][move.to_sq()]
                  + (*contHist[1])[movedPiece][move.to_sq()]
                  + (*contHist[3])[movedPiece][move.to_sq()]
                  + thisThread->pawnHistory[pawn_structure_index(pos)][movedPiece][move.to_sq()];

                // Continuation history based pruning (~2 Elo)
                if (lmrDepth < 6 && history < -4211 * depth)
                    continue;

                history += 2 * thisThread->mainHistory[us][move.from_to()];

                lmrDepth += history / 6437;

                // Futility pruning: parent node (~13 Elo)
                if (!ss->inCheck && lmrDepth < 15
                    && ss->staticEval + (bestValue < ss->staticEval - 57 ? 144 : 57)
                           + 121 * lmrDepth
                         <= alpha)
                    continue;

                lmrDepth = std::max(lmrDepth, 0);

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, -26 * lmrDepth * lmrDepth))
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
            // a reduced search on the position excluding the ttMove and if the result
            // is lower than ttValue minus a margin, then we will extend the ttMove.

            // Note: the depth margin and singularBeta margin are known for having non-linear
            // scaling. Their values are optimized to time controls of 180+1.8 and longer
            // so changing them requires tests at these types of time controls.
            // Recursive singular search is avoided.
            if (!rootNode && move == ttMove && !excludedMove
                && depth >= 4 - (thisThread->completedDepth > 30) + ss->ttPv
                && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && (tte->bound() & BOUND_LOWER)
                && tte->depth() >= depth - 3)
            {
                Value singularBeta  = ttValue - (60 + 54 * (ss->ttPv && !PvNode)) * depth / 64;
                Depth singularDepth = newDepth / 2;

                ss->excludedMove = move;
                value =
                  search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
                ss->excludedMove = Move::none();

                if (value < singularBeta)
                {
                    extension = 1;

                    // We make sure to limit the extensions in some way to avoid a search explosion
                    if (!PvNode && ss->multipleExtensions <= 16)
                    {
                        extension = 2 + (value < singularBeta - 78 && !ttCapture);
                        depth += depth < 16;
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
                    extension = -2;

                // If the ttMove is assumed to fail low over the value of the reduced search (~1 Elo)
                else if (ttValue <= value)
                    extension = -1;
            }

            // Recapture extensions (~1 Elo)
            else if (PvNode && move == ttMove && move.to_sq() == prevSq
                     && thisThread->captureHistory[movedPiece][move.to_sq()]
                                                  [type_of(pos.piece_on(move.to_sq()))]
                          > 4394)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;
        ss->multipleExtensions = (ss - 1)->multipleExtensions + (extension >= 2);

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

        // Decrease reduction if position is or has been on the PV (~7 Elo)
        if (ss->ttPv)
            r -= 1 + (ttValue > alpha) + (tte->depth() >= depth);

        // Increase reduction for cut nodes (~4 Elo)
        if (cutNode)
            r += 2 - (tte->depth() >= depth && ss->ttPv);

        // Increase reduction if ttMove is a capture (~3 Elo)
        if (ttCapture)
            r++;

        // Decrease reduction for PvNodes (~3 Elo)
        if (PvNode)
            r--;

        // Increase reduction on repetition (~1 Elo)
        if (move == (ss - 4)->currentMove && pos.has_repeated())
            r += 2;

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if ((ss + 1)->cutoffCnt > 3)
            r++;

        // Set reduction to 0 for first picked move (ttMove) (~2 Elo)
        // Nullifies all previous reduction adjustments to ttMove and leaves only history to do them
        else if (move == ttMove)
            r = 0;

        ss->statScore = 2 * thisThread->mainHistory[us][move.from_to()]
                      + (*contHist[0])[movedPiece][move.to_sq()]
                      + (*contHist[1])[movedPiece][move.to_sq()]
                      + (*contHist[3])[movedPiece][move.to_sq()] - 4392;

        // Decrease/increase reduction for moves with a good/bad history (~8 Elo)
        r -= ss->statScore / 14189;

        // Step 17. Late moves reduction / extension (LMR, ~117 Elo)
        if (depth >= 2 && moveCount > 1 + rootNode)
        {
            // In general we want to cap the LMR depth search at newDepth, but when
            // reduction is negative, we allow this move a limited search extension
            // beyond the first move depth. This may lead to hidden multiple extensions.
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth d = std::max(1, std::min(newDepth - r, newDepth + 1));

            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result
                // was good enough search deeper, if it was bad enough search shallower.
                const bool doDeeperSearch    = value > (bestValue + 49 + 2 * newDepth);  // (~1 Elo)
                const bool doShallowerSearch = value < bestValue + newDepth;             // (~2 Elo)

                newDepth += doDeeperSearch - doShallowerSearch;

                if (newDepth > d)
                    value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                // Post LMR continuation history updates (~1 Elo)
                int bonus = value <= alpha ? -stat_malus(newDepth)
                          : value >= beta  ? stat_bonus(newDepth)
                                           : 0;

                update_continuation_histories(ss, movedPiece, move.to_sq(), bonus);
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PvNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present (~1 Elo)
            if (!ttMove)
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

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth, false);
        }

        // Step 19. Undo move
        pos.undo_move(move);

        if (rootNode)
            effort[move.from_sq()][move.to_sq()] += nodes - nodeCount;

        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and we return immediately without
        // updating best move, PV and TT.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (rootNode)
        {
            RootMove& rm =
              *std::find(thisThread->rootMoves.begin(), thisThread->rootMoves.end(), move);

            rm.averageScore =
              rm.averageScore != -VALUE_INFINITE ? (2 * value + rm.averageScore) / 3 : value;

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
                    if (depth > 2 && depth < 13 && beta < 13652 && value > -12761)
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
                capturesSearched[captureCount++] = move;
            else
                quietsSearched[quietCount++] = move;
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
        bestValue = (bestValue * (depth + 2) + beta) / (depth + 3);

    if (!moveCount)
        bestValue = excludedMove ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha we update the stats of searched moves
    else if (bestMove)
        update_all_stats(pos, ss, *this, bestMove, bestValue, beta, prevSq, quietsSearched,
                         quietCount, capturesSearched, captureCount, depth);

    // Bonus for prior countermove that caused the fail low
    else if (!priorCapture && prevSq != SQ_NONE)
    {
        int bonus = (depth > 5) + (PvNode || cutNode) + ((ss - 1)->statScore < -15736)
                  + ((ss - 1)->moveCount > 11);
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                      stat_bonus(depth) * bonus);
        thisThread->mainHistory[~us][((ss - 1)->currentMove).from_to()]
          << stat_bonus(depth) * bonus / 2;
    }

    if (PvNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~7 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    // Static evaluation is saved as it was before correction history
    if (!excludedMove && !(rootNode && thisThread->pvIdx))
        tte->save(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
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


// Quiescence search function, which is called by the main search
// function with zero depth, or recursively with further decreasing depth per call.
// (~155 Elo)
template<NodeType nodeType>
Value Search::Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) {

    static_assert(nodeType != Root);
    constexpr bool PvNode = nodeType == PV;

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PvNode || (alpha == beta - 1));
    assert(depth <= 0);

    // Check if we have an upcoming move that draws by repetition, or if
    // the opponent had an alternative move earlier to this position. (~1 Elo)
    if (alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
    {
        alpha = value_draw(this->nodes);
        if (alpha >= beta)
            return alpha;
    }

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
        return (ss->ply >= MAX_PLY && !ss->inCheck) ? evaluate(pos, thisThread->optimism[us])
                                                    : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Decide the replacement and cutoff priority of the qsearch TT entries
    ttDepth = ss->inCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;

    // Step 3. Transposition table lookup
    posKey  = pos.key();
    tte     = tt.probe(posKey, ss->ttHit);
    ttValue = ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove  = ss->ttHit ? tte->move() : Move::none();
    pvHit   = ss->ttHit && tte->is_pv();

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && tte->depth() >= ttDepth
        && ttValue != VALUE_NONE  // Only in case of TT access race or if !ttHit
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    // Step 4. Static evaluation of the position
    Value unadjustedStaticEval = VALUE_NONE;
    if (ss->inCheck)
        bestValue = futilityBase = -VALUE_INFINITE;
    else
    {
        if (ss->ttHit)
        {
            // Never assume anything about values stored in TT
            unadjustedStaticEval = tte->eval();
            if (unadjustedStaticEval == VALUE_NONE)
                unadjustedStaticEval = evaluate(pos, thisThread->optimism[us]);
            ss->staticEval = bestValue =
              to_corrected_static_eval(unadjustedStaticEval, *thisThread, pos);

            // ttValue can be used as a better position evaluation (~13 Elo)
            if (ttValue != VALUE_NONE
                && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttValue;
        }
        else
        {
            // In case of null move search, use previous static eval with a different sign
            unadjustedStaticEval = (ss - 1)->currentMove != Move::null()
                                   ? evaluate(pos, thisThread->optimism[us])
                                   : -(ss - 1)->staticEval;
            ss->staticEval       = bestValue =
              to_corrected_static_eval(unadjustedStaticEval, *thisThread, pos);
        }

        // Stand pat. Return immediately if static value is at least beta
        if (bestValue >= beta)
        {
            if (!ss->ttHit)
                tte->save(posKey, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER, DEPTH_NONE,
                          Move::none(), unadjustedStaticEval, tt.generation());

            return bestValue;
        }

        if (bestValue > alpha)
            alpha = bestValue;

        futilityBase = ss->staticEval + 206;
    }

    const PieceToHistory* contHist[] = {(ss - 1)->continuationHistory,
                                        (ss - 2)->continuationHistory};

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions, and other checks (only if depth >= DEPTH_QS_CHECKS)
    // will be generated.
    Square     prevSq = ((ss - 1)->currentMove).is_ok() ? ((ss - 1)->currentMove).to_sq() : SQ_NONE;
    MovePicker mp(pos, ttMove, depth, &thisThread->mainHistory, &thisThread->captureHistory,
                  contHist, &thisThread->pawnHistory);

    int quietCheckEvasions = 0;

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
            if (!givesCheck && move.to_sq() != prevSq && futilityBase > VALUE_TB_LOSS_IN_MAX_PLY
                && move.type_of() != PROMOTION)
            {
                if (moveCount > 2)
                    continue;

                futilityValue = futilityBase + PieceValue[pos.piece_on(move.to_sq())];

                // If static eval + value of piece we are going to capture is much lower
                // than alpha we can prune this move. (~2 Elo)
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }

                // If static eval is much lower than alpha and move is not winning material
                // we can prune this move. (~2 Elo)
                if (futilityBase <= alpha && !pos.see_ge(move, 1))
                {
                    bestValue = std::max(bestValue, futilityBase);
                    continue;
                }

                // If static exchange evaluation is much worse than what is needed to not
                // fall below alpha we can prune this move.
                if (futilityBase > alpha && !pos.see_ge(move, (alpha - futilityBase) * 4))
                {
                    bestValue = alpha;
                    continue;
                }
            }

            // We prune after the second quiet check evasion move, where being 'in check' is
            // implicitly checked through the counter, and being a 'quiet move' apart from
            // being a tt move is assumed after an increment because captures are pushed ahead.
            if (quietCheckEvasions > 1)
                break;

            // Continuation history based pruning (~3 Elo)
            if (!capture && (*contHist[0])[pos.moved_piece(move)][move.to_sq()] < 0
                && (*contHist[1])[pos.moved_piece(move)][move.to_sq()] < 0)
                continue;

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (!pos.see_ge(move, -74))
                continue;
        }

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.key_after(move)));

        // Update the current move
        ss->currentMove = move;
        ss->continuationHistory =
          &thisThread
             ->continuationHistory[ss->inCheck][capture][pos.moved_piece(move)][move.to_sq()];

        quietCheckEvasions += !capture && ss->inCheck;

        // Step 7. Make and search the move
        thisThread->nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);
        value = -qsearch<nodeType>(pos, ss + 1, -beta, -alpha, depth - 1);
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
    // All legal moves have been searched. A special case: if we're in check
    // and no legal moves were found, it is checkmate.
    if (ss->inCheck && bestValue == -VALUE_INFINITE)
    {
        assert(!MoveList<LEGAL>(pos).size());
        return mated_in(ss->ply);  // Plies to mate from the root
    }

    if (std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY && bestValue >= beta)
        bestValue = (3 * bestValue + beta) / 4;

    // Save gathered info in transposition table
    // Static evaluation is saved as it was before adjustment by correction history
    tte->save(posKey, value_to_tt(bestValue, ss->ply), pvHit,
              bestValue >= beta ? BOUND_LOWER : BOUND_UPPER, ttDepth, bestMove,
              unadjustedStaticEval, tt.generation());

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);

    return bestValue;
}

Depth Search::Worker::reduction(bool i, Depth d, int mn, int delta) {
    int reductionScale = reductions[d] * reductions[mn];
    return (reductionScale + 1118 - delta * 793 / rootDelta) / 1024 + (!i && reductionScale > 863);
}

namespace {
// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
Value value_to_tt(Value v, int ply) {

    assert(v != VALUE_NONE);
    return v >= VALUE_TB_WIN_IN_MAX_PLY ? v + ply : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply : v;
}


// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated from
// current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, we return the highest non-TB score instead.
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
void update_all_stats(const Position& pos,
                      Stack*          ss,
                      Search::Worker& workerThread,
                      Move            bestMove,
                      Value           bestValue,
                      Value           beta,
                      Square          prevSq,
                      Move*           quietsSearched,
                      int             quietCount,
                      Move*           capturesSearched,
                      int             captureCount,
                      Depth           depth) {

    Color                  us             = pos.side_to_move();
    CapturePieceToHistory& captureHistory = workerThread.captureHistory;
    Piece                  moved_piece    = pos.moved_piece(bestMove);
    PieceType              captured;

    int quietMoveBonus = stat_bonus(depth + 1);
    int quietMoveMalus = stat_malus(depth);

    if (!pos.capture_stage(bestMove))
    {
        int bestMoveBonus = bestValue > beta + 166 ? quietMoveBonus      // larger bonus
                                                   : stat_bonus(depth);  // smaller bonus

        // Increase stats for the best move in case it was a quiet move
        update_quiet_stats(pos, ss, workerThread, bestMove, bestMoveBonus);

        int pIndex = pawn_structure_index(pos);
        workerThread.pawnHistory[pIndex][moved_piece][bestMove.to_sq()] << quietMoveBonus;

        // Decrease stats for all non-best quiet moves
        for (int i = 0; i < quietCount; ++i)
        {
            workerThread
                .pawnHistory[pIndex][pos.moved_piece(quietsSearched[i])][quietsSearched[i].to_sq()]
              << -quietMoveMalus;

            workerThread.mainHistory[us][quietsSearched[i].from_to()] << -quietMoveMalus;
            update_continuation_histories(ss, pos.moved_piece(quietsSearched[i]),
                                          quietsSearched[i].to_sq(), -quietMoveMalus);
        }
    }
    else
    {
        // Increase stats for the best move in case it was a capture move
        captured = type_of(pos.piece_on(bestMove.to_sq()));
        captureHistory[moved_piece][bestMove.to_sq()][captured] << quietMoveBonus;
    }

    // Extra penalty for a quiet early move that was not a TT move or
    // main killer move in previous ply when it gets refuted.
    if (prevSq != SQ_NONE
        && ((ss - 1)->moveCount == 1 + (ss - 1)->ttHit
            || ((ss - 1)->currentMove == (ss - 1)->killers[0]))
        && !pos.captured_piece())
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq, -quietMoveMalus);

    // Decrease stats for all non-best capture moves
    for (int i = 0; i < captureCount; ++i)
    {
        moved_piece = pos.moved_piece(capturesSearched[i]);
        captured    = type_of(pos.piece_on(capturesSearched[i].to_sq()));
        captureHistory[moved_piece][capturesSearched[i].to_sq()][captured] << -quietMoveMalus;
    }
}


// Updates histories of the move pairs formed
// by moves at ply -1, -2, -3, -4, and -6 with current move.
void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {

    for (int i : {1, 2, 3, 4, 6})
    {
        // Only update the first 2 continuation histories if we are in check
        if (ss->inCheck && i > 2)
            break;
        if (((ss - i)->currentMove).is_ok())
            (*(ss - i)->continuationHistory)[pc][to] << bonus / (1 + 3 * (i == 3));
    }
}


// Updates move sorting heuristics
void update_quiet_stats(
  const Position& pos, Stack* ss, Search::Worker& workerThread, Move move, int bonus) {

    // Update killers
    if (ss->killers[0] != move)
    {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = move;
    }

    Color us = pos.side_to_move();
    workerThread.mainHistory[us][move.from_to()] << bonus;
    update_continuation_histories(ss, pos.moved_piece(move), move.to_sq(), bonus);

    // Update countermove history
    if (((ss - 1)->currentMove).is_ok())
    {
        Square prevSq                                           = ((ss - 1)->currentMove).to_sq();
        workerThread.counterMoves[pos.piece_on(prevSq)][prevSq] = move;
    }
}
}

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
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


// Used to print debug info and, more importantly,
// to detect when we are out of available time and thus stop the search.
void SearchManager::check_time(Search::Worker& worker) {
    if (--callsCnt > 0)
        return;

    // When using nodes, ensure checking rate is not lower than 0.1% of nodes
    callsCnt = worker.limits.nodes ? std::min(512, int(worker.limits.nodes / 1024)) : 512;

    static TimePoint lastInfoTime = now();

    TimePoint elapsed = tm.elapsed(worker.threads.nodes_searched());
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

std::string SearchManager::pv(const Search::Worker&     worker,
                              const ThreadPool&         threads,
                              const TranspositionTable& tt,
                              Depth                     depth) const {
    std::stringstream ss;

    const auto  nodes     = threads.nodes_searched();
    const auto& rootMoves = worker.rootMoves;
    const auto& pos       = worker.rootPos;
    size_t      pvIdx     = worker.pvIdx;
    TimePoint   time      = tm.elapsed(nodes) + 1;
    size_t      multiPV   = std::min(size_t(worker.options["MultiPV"]), rootMoves.size());
    uint64_t    tbHits    = threads.tb_hits() + (worker.tbConfig.rootInTB ? rootMoves.size() : 0);

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

        if (ss.rdbuf()->in_avail())  // Not at first line
            ss << "\n";

        ss << "info"
           << " depth " << d << " seldepth " << rootMoves[i].selDepth << " multipv " << i + 1
           << " score " << UCI::value(v);

        if (worker.options["UCI_ShowWDL"])
            ss << UCI::wdl(v, pos.game_ply());

        if (i == pvIdx && !tb && updated)  // tablebase- and previous-scores are exact
            ss << (rootMoves[i].scoreLowerbound
                     ? " lowerbound"
                     : (rootMoves[i].scoreUpperbound ? " upperbound" : ""));

        ss << " nodes " << nodes << " nps " << nodes * 1000 / time << " hashfull " << tt.hashfull()
           << " tbhits " << tbHits << " time " << time << " pv";

        for (Move m : rootMoves[i].pv)
            ss << " " << UCI::move(m, pos.is_chess960());
    }

    return ss.str();
}

// Called in case we have no ponder move before exiting the search,
// for instance, in case we stop the search during a fail high at root.
// We try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' we have nothing to think about.
bool RootMove::extract_ponder_from_tt(const TranspositionTable& tt, Position& pos) {

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    bool ttHit;

    assert(pv.size() == 1);
    if (pv[0] == Move::none())
        return false;

    pos.do_move(pv[0], st);
    TTEntry* tte = tt.probe(pos.key(), ttHit);

    if (ttHit)
    {
        Move m = tte->move();  // Local copy to be SMP safe
        if (MoveList<LEGAL>(pos).contains(m))
            pv.push_back(m);
    }

    pos.undo_move(pv[0]);
    return pv.size() > 1;
}


}  // namespace Stockfish
