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

#include "thread.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <memory>
#include <unordered_map>
#include <utility>
#include <array>

#include "misc.h"
#include "movegen.h"
#include "search.h"
#include "syzygy/tbprobe.h"
#include "timeman.h"
#include "tt.h"
#include "types.h"
#include "ucioption.h"

namespace Stockfish {

// Constructor launches the thread and waits until it goes to sleep
// in idle_loop(). Note that 'searching' and 'exit' should be already set.
Thread::Thread(Search::SharedState&                    sharedState,
               std::unique_ptr<Search::ISearchManager> sm,
               size_t                                  n) :
    worker(std::make_unique<Search::Worker>(sharedState, std::move(sm), n)),
    idx(n),
    nthreads(sharedState.options["Threads"]),
    stdThread(&Thread::idle_loop, this) {

    wait_for_search_finished();
}


// Destructor wakes up the thread in idle_loop() and waits
// for its termination. Thread should be already waiting.
Thread::~Thread() {

    assert(!searching);

    exit = true;
    start_searching();
    stdThread.join();
}

// Wakes up the thread that will start the search
void Thread::start_searching() {
    mutex.lock();
    searching = true;
    mutex.unlock();   // Unlock before notifying saves a few CPU-cycles
    cv.notify_one();  // Wake up the thread in idle_loop()
}


// Blocks on the condition variable
// until the thread has finished searching.
void Thread::wait_for_search_finished() {

    std::unique_lock<std::mutex> lk(mutex);
    cv.wait(lk, [&] { return !searching; });
}


// Thread gets parked here, blocked on the
// condition variable, when it has no work to do.

void Thread::idle_loop() {

    // If OS already scheduled us on a different group than 0 then don't overwrite
    // the choice, eventually we are one of many one-threaded processes running on
    // some Windows NUMA hardware, for instance in fishtest. To make it simple,
    // just check if running threads are below a threshold, in this case, all this
    // NUMA machinery is not needed.
    if (nthreads > 8)
        WinProcGroup::bindThisThread(idx);

    while (true)
    {
        std::unique_lock<std::mutex> lk(mutex);
        searching = false;
        cv.notify_one();  // Wake up anyone waiting for search finished
        cv.wait(lk, [&] { return searching; });

        if (exit)
            return;

        lk.unlock();

        worker->start_searching();
    }
}

// Creates/destroys threads to match the requested number.
// Created and launched threads will immediately go to sleep in idle_loop.
// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::set(Search::SharedState sharedState) {

    if (threads.size() > 0)  // destroy any existing thread(s)
    {
        main_thread()->wait_for_search_finished();

        while (threads.size() > 0)
            delete threads.back(), threads.pop_back();
    }

    const size_t requested = sharedState.options["Threads"];

    if (requested > 0)  // create new thread(s)
    {
        threads.push_back(new Thread(
          sharedState, std::unique_ptr<Search::ISearchManager>(new Search::SearchManager()), 0));


        while (threads.size() < requested)
            threads.push_back(new Thread(
              sharedState, std::unique_ptr<Search::ISearchManager>(new Search::NullSearchManager()),
              threads.size()));
        clear();

        main_thread()->wait_for_search_finished();

        // Reallocate the hash with the new threadpool size
        sharedState.tt.resize(sharedState.options["Hash"], requested);
    }
}


// Sets threadPool data to initial values
void ThreadPool::clear() {

    for (Thread* th : threads)
        th->worker->clear();

    main_manager()->callsCnt                 = 0;
    main_manager()->bestPreviousScore        = VALUE_INFINITE;
    main_manager()->bestPreviousAverageScore = VALUE_INFINITE;
    main_manager()->previousTimeReduction    = 1.0;
    main_manager()->tm.clear();
}


// Wakes up main thread waiting in idle_loop() and
// returns immediately. Main thread will wake up other threads and start the search.
void ThreadPool::start_thinking(const OptionsMap&  options,
                                Position&          pos,
                                StateListPtr&      states,
                                Search::LimitsType limits,
                                bool               ponderMode) {

    main_thread()->wait_for_search_finished();

    main_manager()->stopOnPonderhit = stop = abortedSearch = false;
    main_manager()->ponder                                 = ponderMode;

    increaseDepth = true;

    Search::RootMoves rootMoves;

    for (const auto& m : MoveList<LEGAL>(pos))
        if (limits.searchmoves.empty()
            || std::count(limits.searchmoves.begin(), limits.searchmoves.end(), m))
            rootMoves.emplace_back(m);

    Tablebases::Config tbConfig = Tablebases::rank_root_moves(options, pos, rootMoves);

    // After ownership transfer 'states' becomes empty, so if we stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() || setupStates.get());

    if (states.get())
        setupStates = std::move(states);  // Ownership transfer, states is now empty

    // We use Position::set() to set root position across threads. But there are
    // some StateInfo fields (previous, pliesFromNull, capturedPiece) that cannot
    // be deduced from a fen string, so set() clears them and they are set from
    // setupStates->back() later. The rootState is per thread, earlier states are shared
    // since they are read-only.
    for (Thread* th : threads)
    {
        th->worker->limits = limits;
        th->worker->nodes = th->worker->tbHits = th->worker->nmpMinPly =
          th->worker->bestMoveChanges          = 0;
        th->worker->rootDepth = th->worker->completedDepth = 0;
        th->worker->rootMoves                              = rootMoves;
        th->worker->rootPos.set(pos.fen(), pos.is_chess960(), &th->worker->rootState);
        th->worker->rootState = setupStates->back();
        th->worker->tbConfig  = tbConfig;
        th->worker->effort    = {};
    }

    main_thread()->start_searching();
}

Thread* ThreadPool::get_best_thread() const {

    Thread* bestThread = threads.front();
    Value   minScore   = VALUE_NONE;

    std::unordered_map<Move, int64_t, Move::MoveHash> votes(
      2 * std::min(size(), bestThread->worker->rootMoves.size()));

    // Find the minimum score of all threads
    for (Thread* th : threads)
        minScore = std::min(minScore, th->worker->rootMoves[0].score);

    // Vote according to score and depth, and select the best thread
    auto thread_voting_value = [minScore](Thread* th) {
        return (th->worker->rootMoves[0].score - minScore + 14) * int(th->worker->completedDepth);
    };

    for (Thread* th : threads)
        votes[th->worker->rootMoves[0].pv[0]] += thread_voting_value(th);

    for (Thread* th : threads)
    {
        const auto bestThreadScore = bestThread->worker->rootMoves[0].score;
        const auto newThreadScore  = th->worker->rootMoves[0].score;

        const auto& bestThreadPV = bestThread->worker->rootMoves[0].pv;
        const auto& newThreadPV  = th->worker->rootMoves[0].pv;

        const auto bestThreadMoveVote = votes[bestThreadPV[0]];
        const auto newThreadMoveVote  = votes[newThreadPV[0]];

        const bool bestThreadInProvenWin = bestThreadScore >= VALUE_TB_WIN_IN_MAX_PLY;
        const bool newThreadInProvenWin  = newThreadScore >= VALUE_TB_WIN_IN_MAX_PLY;

        const bool bestThreadInProvenLoss =
          bestThreadScore != -VALUE_INFINITE && bestThreadScore <= VALUE_TB_LOSS_IN_MAX_PLY;
        const bool newThreadInProvenLoss =
          newThreadScore != -VALUE_INFINITE && newThreadScore <= VALUE_TB_LOSS_IN_MAX_PLY;

        // Note that we make sure not to pick a thread with truncated-PV for better viewer experience.
        const bool betterVotingValue =
          thread_voting_value(th) * int(newThreadPV.size() > 2)
          > thread_voting_value(bestThread) * int(bestThreadPV.size() > 2);

        if (bestThreadInProvenWin)
        {
            // Make sure we pick the shortest mate / TB conversion
            if (newThreadScore > bestThreadScore)
                bestThread = th;
        }
        else if (bestThreadInProvenLoss)
        {
            // Make sure we pick the shortest mated / TB conversion
            if (newThreadInProvenLoss && newThreadScore < bestThreadScore)
                bestThread = th;
        }
        else if (newThreadInProvenWin || newThreadInProvenLoss
                 || (newThreadScore > VALUE_TB_LOSS_IN_MAX_PLY
                     && (newThreadMoveVote > bestThreadMoveVote
                         || (newThreadMoveVote == bestThreadMoveVote && betterVotingValue))))
            bestThread = th;
    }

    return bestThread;
}


// Start non-main threads
// Will be invoked by main thread after it has started searching
void ThreadPool::start_searching() {

    for (Thread* th : threads)
        if (th != threads.front())
            th->start_searching();
}


// Wait for non-main threads

void ThreadPool::wait_for_search_finished() const {

    for (Thread* th : threads)
        if (th != threads.front())
            th->wait_for_search_finished();
}

}  // namespace Stockfish
