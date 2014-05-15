/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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
#include "ucioption.h"

using namespace Search;

ThreadPool Threads; // Global object

extern void check_time();

namespace {

 // start_routine() is the C function which is called when a new thread
 // is launched. It is a wrapper to the virtual function idle_loop().

 extern "C" { long start_routine(ThreadBase* th) { th->idle_loop(); return 0; } }


 // Helpers to launch a thread after creation and joining before delete. Must be
 // outside Thread c'tor and d'tor because the object will be fully initialized
 // when start_routine (and hence virtual idle_loop) is called and when joining.

 template<typename T> T* new_thread() {
   T* th = new T();
   thread_create(th->handle, start_routine, th); // Will go to sleep
   return th;
 }

 void delete_thread(ThreadBase* th) {
   th->exit = true; // Search must be already finished
   th->notify_one();
   thread_join(th->handle); // Wait for thread termination
   delete th;
 }

}


// notify_one() wakes up the thread when there is some work to do

void ThreadBase::notify_one() {

  mutex.lock();
  sleepCondition.notify_one();
  mutex.unlock();
}


// wait_for() set the thread to sleep until condition 'b' turns true

void ThreadBase::wait_for(volatile const bool& b) {

  mutex.lock();
  while (!b) sleepCondition.wait(mutex);
  mutex.unlock();
}


// Thread c'tor just inits data and does not launch any execution thread.
// Such a thread will only be started when c'tor returns.

Thread::Thread() /* : splitPoints() */ { // Value-initialization bug in MSVC

  searching = false;
  maxPly = splitPointsSize = 0;
  activeSplitPoint = NULL;
  activePosition = NULL;
  idx = Threads.size(); // Starts from 0
}


// cutoff_occurred() checks whether a beta cutoff has occurred in the
// current active split point, or in some ancestor of the split point.

bool Thread::cutoff_occurred() const {

  for (SplitPoint* sp = activeSplitPoint; sp; sp = sp->parentSplitPoint)
      if (sp->cutoff)
          return true;

  return false;
}


// Thread::available_to() checks whether the thread is available to help the
// thread 'master' at a split point. An obvious requirement is that thread must
// be idle. With more than two threads, this is not sufficient: If the thread is
// the master of some split point, it is only available as a slave to the slaves
// which are busy searching the split point at the top of slave's split point
// stack (the "helpful master concept" in YBWC terminology).

bool Thread::available_to(const Thread* master) const {

  if (searching)
      return false;

  // Make a local copy to be sure it doesn't become zero under our feet while
  // testing next condition and so leading to an out of bounds access.
  int size = splitPointsSize;

  // No split points means that the thread is available as a slave for any
  // other thread otherwise apply the "helpful master" concept if possible.
  return !size || splitPoints[size - 1].slavesMask.test(master->idx);
}


// TimerThread::idle_loop() is where the timer thread waits msec milliseconds
// and then calls check_time(). If msec is 0 thread sleeps until it's woken up.

void TimerThread::idle_loop() {

  while (!exit)
  {
      mutex.lock();

      if (!exit)
          sleepCondition.wait_for(mutex, run ? Resolution : INT_MAX);

      mutex.unlock();

      if (run)
          check_time();
  }
}


// MainThread::idle_loop() is where the main thread is parked waiting to be started
// when there is a new search. The main thread will launch all the slave threads.

void MainThread::idle_loop() {

  while (true)
  {
      mutex.lock();

      thinking = false;

      while (!thinking && !exit)
      {
          Threads.sleepCondition.notify_one(); // Wake up the UI thread if needed
          sleepCondition.wait(mutex);
      }

      mutex.unlock();

      if (exit)
          return;

      searching = true;

      Search::think();

      assert(searching);

      searching = false;
  }
}


// init() is called at startup to create and launch requested threads, that will
// go immediately to sleep. We cannot use a c'tor because Threads is a static
// object and we need a fully initialized engine at this point due to allocation
// of Endgames in Thread c'tor.

void ThreadPool::init() {

  timer = new_thread<TimerThread>();
  push_back(new_thread<MainThread>());
  read_uci_options();
}


// exit() cleanly terminates the threads before the program exits. Cannot be done in
// d'tor because we have to terminate the threads before to free ThreadPool object.

void ThreadPool::exit() {

  delete_thread(timer); // As first because check_time() accesses threads data

  for (iterator it = begin(); it != end(); ++it)
      delete_thread(*it);
}


// read_uci_options() updates internal threads parameters from the corresponding
// UCI options and creates/destroys threads to match the requested number. Thread
// objects are dynamically allocated to avoid creating all possible threads
// in advance (which include pawns and material tables), even if only a few
// are to be used.

void ThreadPool::read_uci_options() {

  minimumSplitDepth = Options["Min Split Depth"] * ONE_PLY;
  size_t requested  = Options["Threads"];

  assert(requested > 0);

  // If zero (default) then set best minimum split depth automatically
  if (!minimumSplitDepth)
      minimumSplitDepth = requested < 8 ? 4 * ONE_PLY : 7 * ONE_PLY;

  while (size() < requested)
      push_back(new_thread<Thread>());

  while (size() > requested)
  {
      delete_thread(back());
      pop_back();
  }
}


// available_slave() tries to find an idle thread which is available as a slave
// for the thread 'master'.

Thread* ThreadPool::available_slave(const Thread* master) const {

  for (const_iterator it = begin(); it != end(); ++it)
      if ((*it)->available_to(master))
          return *it;

  return NULL;
}


// split() does the actual work of distributing the work at a node between
// several available threads. If it does not succeed in splitting the node
// (because no idle threads are available), the function immediately returns.
// If splitting is possible, a SplitPoint object is initialized with all the
// data that must be copied to the helper threads and then helper threads are
// told that they have been assigned work. This will cause them to instantly
// leave their idle loops and call search(). When all threads have returned from
// search() then split() returns.

template <bool Fake>
void Thread::split(Position& pos, const Stack* ss, Value alpha, Value beta, Value* bestValue,
                   Move* bestMove, Depth depth, int moveCount,
                   MovePicker* movePicker, int nodeType, bool cutNode) {

  assert(pos.pos_is_ok());
  assert(-VALUE_INFINITE < *bestValue && *bestValue <= alpha && alpha < beta && beta <= VALUE_INFINITE);
  assert(depth >= Threads.minimumSplitDepth);
  assert(searching);
  assert(splitPointsSize < MAX_SPLITPOINTS_PER_THREAD);

  // Pick the next available split point from the split point stack
  SplitPoint& sp = splitPoints[splitPointsSize];

  sp.masterThread = this;
  sp.parentSplitPoint = activeSplitPoint;
  sp.slavesMask = 0, sp.slavesMask.set(idx);
  sp.depth = depth;
  sp.bestValue = *bestValue;
  sp.bestMove = *bestMove;
  sp.alpha = alpha;
  sp.beta = beta;
  sp.nodeType = nodeType;
  sp.cutNode = cutNode;
  sp.movePicker = movePicker;
  sp.moveCount = moveCount;
  sp.pos = &pos;
  sp.nodes = 0;
  sp.cutoff = false;
  sp.ss = ss;

  // Try to allocate available threads and ask them to start searching setting
  // 'searching' flag. This must be done under lock protection to avoid concurrent
  // allocation of the same slave by another master.
  Threads.mutex.lock();
  sp.mutex.lock();

  sp.allSlavesSearching = true; // Must be set under lock protection
  ++splitPointsSize;
  activeSplitPoint = &sp;
  activePosition = NULL;

  if (!Fake)
      for (Thread* slave; (slave = Threads.available_slave(this)) != NULL; )
      {
          sp.slavesMask.set(slave->idx);
          slave->activeSplitPoint = &sp;
          slave->searching = true; // Slave leaves idle_loop()
          slave->notify_one(); // Could be sleeping
      }

  // Everything is set up. The master thread enters the idle loop, from which
  // it will instantly launch a search, because its 'searching' flag is set.
  // The thread will return from the idle loop when all slaves have finished
  // their work at this split point.
  sp.mutex.unlock();
  Threads.mutex.unlock();

  Thread::idle_loop(); // Force a call to base class idle_loop()

  // In the helpful master concept, a master can help only a sub-tree of its
  // split point and because everything is finished here, it's not possible
  // for the master to be booked.
  assert(!searching);
  assert(!activePosition);

  // We have returned from the idle loop, which means that all threads are
  // finished. Note that setting 'searching' and decreasing splitPointsSize is
  // done under lock protection to avoid a race with Thread::available_to().
  Threads.mutex.lock();
  sp.mutex.lock();

  searching = true;
  --splitPointsSize;
  activeSplitPoint = sp.parentSplitPoint;
  activePosition = &pos;
  pos.set_nodes_searched(pos.nodes_searched() + sp.nodes);
  *bestMove = sp.bestMove;
  *bestValue = sp.bestValue;

  sp.mutex.unlock();
  Threads.mutex.unlock();
}

// Explicit template instantiations
template void Thread::split<false>(Position&, const Stack*, Value, Value, Value*, Move*, Depth, int, MovePicker*, int, bool);
template void Thread::split< true>(Position&, const Stack*, Value, Value, Value*, Move*, Depth, int, MovePicker*, int, bool);


// wait_for_think_finished() waits for main thread to go to sleep then returns

void ThreadPool::wait_for_think_finished() {

  MainThread* t = main();
  t->mutex.lock();
  while (t->thinking) sleepCondition.wait(t->mutex);
  t->mutex.unlock();
}


// start_thinking() wakes up the main thread sleeping in MainThread::idle_loop()
// so to start a new search, then returns immediately.

void ThreadPool::start_thinking(const Position& pos, const LimitsType& limits, StateStackPtr& states) {

  wait_for_think_finished();

  SearchTime = Time::now(); // As early as possible

  Signals.stopOnPonderhit = Signals.firstRootMove = false;
  Signals.stop = Signals.failedLowAtRoot = false;

  RootMoves.clear();
  RootPos = pos;
  Limits = limits;
  if (states.get()) // If we don't set a new position, preserve current state
  {
      SetupStates = states; // Ownership transfer here
      assert(!states.get());
  }

  for (MoveList<LEGAL> it(pos); *it; ++it)
      if (   limits.searchmoves.empty()
          || std::count(limits.searchmoves.begin(), limits.searchmoves.end(), *it))
          RootMoves.push_back(RootMove(*it));

  main()->thinking = true;
  main()->notify_one(); // Starts main thread
}
