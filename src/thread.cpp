/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad

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

// Thread constructor makes some init and launches the thread that will go to
// sleep in idle_loop().

Thread::Thread() {

  searching = true; // Avoid a race with start_thinking()
  exit = resetCalls = false;
  maxPly = callsCnt = 0;
  history.clear();
  counterMoves.clear();
  idx = Threads.size(); // Starts from 0
  std::thread::operator=(std::thread(&Thread::idle_loop, this));
}


// Thread destructor waits for thread termination before deleting

Thread::~Thread() {

  mutex.lock();
  exit = true; // Search must be already finished
  mutex.unlock();

  notify_one();
  std::thread::join(); // Wait for thread termination
}


// Thread::join() waits for the thread to finish searching
void Thread::join() {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return !searching; });
}


// Thread::notify_one() wakes up the thread when there is some work to do

void Thread::notify_one() {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.notify_one();
}


// Thread::wait() set the thread to sleep until 'condition' turns true

void Thread::wait(std::atomic_bool& condition) {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return bool(condition); });
}


// Thread::idle_loop() is where the thread is parked when it has no work to do

void Thread::idle_loop() {

  while (!exit)
  {
      std::unique_lock<Mutex> lk(mutex);

      searching = false;

      while (!searching && !exit)
      {
          sleepCondition.notify_one(); // Wake up main thread if needed
          sleepCondition.wait(lk);
      }

      lk.unlock();

      if (!exit && searching)
          search();
  }
}


// ThreadPool::init() is called at startup to create and launch requested threads,
// that will go immediately to sleep. We cannot use a constructor because Threads
// is a static object and we need a fully initialized engine at this point due to
// allocation of Endgames in the Thread constructor.

void ThreadPool::init() {

  push_back(new MainThread);
  read_uci_options();
}


// ThreadPool::exit() terminates the threads before the program exits. Cannot be
// done in destructor because threads must be terminated before freeing us.

void ThreadPool::exit() {

  for (Thread* th : *this)
      delete th;

  clear(); // Get rid of stale pointers
}


// ThreadPool::read_uci_options() updates internal threads parameters from the
// corresponding UCI options and creates/destroys threads to match the requested
// number. Thread objects are dynamically allocated to avoid creating all possible
// threads in advance (which include pawns and material tables), even if only a
// few are to be used.

void ThreadPool::read_uci_options() {

  size_t requested  = Options["Threads"];

  assert(requested > 0);

  while (size() < requested)
      push_back(new Thread);

  while (size() > requested)
  {
      delete back();
      pop_back();
  }
}


// ThreadPool::nodes_searched() returns the number of nodes searched

int64_t ThreadPool::nodes_searched() {

  int64_t nodes = 0;
  for (Thread *th : *this)
      nodes += th->rootPos.nodes_searched();
  return nodes;
}


// ThreadPool::start_thinking() wakes up the main thread sleeping in
// MainThread::idle_loop() and starts a new search, then returns immediately.

void ThreadPool::start_thinking(const Position& pos, const LimitsType& limits,
                                StateStackPtr& states) {
  for (Thread* th : Threads)
      th->join();

  Signals.stopOnPonderhit = Signals.firstRootMove = false;
  Signals.stop = Signals.failedLowAtRoot = false;

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

  main()->searching = true;
  main()->notify_one(); // Wake up main thread: 'searching' must be already set
}
