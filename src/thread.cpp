/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include <algorithm> // For std::count
#include <cassert>

#include "movegen.h"
#include "search.h"
#include "thread.h"
#include "uci.h"

using namespace Search;

ThreadPool Threads; // Global object

/// Thread constructor launch the thread and then wait until it goes to sleep
/// in idle_loop().

Thread::Thread() {

  resetCalls = exit = false;
  maxPly = callsCnt = 0;
  history.clear();
  counterMoves.clear();
  idx = Threads.size(); // Start from 0

  std::unique_lock<Mutex> lk(mutex);
  searching = true;
  nativeThread = std::thread(&Thread::idle_loop, this);
  sleepCondition.wait(lk, [&]{ return !searching; });
}


/// Thread destructor wait for thread termination before returning

Thread::~Thread() {

  mutex.lock();
  exit = true;
  sleepCondition.notify_one();
  mutex.unlock();
  nativeThread.join();
}


/// Thread::wait_for_search_finished() wait on sleep condition until not searching

void Thread::wait_for_search_finished() {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return !searching; });
}


/// Thread::wait() wait on sleep condition until condition is true

void Thread::wait(std::atomic_bool& condition) {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return bool(condition); });
}


/// Thread::start_searching() wake up the thread that will start the search

void Thread::start_searching(bool resume) {

  std::unique_lock<Mutex> lk(mutex);

  if (!resume)
      searching = true;

  sleepCondition.notify_one();
}


/// Thread::idle_loop() is where the thread is parked when it has no work to do

void Thread::idle_loop() {

  while (!exit)
  {
      std::unique_lock<Mutex> lk(mutex);

      searching = false;

      while (!searching && !exit)
      {
          sleepCondition.notify_one(); // Wake up any waiting thread
          sleepCondition.wait(lk);
      }

      lk.unlock();

      if (!exit)
          search();
  }
}


/// ThreadPool::init() create and launch requested threads, that will go
/// immediately to sleep. We cannot use a constructor because Threads is a
/// static object and we need a fully initialized engine at this point due to
/// allocation of Endgames in the Thread constructor.

void ThreadPool::init() {

  push_back(new MainThread);
  read_uci_options();
}


/// ThreadPool::exit() terminate threads before the program exits. Cannot be
/// done in destructor because threads must be terminated before deleting any
/// static objects, so while still in main().

void ThreadPool::exit() {

  while (size())
      delete back(), pop_back();
}


/// ThreadPool::read_uci_options() updates internal threads parameters from the
/// corresponding UCI options and creates/destroys threads to match requested
/// number. Thread objects are dynamically allocated.

void ThreadPool::read_uci_options() {

  size_t requested = Options["Threads"];

  assert(requested > 0);

  while (size() < requested)
      push_back(new Thread);

  while (size() > requested)
      delete back(), pop_back();
}


/// ThreadPool::nodes_searched() return the number of nodes searched

int64_t ThreadPool::nodes_searched() {

  int64_t nodes = 0;
  for (Thread* th : *this)
      nodes += th->rootPos.nodes_searched();
  return nodes;
}


/// ThreadPool::start_thinking() wake up the main thread sleeping in idle_loop()
/// and start a new search, then return immediately.

void ThreadPool::start_thinking(const Position& pos, const LimitsType& limits,
                                StateStackPtr& states) {

  main()->wait_for_search_finished();

  Signals.stopOnPonderhit = Signals.stop = false;

  main()->rootMoves.clear();
  main()->rootPos = pos;
  Limits = limits;
  if (states.get()) // If we don't set a new position, preserve current state
  {
      SetupStates = std::move(states); // Ownership transfer here
      assert(!states.get());
  }

  for (const auto& m : MoveList<LEGAL>(pos))
      if (   limits.searchmoves.empty()
          || std::count(limits.searchmoves.begin(), limits.searchmoves.end(), m))
          main()->rootMoves.push_back(RootMove(m));

  main()->start_searching();
}
