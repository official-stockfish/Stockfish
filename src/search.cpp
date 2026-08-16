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

#include "attacks.h"          // <-- नया include (king_hunter_score के लिए)
#include "bitboard.h"         // <-- नया include
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

static constexpr std::array<int, 16> lmrDivisor = {3637, 2787, 2761, 2939, 3171, 3347, 3147, 2762,
                                                   2772, 3106, 3107, 3060, 3112, 2991, 3090, 3542};

namespace TB = Tablebases;

// ---------- किंग हंटर बोनस (प्रयोगात्मक) ----------
template<Color Us>
Value king_hunter_score(const Position& pos) {
    constexpr Color Them = ~Us;
    Square ksq = pos.square<KING>(Them);
    Bitboard kingRing = Attacks::attacks_bb<KING>(ksq) | square_bb(ksq);

    int score = 0;

    // 1. हमारे मोहरों द्वारा किंग रिंग पर हमले
    Bitboard ourPieces = pos.pieces(Us);
    while (ourPieces) {
        Square s = pop_lsb(ourPieces);
        PieceType pt = type_of(pos.piece_on(s));
        Bitboard attacks = Attacks::attacks_bb(pt, s, pos.pieces());

        if (attacks & kingRing) {
            int w = 0;
            switch (pt) {
                case QUEEN:  w = 12; break;
                case ROOK:   w = 7;  break;
                case BISHOP: w = 5;  break;
                case KNIGHT: w = 5;  break;
                case PAWN:   w = 2;  break;
                default:     w = 0;  break;
            }
            if (attacks & square_bb(ksq))
                w += 10;
            score += w;
        }
    }

    // 2. राजा की प्यादा ढाल
    int friendlyPawns = popcount(pos.pieces(Them, PAWN) & kingRing);
    score += (8 - friendlyPawns) * 4;

    // 3. राजा की गतिशीलता
    Bitboard kingMoves = Attacks::attacks_bb<KING>(ksq) & ~pos.pieces(Them) & ~pos.attackers_to(ksq, pos.pieces());
    int mobility = popcount(kingMoves);
    score += (8 - mobility) * 3;

    // 4. प्यादों की उन्नति (Pawn storm)
    Bitboard pawnStorm = pos.pieces(Us, PAWN) & Attacks::attacks_bb<KING>(ksq);
    score += popcount(pawnStorm) * 6;

    return Value(score * 8);
}
// -------------------------------------------------

void syzygy_extend_pv(const OptionsMap&            options,
                      const Search::LimitsType&    limits,
                      Stockfish::Position&         pos,
                      Stockfish::Search::RootMove& rootMove,
                      Value&                       v);

using namespace Search;

namespace {

constexpr u64 NODES_LIMIT_OUTPUT = 10'000'000;

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
    const int   pcv    = shared.pawn_correction_entry(pos)[us].pawn;
    const int   micv   = shared.minor_piece_correction_entry(pos)[us].minor;
    const int   wnpcv  = shared.nonpawn_correction_entry<WHITE>(pos)[us].nonPawnWhite;
    const int   bnpcv  = shared.nonpawn_correction_entry<BLACK>(pos)[us].nonPawnBlack;
    const int   cntcv =
      m.is_ok()
          ? 8761
            * ((*(ss - 2)->continuationCorrectionHistory)[pos.piece_on(m.to_sq())][m.to_sq()]
               + (*(ss - 4)->continuationCorrectionHistory)[pos.piece_on(m.to_sq())][m.to_sq()])
          : 64049;

    return 15341 * pcv + 10569 * micv + 12906 * (wnpcv + bnpcv) + cntcv;
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

    constexpr int nonPawnWeight = 186;
    auto&         shared        = workerThread.sharedHistory;

    shared.pawn_correction_entry(pos)[us].pawn << bonus;
    shared.minor_piece_correction_entry(pos)[us].minor << bonus * 150 / 128;
    shared.nonpawn_correction_entry<WHITE>(pos)[us].nonPawnWhite << bonus * nonPawnWeight / 128;
    shared.nonpawn_correction_entry<BLACK>(pos)[us].nonPawnBlack << bonus * nonPawnWeight / 128;

    if (m.is_ok())
    {
        const Square to = m.to_sq();
        const Piece  pc = pos.piece_on(to);
        (*(ss - 2)->continuationCorrectionHistory)[pc][to] << bonus * 130 / 128;
        (*(ss - 4)->continuationCorrectionHistory)[pc][to] << bonus * 70 / 128;
    }
}

// Add a small random component to draw evaluations to avoid 3-fold blindness
Value value_draw(usize nodes) { return VALUE_DRAW - 1 + Value(nodes & 0x2); }
Value value_to_tt(Value v, int ply);
Value value_from_tt(Value v, int ply, int r50c);
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
                      Move            ttMove,
                      bool            PvNode);

// Detect shuffling moves in order to limit search explosions
// Added in #6447 as non-regression, and so its parameters should not be tuned
bool is_shuffling(Move move, Stack* const ss, const Position& pos) {
    if (pos.capture_stage(move) || pos.rule50_count() < 10)
        return false;
    if (pos.state()->pliesFromNull < 6 || ss->ply < 20)
        return false;
    return move.from_sq() == (ss - 2)->currentMove.to_sq()
        && (ss - 2)->currentMove.from_sq() == (ss - 4)->currentMove.to_sq();
}

// Look up the futility pruning cutoff depth. This function is important for mate finding.
inline int futility_depth(Value eval, Value beta) {
    // LUT values obtained from:
    //      depth = 13 + int(0.5 + 6 / int(1 + pow(abs(eval) + abs(beta), 3) / 50'000'000'000))
    static constexpr std::array Lut{Value(1657), 2555, 3294, 4122, 5314, 8194, VALUE_INFINITE * 2};
    const Value                 prob  = std::abs(eval) + std::abs(beta);
    int                         depth = 0;
    while (Lut[depth] < prob)
        ++depth;

    return 19 - depth;
}

}  // namespace

Search::Worker::Worker(SharedState&                    sharedState,
                       std::unique_ptr<ISearchManager> sm,
                       usize                           threadId,
                       usize                           numaThreadId,
                       usize                           numaTotalThreads,
                       NumaReplicatedAccessToken       token) :
    // Unpack the SharedState struct into member variables
    sharedHistory(sharedState.sharedHistories.at(token.get_numa_index())),
    continuationHistory(sharedHistory.continuationHistory()),
    threadIdx(threadId),
    numaThreadIdx(numaThreadId),
    numaTotal(numaTotalThreads),
    numaAccessToken(token),
    manager(std::move(sm)),
    options(sharedState.options),
    threads(sharedState.threads),
    tt(sharedState.tt),
    network(sharedState.network),
    refreshTable(network[token]) {
    clear();
}

void Search::Worker::ensure_network_replicated() {
    // Access once to force lazy initialization.
    // We do this because we want to avoid initialization during search.
    (void) (network[numaAccessToken]);
}

void Search::Worker::start_searching() {

    accumulatorStack.reset();

    // Non-main threads go directly to iterative_deepening()
    if (!is_mainthread())
    {
        iterative_deepening();
        return;
    }

    main_manager()->tm.init(limits, rootPos.side_to_move(), rootPos.game_ply(), options,
                            main_manager()->originalTimeAdjust);
    tt.new_search();
    main_manager()->updates.onStart();

    if (rootMoves.empty())
    {
        main_manager()->updates.onUpdateNoMoves(
          {0, {rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW, rootPos}});
        main_manager()->updates.onBestmove(UCIEngine::move(Move::none()), "");
        return;
    }

    // Main thread starts non-main threads, and begins own search.
    threads.start_searching();
    bool uciPvSent = iterative_deepening();

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

    if (!limits.depth && !skill.enabled())
        bestThread = threads.get_best_thread()->worker.get();

    main_manager()->bestPreviousScore        = bestThread->rootMoves[0].score;
    main_manager()->bestPreviousAverageScore = bestThread->rootMoves[0].averageScore;

    if (bestThread->rootMoves[0].pv.size() == 1
        && bestThread->rootMoves[0].extract_ponder_from_tt(tt, rootPos))
        uciPvSent = false;

    // Send PV info if it has changed since last output in iterative_deepening().
    if (!uciPvSent || bestThread != this)
        main_manager()->output_pv(*bestThread, threads, tt, bestThread->rootDepth);

    // In rare cases, output_pv() may change the ponder move through syzygy_extend_pv().
    std::string ponder;
    if (bestThread->rootMoves[0].pv.size() > 1)
        ponder = UCIEngine::move(bestThread->rootMoves[0].pv[1], rootPos.is_chess960());

    auto bestmove = UCIEngine::move(bestThread->rootMoves[0].pv[0], rootPos.is_chess960());
    main_manager()->updates.onBestmove(bestmove, ponder);
}

// Main iterative deepening loop. It calls search()
// repeatedly with increasing depth until the allocated thinking time has been
// consumed, the user stops the search, or the maximum search depth is reached.
bool Search::Worker::iterative_deepening() {

    SearchManager* mainThread = (is_mainthread() ? main_manager() : nullptr);

    PVMoves pv;

    PVMoves lastBestMovePV;
    Depth   lastBestMoveDepth = 0;
    Value   lastBestMoveScore = -VALUE_INFINITE;

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

    ss->pv = &pv;

    if (mainThread)
    {
        if (mainThread->bestPreviousScore == VALUE_INFINITE)
            mainThread->iterValue.fill(VALUE_ZERO);
        else
            mainThread->iterValue.fill(mainThread->bestPreviousScore);
    }

    usize multiPV = usize(options["MultiPV"]);
    Skill skill(options["Skill Level"], options["UCI_LimitStrength"] ? int(options["UCI_Elo"]) : 0);

    // When playing with strength handicap enable MultiPV search that we will
    // use behind-the-scenes to retrieve a set of possible moves.
    if (skill.enabled())
        multiPV = std::max(multiPV, usize(4));

    multiPV = std::min(multiPV, rootMoves.size());

    int  searchAgainCounter = 0;
    bool uciPvSent          = false;

    lowPlyHistory.fill(102);

    for (Color c : {WHITE, BLACK})
        for (int i = 0; i < UINT_16_HISTORY_SIZE; i++)
            mainHistory[c][i] = mainHistory[c][i] * 729 / 1024;

    // Iterative deepening loop until requested to stop or the target depth is reached
    while (rootDepth + 1 < MAX_PLY && !threads.stop
           && !(limits.depth && mainThread && rootDepth >= limits.depth))
    {
        rootDepth++;

        // Age out PV variability metric and signal the start of a new iteration.
        if (mainThread)
        {
            totBestMoveChanges /= 2;
            uciPvSent = false;
        }

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (usize i = 0; i < rootMoves.size(); ++i)
        {
            rootMoves[i].previousScore      = rootMoves[i].score;
            rootMoves[i].previousPV         = rootMoves[i].pv;
            rootMoves[i].previousScoreExact = i < multiPV;
        }

        usize pvFirst = pvLast = 0;

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

            lastIterationIdxPV = rootMoves[pvIdx].previousPV;

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = 0;

            // Reset aspiration window starting size
            delta     = 5 + threadIdx % 8 + std::abs(rootMoves[pvIdx].meanSquaredScore) / 10193;
            Value avg = rootMoves[pvIdx].averageScore;
            alpha     = std::max(avg - delta, -VALUE_INFINITE);
            beta      = std::min(avg + delta, VALUE_INFINITE);

            // Adjust optimism based on root move's averageScore
            optimism[us]  = 114 * avg / (std::abs(avg) + 85);
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
                    && nodes > NODES_LIMIT_OUTPUT)
                    main_manager()->output_pv(*this, threads, tt, rootDepth);

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

                delta += 47 * delta / 128;

                assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
            }

            if (threads.stop && pvIdx)
            {
                // In multiPV analysis we do not let aborted searches spoil mated-in/
                // TB loss scores from a completed search in an earlier PV line.
                // Hence we guard against an aborted pvIdx line overtaking pvIdx - 1
                // when pvIdx - 1 is a proven loss.
                // Moreover, we do not trust an exact loss score from an aborted search.
                if ((is_loss(rootMoves[pvIdx - 1].score) && rootMoves[pvIdx] < rootMoves[pvIdx - 1])
                    || rootMoves[pvIdx].score_is_exact_loss())
                {
                    // If previousScore is exact and worse than pvIdx - 1, we can safely use it.
                    // If it is equal, we make sure it cannot overtake pvIdx - 1.
                    if (rootMoves[pvIdx].previousScore != -VALUE_INFINITE
                        && rootMoves[pvIdx].previousScoreExact
                        && rootMoves[pvIdx].previousScore <= rootMoves[pvIdx - 1].score)
                    {
                        rootMoves[pvIdx].score = rootMoves[pvIdx].uciScore =
                          rootMoves[pvIdx].previousScore;
                        rootMoves[pvIdx].previousScore = -VALUE_INFINITE;
                        rootMoves[pvIdx].pv            = rootMoves[pvIdx].previousPV;
                        rootMoves[pvIdx].unset_bound_flags();
                    }

                    // Otherwise, if we can, we cap the score to the best possible, and mark
                    // the score as a bound (also a valid excuse for the incomplete PV.)
                    else
                    {
                        if (is_loss(rootMoves[pvIdx - 1].score))
                        {
                            rootMoves[pvIdx].score = rootMoves[pvIdx].uciScore =
                              rootMoves[pvIdx - 1].score - 1;
                            rootMoves[pvIdx].pv = rootMoves[pvIdx - 1].pv;
                        }
                        else
                        {
                            rootMoves[pvIdx].score = rootMoves[pvIdx].uciScore =
                              rootMoves[pvIdx - 1].score + 1;
                            rootMoves[pvIdx].pv = rootMoves[pvIdx - 1].pv;
                        }
                        rootMoves[pvIdx].flagLowerBound = true;
                    }
                }
                else if (pvIdx)
                    rootMoves[pvIdx].score = rootMoves[pvIdx].uciScore =
                      rootMoves[pvIdx].previousScore;

                if (!rootMoves[pvIdx].pv.empty())
                {
                    std::stable_sort(rootMoves.begin() + pvIdx, rootMoves.begin() + pvLast);
                    continue;
                }
            }

            // After searching the PV line, update the average score for this root move
            rootMoves[pvIdx].averageScore = rootMoves[pvIdx].score;

            // Reset for next PV line
            std::fill(std::begin(optimism), std::end(optimism), VALUE_ZERO);
        }

        if (threads.stop)
            break;

        // Sort the PV lines searched so far and update the best move if changed
        std::stable_sort(rootMoves.begin(), rootMoves.begin() + pvLast);

        // Send PV info to GUI
        if (mainThread)
        {
            uciPvSent = true;
            main_manager()->output_pv(*this, threads, tt, rootDepth);
        }
    }

    return uciPvSent;
}

// Main search function for both PV and non-PV nodes.
//
// This function is called recursively. Its parameter 'depth' is the remaining
// depth to search, always >= 1. 'ss' is the current stack. 'alpha' and 'beta'
// are the alpha-beta bounds. 'PvNode' is true if the node is a PV node.
//
// It returns the evaluation value of the position from the point of view of the
// side to move.
template<NodeType nodeType>
Value Search::Worker::search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode) {

    constexpr bool PvNode = nodeType != NonPV;
    const bool rootNode  = PvNode && ss->ply == 0;

    // Check if we have to stop the search, or if the maximum depth is reached.
    if (threads.stop.load(std::memory_order_relaxed)
        || (is_mainthread() && limits.nodes && threads.nodes_searched() >= limits.nodes))
        return VALUE_ZERO;

    // Use the current position's side to move
    const Color us = pos.side_to_move();

    // Update the global nodes counter and the thread-specific nodes counter
    nodes.fetch_add(1, std::memory_order_relaxed);
    if (threadIdx == 0)
        main_manager()->nodes += 1;

    // Mate distance pruning
    alpha = std::max(alpha, value_mated_in(ss->ply + 1));
    beta  = std::min(beta, value_mate_in(ss->ply + 1));
    if (alpha >= beta)
        return alpha;

    // Transposition table lookup. At PV nodes, we don't use the TT for pruning,
    // but we use it for move ordering and to get the static evaluation.
    const bool ttHit = tt.probe(pos, ss, ttValue, ttMove, ttHit, ttDepth, ttBound, ttStaticEval);
    ss->ttMove       = ttMove;
    ss->ttHit        = ttHit;

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && ttHit && ttDepth >= depth
        && ttBound != BOUND_NONE  // Only if ttBound is valid
        && (ttBound == BOUND_EXACT || (ttBound == BOUND_LOWER ? ttValue >= beta : ttValue <= alpha)))
    {
        // If ttMove is not a legal move, we should not use it for pruning
        if (ttMove.is_ok())
            ss->ttMove = ttMove;

        return value_from_tt(ttValue, ss->ply, pos.rule50_count());
    }

    // Update the static evaluation of the position
    Value staticEval = ss->staticEval;

    // If the static evaluation hasn't been set yet, compute it
    if (staticEval == VALUE_NONE)
    {
        // Use the NNUE evaluation
        auto [psqt, positional] = network[numaAccessToken].evaluate(pos, accumulatorStack);
        staticEval              = psqt + positional;

        // ----- किंग हंटर बोनस जोड़ें (केवल गैर-PV नोड्स पर) -----
        if (!PvNode) {
            Value kingBonus = (us == WHITE) ? king_hunter_score<WHITE>(pos) : king_hunter_score<BLACK>(pos);
            staticEval += kingBonus;
        }
        // ---------------------------------------------------------

        // Store the static evaluation in the stack
        ss->staticEval = staticEval;

        // Store the static evaluation in the TT if not already present
        if (!ttHit)
            tt.store(pos, ss, VALUE_NONE, BOUND_NONE, Move::none(), 0, staticEval);
    }

    // Use the corrected static evaluation if available
    if (ss->staticEvalCorrection != 0)
        staticEval = to_corrected_static_eval(staticEval, ss->staticEvalCorrection);

    // Razoring (only at non-PV nodes)
    if (!PvNode && depth < 4 && staticEval + 201 * depth < beta)
    {
        if (staticEval + 218 * depth < beta && depth < 3)
            return staticEval;

        Value ralpha = beta - 45 * depth;
        Value v      = search<NonPV>(pos, ss + 1, ralpha - 1, ralpha, depth - 1, false);
        if (v < ralpha)
            return v;
    }

    // Futility pruning (only at non-PV nodes)
    if (!PvNode && depth < 7 && staticEval + futility_depth(staticEval, beta) < beta
        && pos.non_pawn_material(us) + pos.count<PAWN>(us) * 534 >= 0)
        return staticEval;

    // Null move search
    if (!PvNode && depth >= 2 && !pos.checkers() && staticEval >= beta
        && pos.non_pawn_material(us) > 0)
    {
        ss->currentMove = Move::none();

        // Depth reduction for null move
        Depth R = 3 + depth / 3 + std::min(1, (staticEval - beta) / 200);

        pos.do_null_move(ss + 1);
        Value v = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
        pos.undo_null_move();

        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (v >= beta)
            return v;
    }

    // Generate all legal moves
    MoveList<LEGAL> moves(pos);
    if (moves.size() == 0)
        return pos.checkers() ? value_mated_in(ss->ply) : VALUE_DRAW;

    // Move ordering
    MovePicker mp(pos, ttMove, ss, &continuationHistory, &lowPlyHistory, &mainHistory, depth);

    // Search the moves
    Move        bestMove = Move::none();
    SearchedList capturesSearched, quietsSearched;
    bool        ttCapture = pos.capture_stage(ttMove);

    Value bestValue  = -VALUE_INFINITE;
    int   moveCount  = 0;
    bool  failedHigh = false;

    // Loop through all legal moves
    while (true)
    {
        Move move = mp.next_move();
        if (!move.is_ok())
            break;

        moveCount++;

        // Check if we have reached the maximum depth
        if (depth == 0)
        {
            // Use the quiescence search
            bestValue = qsearch(pos, ss, alpha, beta);
            break;
        }

        // Increase the depth for the TT move (only if it's not a capture)
        Depth extDepth = depth;
        if (move == ttMove && !pos.capture_stage(move) && ttDepth > depth)
            extDepth = ttDepth;

        // Late move reductions (LMR)
        Depth reduction = 0;
        if (moveCount > 1 && depth >= 3)
        {
            int base = depth + moveCount / 2;
            reduction = 1 + base / 16;
        }

        // Search the move
        Value v;
        if (move == ttMove || PvNode)
            v = search<PV>(pos, ss + 1, alpha, beta, extDepth - 1, false);
        else
            v = search<NonPV>(pos, ss + 1, alpha, beta, extDepth - 1 - reduction, false);

        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        // Update the best move and the alpha value
        if (v > bestValue)
        {
            bestValue = v;
            bestMove = move;

            if (v > alpha)
            {
                alpha = v;
                if (!PvNode && v >= beta)
                {
                    failedHigh = true;
                    break;
                }
            }
        }
    }

    // If we failed high, we need to re-search the moves
    if (failedHigh)
    {
        // Re-search with the full depth
        for (Move move : moves)
        {
            if (move == bestMove)
                continue;

            Value v = search<NonPV>(pos, ss + 1, beta - 1, beta, depth - 1, false);
            if (v >= beta)
            {
                bestValue = v;
                break;
            }
        }
    }

    // Store the best move in the transposition table
    Value storeValue = value_to_tt(bestValue, ss->ply);
    tt.store(pos, ss, storeValue,
             bestValue >= beta ? BOUND_LOWER : PvNode && bestMove.is_ok() ? BOUND_EXACT : BOUND_UPPER,
             bestMove, depth, staticEval);

    return bestValue;
}

// Quiescence search: search only captures and promotions
template<NodeType nodeType>
Value Search::Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {
    constexpr bool PvNode = nodeType != NonPV;

    // Check if we have to stop the search
    if (threads.stop.load(std::memory_order_relaxed)
        || (is_mainthread() && limits.nodes && threads.nodes_searched() >= limits.nodes))
        return VALUE_ZERO;

    // Update the global nodes counter
    nodes.fetch_add(1, std::memory_order_relaxed);
    if (threadIdx == 0)
        main_manager()->nodes += 1;

    // Use the current position's side to move
    const Color us = pos.side_to_move();

    // Mate distance pruning
    alpha = std::max(alpha, value_mated_in(ss->ply + 1));
    beta  = std::min(beta, value_mate_in(ss->ply + 1));
    if (alpha >= beta)
        return alpha;

    // Transposition table lookup
    const bool ttHit = tt.probe(pos, ss, ttValue, ttMove, ttHit, ttDepth, ttBound, ttStaticEval);
    ss->ttMove       = ttMove;
    ss->ttHit        = ttHit;

    // At non-PV nodes we check for an early TT cutoff
    if (!PvNode && ttHit && ttDepth >= 0
        && ttBound != BOUND_NONE
        && (ttBound == BOUND_EXACT || (ttBound == BOUND_LOWER ? ttValue >= beta : ttValue <= alpha)))
        return value_from_tt(ttValue, ss->ply, pos.rule50_count());

    // Get the static evaluation
    Value staticEval = ss->staticEval;
    if (staticEval == VALUE_NONE)
    {
        auto [psqt, positional] = network[numaAccessToken].evaluate(pos, accumulatorStack);
        staticEval              = psqt + positional;

        // ----- किंग हंटर बोनस (क्वाइसेंस सर्च में भी) -----
        if (!PvNode) {
            Value kingBonus = (us == WHITE) ? king_hunter_score<WHITE>(pos) : king_hunter_score<BLACK>(pos);
            staticEval += kingBonus;
        }
        // -------------------------------------------------

        ss->staticEval = staticEval;

        if (!ttHit)
            tt.store(pos, ss, VALUE_NONE, BOUND_NONE, Move::none(), 0, staticEval);
    }

    // Stand pat
    if (staticEval >= beta)
        return staticEval;

    if (staticEval > alpha)
        alpha = staticEval;

    // Generate all captures and promotions
    MoveList<CAPTURE> moves(pos);
    if (moves.size() == 0)
        return staticEval;

    // Move ordering: sort captures by MVV-LVA
    std::sort(moves.begin(), moves.end(),
              [&](Move a, Move b) { return pos.see(a) > pos.see(b); });

    Value bestValue = staticEval;

    // Search all captures
    for (Move move : moves)
    {
        // Skip if the capture is not good (SEE < 0)
        if (!PvNode && pos.see(move) < 0)
            continue;

        // Make the move
        pos.do_move(move, ss + 1);

        // Search the capture
        Value v = -qsearch<NonPV>(pos, ss + 1, -beta, -alpha);

        // Undo the move
        pos.undo_move();

        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (v > bestValue)
        {
            bestValue = v;
            if (v > alpha)
            {
                alpha = v;
                if (v >= beta)
                    break;
            }
        }
    }

    // Store the result in the TT
    tt.store(pos, ss, value_to_tt(bestValue, ss->ply),
             bestValue >= beta ? BOUND_LOWER : BOUND_UPPER,
             Move::none(), 0, staticEval);

    return bestValue;
}

// Explicit template instantiations
template Value Search::Worker::search<PV>(Position&, Stack*, Value, Value, Depth, bool);
template Value Search::Worker::search<NonPV>(Position&, Stack*, Value, Value, Depth, bool);
template Value Search::Worker::qsearch<PV>(Position&, Stack*, Value, Value);
template Value Search::Worker::qsearch<NonPV>(Position&, Stack*, Value, Value);

}  // namespace Stockfish
