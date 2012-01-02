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

#include <iostream>

#include "search.h"
#include "thread.h"
#include "ucioption.h"

using namespace Search;

ThreadsManager Threads; // Global object

namespace { extern "C" {

 // start_routine() is the C function which is called when a new thread
 // is launched. It simply calls idle_loop() of the supplied thread. The first
 // and last thread are special. First one is the main search thread while the
 // last one mimics a timer, they run in main_loop() and timer_loop().

#if defined(_MSC_VER)
  DWORD WINAPI start_routine(LPVOID thread) {
#else
  void* start_routine(void* thread) {
#endif

    Thread* th = (Thread*)thread;

    if (th->threadID == 0)
        th->main_loop();

    else if (th->threadID == MAX_THREADS)
        th->timer_loop();

    else
        th->idle_loop(NULL);

    return 0;
  }

} }


// wake_up() wakes up the thread, normally at the beginning of the search or,
// if "sleeping threads" is used, when there is some work to do.

void Thread::wake_up() {

  lock_grab(&sleepLock);
  cond_signal(&sleepCond);
  lock_release(&sleepLock);
}


// cutoff_occurred() checks whether a beta cutoff has occurred in the current
// active split point, or in some ancestor of the split point.

bool Thread::cutoff_occurred() const {

  for (SplitPoint* sp = splitPoint; sp; sp = sp->parent)
      if (sp->is_betaCutoff)
          return true;

  return false;
}


// is_available_to() checks whether the thread is available to help the thread with
// threadID "master" at a split point. An obvious requirement is that thread must be
// idle. With more than two threads, this is not by itself sufficient: If the thread
// is the master of some active split point, it is only available as a slave to the
// threads which are busy searching the split point at the top of "slave"'s split
// point stack (the "helpful master concept" in YBWC terminology).

bool Thread::is_available_to(int master) const {

  if (is_searching)
      return false;

  // Make a local copy to be sure doesn't become zero under our feet while
  // testing next condition and so leading to an out of bound access.
  int localActiveSplitPoints = activeSplitPoints;

  // No active split points means that the thread is available as a slave for any
  // other thread otherwise apply the "helpful master" concept if possible.
  if (   !localActiveSplitPoints
      || splitPoints[localActiveSplitPoints - 1].is_slave[master])
      return true;

  return false;
}


// read_uci_options() updates number of active threads and other parameters
// according to the UCI options values. It is called before to start a new search.

void ThreadsManager::read_uci_options() {

  maxThreadsPerSplitPoint = Options["Max Threads per Split Point"];
  minimumSplitDepth       = Options["Min Split Depth"] * ONE_PLY;
  useSleepingThreads      = Options["Use Sleeping Threads"];

  set_size(Options["Threads"]);
}


// set_size() changes the number of active threads and raises do_sleep flag for
// all the unused threads that will go immediately to sleep.

void ThreadsManager::set_size(int cnt) {

  assert(cnt > 0 && cnt <= MAX_THREADS);

  activeThreads = cnt;

  for (int i = 1; i < MAX_THREADS; i++) // Ignore main thread
      if (i < activeThreads)
      {
          // Dynamically allocate pawn and material hash tables according to the
          // number of active threads. This avoids preallocating memory for all
          // possible threads if only few are used.
          threads[i].pawnTable.init();
          threads[i].materialTable.init();

          threads[i].do_sleep = false;
      }
      else
          threads[i].do_sleep = true;
}


// init() is called during startup. Initializes locks and condition variables
// and launches all threads sending them immediately to sleep.

void ThreadsManager::init() {

  // Initialize sleep condition and lock used by thread manager
  cond_init(&sleepCond);
  lock_init(&threadsLock);

  // Initialize thread's sleep conditions and split point locks
  for (int i = 0; i <= MAX_THREADS; i++)
  {
      lock_init(&threads[i].sleepLock);
      cond_init(&threads[i].sleepCond);

      for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
          lock_init(&(threads[i].splitPoints[j].lock));
  }

  // Allocate main thread tables to call evaluate() also when not searching
  threads[0].pawnTable.init();
  threads[0].materialTable.init();

  // Create and launch all the threads, threads will go immediately to sleep
  for (int i = 0; i <= MAX_THREADS; i++)
  {
      threads[i].is_searching = false;
      threads[i].do_sleep = (i != 0); // Avoid a race with start_thinking()
      threads[i].threadID = i;

#if defined(_MSC_VER)
      threads[i].handle = CreateThread(NULL, 0, start_routine, &threads[i], 0, NULL);
      bool ok = (threads[i].handle != NULL);
#else
      bool ok = !pthread_create(&threads[i].handle, NULL, start_routine, &threads[i]);
#endif

      if (!ok)
      {
          std::cerr << "Failed to create thread number " << i << std::endl;
          ::exit(EXIT_FAILURE);
      }
  }
}


// exit() is called to cleanly terminate the threads when the program finishes

void ThreadsManager::exit() {

  for (int i = 0; i <= MAX_THREADS; i++)
  {
      threads[i].do_terminate = true; // Search must be already finished
      threads[i].wake_up();

      // Wait for thread termination
#if defined(_MSC_VER)
      WaitForSingleObject(threads[i].handle, INFINITE);
      CloseHandle(threads[i].handle);
#else
      pthread_join(threads[i].handle, NULL);
#endif

      // Now we can safely destroy associated locks and wait conditions
      lock_destroy(&threads[i].sleepLock);
      cond_destroy(&threads[i].sleepCond);

      for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
          lock_destroy(&(threads[i].splitPoints[j].lock));
  }

  lock_destroy(&threadsLock);
  cond_destroy(&sleepCond);
}


// available_slave_exists() tries to find an idle thread which is available as
// a slave for the thread with threadID 'master'.

bool ThreadsManager::available_slave_exists(int master) const {

  assert(master >= 0 && master < activeThreads);

  for (int i = 0; i < activeThreads; i++)
      if (threads[i].is_available_to(master))
          return true;

  return false;
}


// split_point_finished() checks if all the slave threads of a given split
// point have finished searching.

bool ThreadsManager::split_point_finished(SplitPoint* sp) const {

  for (int i = 0; i < activeThreads; i++)
      if (sp->is_slave[i])
          return false;

  return true;
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
                            Value bestValue, Depth depth, Move threatMove,
                            int moveCount, MovePicker* mp, int nodeType) {
  assert(pos.pos_is_ok());
  assert(bestValue > -VALUE_INFINITE);
  assert(bestValue <= alpha);
  assert(alpha < beta);
  assert(beta <= VALUE_INFINITE);
  assert(depth > DEPTH_ZERO);
  assert(pos.thread() >= 0 && pos.thread() < activeThreads);
  assert(activeThreads > 1);

  int i, master = pos.thread();
  Thread& masterThread = threads[master];

  // If we already have too many active split points, don't split
  if (masterThread.activeSplitPoints >= MAX_ACTIVE_SPLIT_POINTS)
      return bestValue;

  // Pick the next available split point from the split point stack
  SplitPoint* sp = &masterThread.splitPoints[masterThread.activeSplitPoints];

  // Initialize the split point
  sp->parent = masterThread.splitPoint;
  sp->master = master;
  sp->is_betaCutoff = false;
  sp->depth = depth;
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

  for (i = 0; i < activeThreads; i++)
      sp->is_slave[i] = false;

  // If we are here it means we are not available
  assert(masterThread.is_searching);

  int workersCnt = 1; // At least the master is included

  // Try to allocate available threads and ask them to start searching setting
  // is_searching flag. This must be done under lock protection to avoid concurrent
  // allocation of the same slave by another master.
  lock_grab(&threadsLock);

  for (i = 0; !Fake && i < activeThreads && workersCnt < maxThreadsPerSplitPoint; i++)
      if (threads[i].is_available_to(master))
      {
          workersCnt++;
          sp->is_slave[i] = true;
          threads[i].splitPoint = sp;

          // This makes the slave to exit from idle_loop()
          threads[i].is_searching = true;

          if (useSleepingThreads)
              threads[i].wake_up();
      }

  lock_release(&threadsLock);

  // We failed to allocate even one slave, return
  if (!Fake && workersCnt == 1)
      return bestValue;

  masterThread.splitPoint = sp;
  masterThread.activeSplitPoints++;

  // Everything is set up. The master thread enters the idle loop, from which
  // it will instantly launch a search, because its is_searching flag is set.
  // We pass the split point as a parameter to the idle loop, which means that
  // the thread will return from the idle loop when all slaves have finished
  // their work at this split point.
  masterThread.idle_loop(sp);

  // In helpful master concept a master can help only a sub-tree of its split
  // point, and because here is all finished is not possible master is booked.
  assert(!masterThread.is_searching);

  // We have returned from the idle loop, which means that all threads are
  // finished. Note that changing state and decreasing activeSplitPoints is done
  // under lock protection to avoid a race with Thread::is_available_to().
  lock_grab(&threadsLock);

  masterThread.is_searching = true;
  masterThread.activeSplitPoints--;

  lock_release(&threadsLock);

  masterThread.splitPoint = sp->parent;
  pos.set_nodes_searched(pos.nodes_searched() + sp->nodes);

  return sp->bestValue;
}

// Explicit template instantiations
template Value ThreadsManager::split<false>(Position&, Stack*, Value, Value, Value, Depth, Move, int, MovePicker*, int);
template Value ThreadsManager::split<true>(Position&, Stack*, Value, Value, Value, Depth, Move, int, MovePicker*, int);


// Thread::timer_loop() is where the timer thread waits maxPly milliseconds and
// then calls do_timer_event(). If maxPly is 0 thread sleeps until is woken up.
extern void check_time();

void Thread::timer_loop() {

  while (!do_terminate)
  {
      lock_grab(&sleepLock);
      timed_wait(&sleepCond, &sleepLock, maxPly ? maxPly : INT_MAX);
      lock_release(&sleepLock);
      check_time();
  }
}


// ThreadsManager::set_timer() is used to set the timer to trigger after msec
// milliseconds. If msec is 0 then timer is stopped.

void ThreadsManager::set_timer(int msec) {

  Thread& timer = threads[MAX_THREADS];

  lock_grab(&timer.sleepLock);
  timer.maxPly = msec;
  cond_signal(&timer.sleepCond); // Wake up and restart the timer
  lock_release(&timer.sleepLock);
}


// Thread::main_loop() is where the main thread is parked waiting to be started
// when there is a new search. Main thread will launch all the slave threads.

void Thread::main_loop() {

  while (true)
  {
      lock_grab(&sleepLock);

      do_sleep = true; // Always return to sleep after a search
      is_searching = false;

      while (do_sleep && !do_terminate)
      {
          cond_signal(&Threads.sleepCond); // Wake up UI thread if needed
          cond_wait(&sleepCond, &sleepLock);
      }

      is_searching = true;

      lock_release(&sleepLock);

      if (do_terminate)
          return;

      think(); // This is the search entry point
  }
}


// ThreadsManager::start_thinking() is used by UI thread to wake up the main
// thread parked in main_loop() and starting a new search. If asyncMode is true
// then function returns immediately, otherwise caller is blocked waiting for
// the search to finish.

void ThreadsManager::start_thinking(const Position& pos, const LimitsType& limits,
                                    const std::vector<Move>& searchMoves, bool asyncMode) {
  Thread& main = threads[0];

  lock_grab(&main.sleepLock);

  // Wait main thread has finished before to launch a new search
  while (!main.do_sleep)
      cond_wait(&sleepCond, &main.sleepLock);

  // Copy input arguments to initialize the search
  RootPosition.copy(pos, 0);
  Limits = limits;
  SearchMoves = searchMoves;

  // Reset signals before to start the new search
  memset((void*)&Signals, 0, sizeof(Signals));

  main.do_sleep = false;
  cond_signal(&main.sleepCond); // Wake up main thread and start searching

  if (!asyncMode)
      while (!main.do_sleep)
          cond_wait(&sleepCond, &main.sleepLock);

  lock_release(&main.sleepLock);
}


// ThreadsManager::stop_thinking() is used by UI thread to raise a stop request
// and to wait for the main thread finishing the search. Needed to wait exiting
// and terminate the threads after a 'quit' command.

void ThreadsManager::stop_thinking() {

  Thread& main = threads[0];

  Search::Signals.stop = true;

  lock_grab(&main.sleepLock);

  cond_signal(&main.sleepCond); // In case is waiting for stop or ponderhit

  while (!main.do_sleep)
      cond_wait(&sleepCond, &main.sleepLock);

  lock_release(&main.sleepLock);
}


// ThreadsManager::wait_for_stop_or_ponderhit() is called when the maximum depth
// is reached while the program is pondering. The point is to work around a wrinkle
// in the UCI protocol: When pondering, the engine is not allowed to give a
// "bestmove" before the GUI sends it a "stop" or "ponderhit" command. We simply
// wait here until one of these commands (that raise StopRequest) is sent and
// then return, after which the bestmove and pondermove will be printed.

void ThreadsManager::wait_for_stop_or_ponderhit() {

  Signals.stopOnPonderhit = true;

  Thread& main = threads[0];

  lock_grab(&main.sleepLock);

  while (!Signals.stop)
      cond_wait(&main.sleepCond, &main.sleepLock);

  lock_release(&main.sleepLock);
}
