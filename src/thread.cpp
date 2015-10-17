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

extern void check_time();

namespace {

 // Helpers to launch a thread after creation and joining before delete. Must be
 // outside Thread c'tor and d'tor because the object must be fully initialized
 // when start_routine (and hence virtual idle_loop) is called and when joining.

 template<typename T> T* new_thread() {
   std::thread* th = new T;
   *th = std::thread(&T::idle_loop, (T*)th); // Will go to sleep
   return (T*)th;
 }

 void delete_thread(ThreadBase* th) {

   th->mutex.lock();
   th->exit = true; // Search must be already finished
   th->mutex.unlock();

   th->notify_one();
   th->join(); // Wait for thread termination
   delete th;
 }

}


// ThreadBase::notify_one() wakes up the thread when there is some work to do

void ThreadBase::notify_one() {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.notify_one();
}


// ThreadBase::wait() set the thread to sleep until 'condition' turns true

void ThreadBase::wait(volatile const bool& condition) {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return condition; });
}


// ThreadBase::wait_while() set the thread to sleep until 'condition' turns false

void ThreadBase::wait_while(volatile const bool& condition) {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return !condition; });
}


// Thread c'tor makes some init but does not launch any execution thread that
// will be started only when c'tor returns.

Thread::Thread() /* : splitPoints() */ { // Initialization of non POD broken in MSVC

  searching = false;
  maxPly = 0;
  idx = Threads.size(); // Starts from 0
}


// TimerThread::idle_loop() is where the timer thread waits Resolution milliseconds
// and then calls check_time(). When not searching, thread sleeps until it's woken up.

void TimerThread::idle_loop() {

  while (!exit)
  {
      std::unique_lock<Mutex> lk(mutex);

      if (!exit)
          sleepCondition.wait_for(lk, std::chrono::milliseconds(run ? Resolution : INT_MAX));

      lk.unlock();

      if (!exit && run)
          check_time();
  }
}


// Thread::idle_loop() is where the thread is parked when it has no work to do

void Thread::idle_loop() {

  while (!exit)
  {
      std::unique_lock<Mutex> lk(mutex);

      while (!searching && !exit)
          sleepCondition.wait(lk);

      lk.unlock();

      if (!exit && searching)
          search();
  }
}


// MainThread::idle_loop() is where the main thread is parked waiting to be started
// when there is a new search. The main thread will launch all the slave threads.

void MainThread::idle_loop() {

  while (!exit)
  {
      std::unique_lock<Mutex> lk(mutex);

      thinking = false;

      while (!thinking && !exit)
      {
          sleepCondition.notify_one(); // Wake up the UI thread if needed
          sleepCondition.wait(lk);
      }

      lk.unlock();

      if (!exit)
          think();
  }
}


// MainThread::join() waits for main thread to finish thinking

void MainThread::join() {

  std::unique_lock<Mutex> lk(mutex);
  sleepCondition.wait(lk, [&]{ return !thinking; });
}


// ThreadPool::init() is called at startup to create and launch requested threads,
// that will go immediately to sleep. We cannot use a c'tor because Threads is a
// static object and we need a fully initialized engine at this point due to
// allocation of Endgames in Thread c'tor.

void ThreadPool::init() {

  timer = new_thread<TimerThread>();
  push_back(new_thread<MainThread>());
  read_uci_options();
}


// ThreadPool::exit() terminates the threads before the program exits. Cannot be
// done in d'tor because threads must be terminated before freeing us.

void ThreadPool::exit() {

  delete_thread(timer); // As first because check_time() accesses threads data
  timer = nullptr;

  for (Thread* th : *this)
      delete_thread(th);

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
      push_back(new_thread<Thread>());

  while (size() > requested)
  {
      delete_thread(back());
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
  main()->join();

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

  main()->thinking = true;
  main()->notify_one(); // Wake up main thread: 'thinking' must be already set
}
