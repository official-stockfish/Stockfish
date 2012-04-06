/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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
#include <iostream>

#include "movegen.h"
#include "search.h"
#include "thread.h"
#include "ucioption.h"

using namespace Search;

ThreadsManager Threads; // Global object

namespace { extern "C" {

 // start_routine() is the C function which is called when a new thread
 // is launched. It is a wrapper to member function pointed by start_fn.

 long start_routine(Thread* th) { (th->*(th->start_fn))(); return 0; }

} }


// Thread c'tor starts a newly-created thread of execution that will call
// the idle loop function pointed by start_fn going immediately to sleep.

Thread::Thread(Fn fn) {

  is_searching = do_exit = false;
  maxPly = splitPointsCnt = 0;
  curSplitPoint = NULL;
  start_fn = fn;
  idx = Threads.size();

  do_sleep = (fn != &Thread::main_loop); // Avoid a race with start_searching()

  lock_init(sleepLock);
  cond_init(sleepCond);

  for (int j = 0; j < MAX_SPLITPOINTS_PER_THREAD; j++)
      lock_init(splitPoints[j].lock);

  if (!thread_create(handle, start_routine, this))
  {
      std::cerr << "Failed to create thread number " << idx << std::endl;
      ::exit(EXIT_FAILURE);
  }
}


// Thread d'tor waits for thread termination before to return.

Thread::~Thread() {

  assert(do_sleep);

  do_exit = true; // Search must be already finished
  wake_up();

  thread_join(handle); // Wait for thread termination

  lock_destroy(sleepLock);
  cond_destroy(sleepCond);

  for (int j = 0; j < MAX_SPLITPOINTS_PER_THREAD; j++)
      lock_destroy(splitPoints[j].lock);
}


// Thread::timer_loop() is where the timer thread waits maxPly milliseconds and
// then calls check_time(). If maxPly is 0 thread sleeps until is woken up.
extern void check_time();

void Thread::timer_loop() {

  while (!do_exit)
  {
      lock_grab(sleepLock);
      timed_wait(sleepCond, sleepLock, maxPly ? maxPly : INT_MAX);
      lock_release(sleepLock);
      check_time();
  }
}


// Thread::main_loop() is where the main thread is parked waiting to be started
// when there is a new search. Main thread will launch all the slave threads.

void Thread::main_loop() {

  while (true)
  {
      lock_grab(sleepLock);

      do_sleep = true; // Always return to sleep after a search
      is_searching = false;

      while (do_sleep && !do_exit)
      {
          cond_signal(Threads.sleepCond); // Wake up UI thread if needed
          cond_wait(sleepCond, sleepLock);
      }

      lock_release(sleepLock);

      if (do_exit)
          return;

      is_searching = true;

      Search::think();
  }
}


// Thread::wake_up() wakes up the thread, normally at the beginning of the search
// or, if "sleeping threads" is used at split time.

void Thread::wake_up() {

  lock_grab(sleepLock);
  cond_signal(sleepCond);
  lock_release(sleepLock);
}


// Thread::wait_for_stop_or_ponderhit() is called when the maximum depth is
// reached while the program is pondering. The point is to work around a wrinkle
// in the UCI protocol: When pondering, the engine is not allowed to give a
// "bestmove" before the GUI sends it a "stop" or "ponderhit" command. We simply
// wait here until one of these commands (that raise StopRequest) is sent and
// then return, after which the bestmove and pondermove will be printed.

void Thread::wait_for_stop_or_ponderhit() {

  Signals.stopOnPonderhit = true;

  lock_grab(sleepLock);
  while (!Signals.stop) cond_wait(sleepCond, sleepLock);
  lock_release(sleepLock);
}


// Thread::cutoff_occurred() checks whether a beta cutoff has occurred in the
// current active split point, or in some ancestor of the split point.

bool Thread::cutoff_occurred() const {

  for (SplitPoint* sp = curSplitPoint; sp; sp = sp->parent)
      if (sp->cutoff)
          return true;

  return false;
}


// Thread::is_available_to() checks whether the thread is available to help the
// thread 'master' at a split point. An obvious requirement is that thread must
// be idle. With more than two threads, this is not sufficient: If the thread is
// the master of some active split point, it is only available as a slave to the
// slaves which are busy searching the split point at the top of slaves split
// point stack (the "helpful master concept" in YBWC terminology).

bool Thread::is_available_to(Thread* master) const {

  if (is_searching)
      return false;

  // Make a local copy to be sure doesn't become zero under our feet while
  // testing next condition and so leading to an out of bound access.
  int spCnt = splitPointsCnt;

  // No active split points means that the thread is available as a slave for any
  // other thread otherwise apply the "helpful master" concept if possible.
  return !spCnt || (splitPoints[spCnt - 1].slavesMask & (1ULL << master->idx));
}


// init() is called at startup. Initializes lock and condition variable and
// launches requested threads sending them immediately to sleep. We cannot use
// a c'tor becuase Threads is a static object and we need a fully initialized
// engine at this point due to allocation of endgames in Thread c'tor.

void ThreadsManager::init() {

  cond_init(sleepCond);
  lock_init(splitLock);
  timer = new Thread(&Thread::timer_loop);
  threads.push_back(new Thread(&Thread::main_loop));
  read_uci_options();
}


// d'tor cleanly terminates the threads when the program exits.

ThreadsManager::~ThreadsManager() {

  for (int i = 0; i < size(); i++)
      delete threads[i];

  delete timer;
  lock_destroy(splitLock);
  cond_destroy(sleepCond);
}


// read_uci_options() updates internal threads parameters from the corresponding
// UCI options and creates/destroys threads to match the requested number. Thread
// objects are dynamically allocated to avoid creating in advance all possible
// threads, with included pawns and material tables, if only few are used.

void ThreadsManager::read_uci_options() {

  maxThreadsPerSplitPoint = Options["Max Threads per Split Point"];
  minimumSplitDepth       = Options["Min Split Depth"] * ONE_PLY;
  useSleepingThreads      = Options["Use Sleeping Threads"];
  int requested           = Options["Threads"];

  assert(requested > 0);

  while (size() < requested)
      threads.push_back(new Thread(&Thread::idle_loop));

  while (size() > requested)
  {
      delete threads.back();
      threads.pop_back();
  }
}


// wake_up() is called before a new search to start the threads that are waiting
// on the sleep condition and to reset maxPly. When useSleepingThreads is set
// threads will be woken up at split time.

void ThreadsManager::wake_up() const {

  for (int i = 0; i < size(); i++)
  {
      threads[i]->maxPly = 0;
      threads[i]->do_sleep = false;

      if (!useSleepingThreads)
          threads[i]->wake_up();
  }
}


// sleep() is called after the search finishes to ask all the threads but the
// main one to go waiting on a sleep condition.

void ThreadsManager::sleep() const {

  for (int i = 1; i < size(); i++) // Main thread will go to sleep by itself
      threads[i]->do_sleep = true; // to avoid a race with start_searching()
}


// available_slave_exists() tries to find an idle thread which is available as
// a slave for the thread 'master'.

bool ThreadsManager::available_slave_exists(Thread* master) const {

  for (int i = 0; i < size(); i++)
      if (threads[i]->is_available_to(master))
          return true;

  return false;
}


// split() does the actual work of distributing the work at a node between
// several available threads. If it does not succeed in splitting the node
// (because no idle threads are available, or because we have no unused split
// point objects), the function immediately returns. If splitting is possible, a
// SplitPoint object is initialized with all the data that must be copied to the
// helper threads and then helper threads are told that they have been assigned
// work. This will cause them to instantly leave their idle loops and call
// search(). When all threads have returned from search() then split() returns.

template <bool Fake>
Value ThreadsManager::split(Position& pos, Stack* ss, Value alpha, Value beta,
                            Value bestValue, Move* bestMove, Depth depth,
                            Move threatMove, int moveCount, MovePicker* mp, int nodeType) {
  assert(pos.pos_is_ok());
  assert(bestValue > -VALUE_INFINITE);
  assert(bestValue <= alpha);
  assert(alpha < beta);
  assert(beta <= VALUE_INFINITE);
  assert(depth > DEPTH_ZERO);

  Thread* master = pos.this_thread();

  if (master->splitPointsCnt >= MAX_SPLITPOINTS_PER_THREAD)
      return bestValue;

  // Pick the next available split point from the split point stack
  SplitPoint* sp = &master->splitPoints[master->splitPointsCnt++];

  sp->parent = master->curSplitPoint;
  sp->master = master;
  sp->cutoff = false;
  sp->slavesMask = 1ULL << master->idx;
  sp->depth = depth;
  sp->bestMove = *bestMove;
  sp->threatMove = threatMove;
  sp->alpha = alpha;
  sp->beta = beta;
  sp->nodeType = nodeType;
  sp->bestValue = bestValue;
  sp->mp = mp;
  sp->moveCount = moveCount;
  sp->pos = &pos;
  sp->nodes = 0;
  sp->ss = ss;

  assert(master->is_searching);

  master->curSplitPoint = sp;
  int slavesCnt = 0;

  // Try to allocate available threads and ask them to start searching setting
  // is_searching flag. This must be done under lock protection to avoid concurrent
  // allocation of the same slave by another master.
  lock_grab(sp->lock);
  lock_grab(splitLock);

  for (int i = 0; i < size() && !Fake; ++i)
      if (threads[i]->is_available_to(master))
      {
          sp->slavesMask |= 1ULL << i;
          threads[i]->curSplitPoint = sp;
          threads[i]->is_searching = true; // Slave leaves idle_loop()

          if (useSleepingThreads)
              threads[i]->wake_up();

          if (++slavesCnt + 1 >= maxThreadsPerSplitPoint) // Master is always included
              break;
      }

  lock_release(splitLock);
  lock_release(sp->lock);

  // Everything is set up. The master thread enters the idle loop, from which
  // it will instantly launch a search, because its is_searching flag is set.
  // We pass the split point as a parameter to the idle loop, which means that
  // the thread will return from the idle loop when all slaves have finished
  // their work at this split point.
  if (slavesCnt || Fake)
  {
      master->idle_loop(sp);

      // In helpful master concept a master can help only a sub-tree of its split
      // point, and because here is all finished is not possible master is booked.
      assert(!master->is_searching);
  }

  // We have returned from the idle loop, which means that all threads are
  // finished. Note that setting is_searching and decreasing splitPointsCnt is
  // done under lock protection to avoid a race with Thread::is_available_to().
  lock_grab(sp->lock); // To protect sp->nodes
  lock_grab(splitLock);

  master->is_searching = true;
  master->splitPointsCnt--;
  master->curSplitPoint = sp->parent;
  pos.set_nodes_searched(pos.nodes_searched() + sp->nodes);
  *bestMove = sp->bestMove;

  lock_release(splitLock);
  lock_release(sp->lock);

  return sp->bestValue;
}

// Explicit template instantiations
template Value ThreadsManager::split<false>(Position&, Stack*, Value, Value, Value, Move*, Depth, Move, int, MovePicker*, int);
template Value ThreadsManager::split<true>(Position&, Stack*, Value, Value, Value, Move*, Depth, Move, int, MovePicker*, int);


// ThreadsManager::set_timer() is used to set the timer to trigger after msec
// milliseconds. If msec is 0 then timer is stopped.

void ThreadsManager::set_timer(int msec) {

  lock_grab(timer->sleepLock);
  timer->maxPly = msec;
  cond_signal(timer->sleepCond); // Wake up and restart the timer
  lock_release(timer->sleepLock);
}


// ThreadsManager::wait_for_search_finished() waits for main thread to go to
// sleep, this means search is finished. Then returns.

void ThreadsManager::wait_for_search_finished() {

  Thread* t = main_thread();
  lock_grab(t->sleepLock);
  cond_signal(t->sleepCond); // In case is waiting for stop or ponderhit
  while (!t->do_sleep) cond_wait(sleepCond, t->sleepLock);
  lock_release(t->sleepLock);
}


// ThreadsManager::start_searching() wakes up the main thread sleeping in
// main_loop() so to start a new search, then returns immediately.

void ThreadsManager::start_searching(const Position& pos, const LimitsType& limits,
                                     const std::vector<Move>& searchMoves) {
  wait_for_search_finished();

  SearchTime.restart(); // As early as possible

  Signals.stopOnPonderhit = Signals.firstRootMove = false;
  Signals.stop = Signals.failedLowAtRoot = false;

  RootPosition = pos;
  Limits = limits;
  RootMoves.clear();

  for (MoveList<MV_LEGAL> ml(pos); !ml.end(); ++ml)
      if (searchMoves.empty() || count(searchMoves.begin(), searchMoves.end(), ml.move()))
          RootMoves.push_back(RootMove(ml.move()));

  main_thread()->do_sleep = false;
  main_thread()->wake_up();
}
