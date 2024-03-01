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

#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

#include "misc.h"
#include "movepick.h"
#include "position.h"
#include "syzygy/tbprobe.h"
#include "timeman.h"
#include "types.h"

namespace Stockfish {

// Different node types, used as a template parameter
enum NodeType {
    NonPV,
    PV,
    Root
};

class TranspositionTable;
class ThreadPool;
class OptionsMap;

namespace Search {

// Stack struct keeps track of the information we need to remember from nodes
// shallower and deeper in the tree during the search. Each search thread has
// its own array of Stack objects, indexed by the current ply.
struct Stack {
    Move*           pv;
    PieceToHistory* continuationHistory;
    int             ply;
    Move            currentMove;
    Move            excludedMove;
    Move            killers[2];
    Value           staticEval;
    int             statScore;
    int             moveCount;
    bool            inCheck;
    bool            ttPv;
    bool            ttHit;
    int             multipleExtensions;
    int             cutoffCnt;
};


// RootMove struct is used for moves at the root of the tree. For each root move
// we store a score and a PV (really a refutation in the case of moves which
// fail low). Score is normally set at -VALUE_INFINITE for all non-pv moves.
struct RootMove {

    explicit RootMove(Move m) :
        pv(1, m) {}
    bool extract_ponder_from_tt(const TranspositionTable& tt, Position& pos);
    bool operator==(const Move& m) const { return pv[0] == m; }
    // Sort in descending order
    bool operator<(const RootMove& m) const {
        return m.score != score ? m.score < score : m.previousScore < previousScore;
    }

    Value             score           = -VALUE_INFINITE;
    Value             previousScore   = -VALUE_INFINITE;
    Value             averageScore    = -VALUE_INFINITE;
    Value             uciScore        = -VALUE_INFINITE;
    bool              scoreLowerbound = false;
    bool              scoreUpperbound = false;
    int               selDepth        = 0;
    int               tbRank          = 0;
    Value             tbScore;
    std::vector<Move> pv;
};

using RootMoves = std::vector<RootMove>;


// LimitsType struct stores information sent by GUI about available time to
// search the current move, maximum depth/time, or if we are in analysis mode.
struct LimitsType {

    // Init explicitly due to broken value-initialization of non POD in MSVC
    LimitsType() {
        time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] = npmsec = movetime = TimePoint(0);
        movestogo = depth = mate = perft = infinite = 0;
        nodes                                       = 0;
    }

    bool use_time_management() const { return time[WHITE] || time[BLACK]; }

    std::vector<Move> searchmoves;
    TimePoint         time[COLOR_NB], inc[COLOR_NB], npmsec, movetime, startTime;
    int               movestogo, depth, mate, perft, infinite;
    uint64_t          nodes;
};


// The UCI stores the uci options, thread pool, and transposition table.
// This struct is used to easily forward data to the Search::Worker class.
struct SharedState {
    SharedState(const OptionsMap&   optionsMap,
                ThreadPool&         threadPool,
                TranspositionTable& transpositionTable) :
        options(optionsMap),
        threads(threadPool),
        tt(transpositionTable) {}

    const OptionsMap&   options;
    ThreadPool&         threads;
    TranspositionTable& tt;
};

class Worker;

// Null Object Pattern, implement a common interface for the SearchManagers.
// A Null Object will be given to non-mainthread workers.
class ISearchManager {
   public:
    virtual ~ISearchManager() {}
    virtual void check_time(Search::Worker&) = 0;
};

// SearchManager manages the search from the main thread. It is responsible for
// keeping track of the time, and storing data strictly related to the main thread.
class SearchManager: public ISearchManager {
   public:
    void check_time(Search::Worker& worker) override;

    std::string pv(const Search::Worker&     worker,
                   const ThreadPool&         threads,
                   const TranspositionTable& tt,
                   Depth                     depth) const;

    Stockfish::TimeManagement tm;
    int                       callsCnt;
    std::atomic_bool          ponder;

    std::array<Value, 4> iterValue;
    double               previousTimeReduction;
    Value                bestPreviousScore;
    Value                bestPreviousAverageScore;
    bool                 stopOnPonderhit;

    size_t id;
};

class NullSearchManager: public ISearchManager {
   public:
    void check_time(Search::Worker&) override {}
};

// Search::Worker is the class that does the actual search.
// It is instantiated once per thread, and it is responsible for keeping track
// of the search history, and storing data required for the search.
class Worker {
   public:
    Worker(SharedState&, std::unique_ptr<ISearchManager>, size_t);

    // Called at instantiation to initialize Reductions tables
    // Reset histories, usually before a new game
    void clear();

    // Called when the program receives the UCI 'go' command.
    // It searches from the root position and outputs the "bestmove".
    void start_searching();

    bool is_mainthread() const { return thread_idx == 0; }

    // Public because they need to be updatable by the stats
    CounterMoveHistory    counterMoves;
    ButterflyHistory      mainHistory;
    CapturePieceToHistory captureHistory;
    ContinuationHistory   continuationHistory[2][2];
    PawnHistory           pawnHistory;
    CorrectionHistory     correctionHistory;

   private:
    void iterative_deepening();

    // Main search function for both PV and non-PV nodes
    template<NodeType nodeType>
    Value search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode);

    // Quiescence search function, which is called by the main search
    template<NodeType nodeType>
    Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth = 0);

    Depth reduction(bool i, Depth d, int mn, int delta);

    // Get a pointer to the search manager, only allowed to be called by the
    // main thread.
    SearchManager* main_manager() const {
        assert(thread_idx == 0);
        return static_cast<SearchManager*>(manager.get());
    }

    std::array<std::array<uint64_t, SQUARE_NB>, SQUARE_NB> effort;

    LimitsType limits;

    size_t                pvIdx, pvLast;
    std::atomic<uint64_t> nodes, tbHits, bestMoveChanges;
    int                   selDepth, nmpMinPly;

    Value optimism[COLOR_NB];

    Position  rootPos;
    StateInfo rootState;
    RootMoves rootMoves;
    Depth     rootDepth, completedDepth;
    Value     rootDelta;

    size_t thread_idx;

    // Reductions lookup table initialized at startup
    std::array<int, MAX_MOVES> reductions;  // [depth or moveNumber]

    // The main thread has a SearchManager, the others have a NullSearchManager
    std::unique_ptr<ISearchManager> manager;

    Tablebases::Config tbConfig;

    const OptionsMap&   options;
    ThreadPool&         threads;
    TranspositionTable& tt;

    friend class Stockfish::ThreadPool;
    friend class SearchManager;
};


}  // namespace Search

}  // namespace Stockfish

#endif  // #ifndef SEARCH_H_INCLUDED
