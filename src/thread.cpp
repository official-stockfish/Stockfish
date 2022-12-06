/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

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

#include <cassert>

#include <algorithm> // For std::count
#include "movegen.h"
#include "search.h"
#include "thread.h"
#include "uci.h"
#include "syzygy/tbprobe.h"
#include "tt.h"

namespace Stockfish {

ThreadPool Threads; // Global object


/// Thread constructor launches the thread and waits until it goes to sleep
/// in idle_loop(). Note that 'searching' and 'exit' should be already set.

Thread::Thread(size_t n) : idx(n), stdThread(&Thread::idle_loop, this) {

  wait_for_search_finished();
}


/// Thread destructor wakes up the thread in idle_loop() and waits
/// for its termination. Thread should be already waiting.

Thread::~Thread() {

  assert(!searching);

  exit = true;
  start_searching();
  stdThread.join();
}


/// Thread::clear() reset histories, usually before a new game

void Thread::clear() {

  counterMoves.fill(MOVE_NONE);
  mainHistory.fill(0);
  captureHistory.fill(0);
  previousDepth = 0;

  for (bool inCheck : { false, true })
      for (StatsType c : { NoCaptures, Captures })
          for (auto& to : continuationHistory[inCheck][c])
              for (auto& h : to)
                  h->fill(-71);
}


/// Thread::start_searching() wakes up the thread that will start the search

void Thread::start_searching() {

  std::lock_guard<std::mutex> lk(mutex);
  searching = true;
  cv.notify_one(); // Wake up the thread in idle_loop()
}


/// Thread::wait_for_search_finished() blocks on the condition variable
/// until the thread has finished searching.

void Thread::wait_for_search_finished() {

  std::unique_lock<std::mutex> lk(mutex);
  cv.wait(lk, [&]{ return !searching; });
}


/// Thread::idle_loop() is where the thread is parked, blocked on the
/// condition variable, when it has no work to do.

void Thread::idle_loop() {

  // If OS already scheduled us on a different group than 0 then don't overwrite
  // the choice, eventually we are one of many one-threaded processes running on
  // some Windows NUMA hardware, for instance in fishtest. To make it simple,
  // just check if running threads are below a threshold, in this case all this
  // NUMA machinery is not needed.
  if (Options["Threads"] > 8)
      WinProcGroup::bindThisThread(idx);

  while (true)
  {
      std::unique_lock<std::mutex> lk(mutex);
      searching = false;
      cv.notify_one(); // Wake up anyone waiting for search finished
      cv.wait(lk, [&]{ return searching; });

      if (exit)
          return;

      lk.unlock();

      search();
  }
}

/// ThreadPool::set() creates/destroys threads to match the requested number.
/// Created and launched threads will immediately go to sleep in idle_loop.
/// Upon resizing, threads are recreated to allow for binding if necessary.

void ThreadPool::set(size_t requested) {

  if (size() > 0)   // destroy any existing thread(s)
  {
      main()->wait_for_search_finished();

      while (size() > 0)
          delete back(), pop_back();
  }

  if (requested > 0)   // create new thread(s)
  {
      push_back(new MainThread(0));

      while (size() < requested)
          push_back(new Thread(size()));
      clear();

      // Reallocate the hash with the new threadpool size
      TT.resize(size_t(Options["Hash"]));

      // Init thread number dependent search params.
      Search::init();
  }
}


/// ThreadPool::clear() sets threadPool data to initial values

void ThreadPool::clear() {

  for (Thread* th : *this)
      th->clear();

  main()->callsCnt = 0;
  main()->bestPreviousScore = VALUE_INFINITE;
  main()->bestPreviousAverageScore = VALUE_INFINITE;
  main()->previousTimeReduction = 1.0;
}


/// ThreadPool::start_thinking() wakes up main thread waiting in idle_loop() and
/// returns immediately. Main thread will wake up other threads and start the search.

void ThreadPool::start_thinking(Position& pos, StateListPtr& states,
                                const Search::LimitsType& limits, bool ponderMode) {

  main()->wait_for_search_finished();

  main()->stopOnPonderhit = stop = false;
  increaseDepth = true;
  main()->ponder = ponderMode;
  Search::Limits = limits;
  Search::RootMoves rootMoves;

  for (const auto& m : MoveList<LEGAL>(pos))
      if (   limits.searchmoves.empty()
          || std::count(limits.searchmoves.begin(), limits.searchmoves.end(), m))
          rootMoves.emplace_back(m);

  if (!rootMoves.empty())
      Tablebases::rank_root_moves(pos, rootMoves);

  // After ownership transfer 'states' becomes empty, so if we stop the search
  // and call 'go' again without setting a new position states.get() == NULL.
  assert(states.get() || setupStates.get());

  if (states.get())
      setupStates = std::move(states); // Ownership transfer, states is now empty

  // We use Position::set() to set root position across threads. But there are
  // some StateInfo fields (previous, pliesFromNull, capturedPiece) that cannot
  // be deduced from a fen string, so set() clears them and they are set from
  // setupStates->back() later. The rootState is per thread, earlier states are shared
  // since they are read-only.
  for (Thread* th : *this)
  {
      th->nodes = th->tbHits = th->nmpMinPly = th->bestMoveChanges = 0;
      th->rootDepth = th->completedDepth = 0;
      th->rootMoves = rootMoves;
      th->rootPos.set(pos.fen(), pos.is_chess960(), &th->rootState, th);
      th->rootState = setupStates->back();
  }

  main()->start_searching();
}

Thread* ThreadPool::get_best_thread() const {

    Thread* bestThread = front();
    std::map<Move, int64_t> votes;
    Value minScore = VALUE_NONE;

    // Find minimum score of all threads
    for (Thread* th: *this)
        minScore = std::min(minScore, th->rootMoves[0].score);

    // Vote according to score and depth, and select the best thread
    auto thread_value = [minScore](Thread* th) {
            return (th->rootMoves[0].score - minScore + 14) * int(th->completedDepth);
        };

    for (Thread* th : *this)
        votes[th->rootMoves[0].pv[0]] += thread_value(th);

    for (Thread* th : *this)
        if (abs(bestThread->rootMoves[0].score) >= VALUE_TB_WIN_IN_MAX_PLY)
        {
            // Make sure we pick the shortest mate / TB conversion or stave off mate the longest
            if (th->rootMoves[0].score > bestThread->rootMoves[0].score)
                bestThread = th;
        }
        else if (   th->rootMoves[0].score >= VALUE_TB_WIN_IN_MAX_PLY
                 || (   th->rootMoves[0].score > VALUE_TB_LOSS_IN_MAX_PLY
                     && (   votes[th->rootMoves[0].pv[0]] > votes[bestThread->rootMoves[0].pv[0]]
                         || (   votes[th->rootMoves[0].pv[0]] == votes[bestThread->rootMoves[0].pv[0]]
                             && thread_value(th) > thread_value(bestThread)))))
            bestThread = th;

    return bestThread;
}


/// Start non-main threads

void ThreadPool::start_searching() {

    for (Thread* th : *this)
        if (th != front())
            th->start_searching();
}


/// Wait for non-main threads

void ThreadPool::wait_for_search_finished() const {

    for (Thread* th : *this)
        if (th != front())
            th->wait_for_search_finished();
}

} // namespace Stockfish
