/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include "thread.h"
#include "ucioption.h"

ThreadsManager Threads; // Global object definition

namespace { extern "C" {

 // start_routine() is the C function which is called when a new thread
 // is launched. It simply calls idle_loop() with the supplied threadID.
 // There are two versions of this function; one for POSIX threads and
 // one for Windows threads.

#if defined(_MSC_VER)

  DWORD WINAPI start_routine(LPVOID threadID) {

    Threads.idle_loop(*(int*)threadID, NULL);
    return 0;
  }

#else

  void* start_routine(void* threadID) {

    Threads.idle_loop(*(int*)threadID, NULL);
    return NULL;
  }

#endif

} }


// wake_up() wakes up the thread, normally at the beginning of the search or,
// if "sleeping threads" is used, when there is some work to do.

void Thread::wake_up() {

  lock_grab(&sleepLock);
  cond_signal(&sleepCond);
  lock_release(&sleepLock);
}


// cutoff_occurred() checks whether a beta cutoff has occurred in
// the thread's currently active split point, or in some ancestor of
// the current split point.

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

  if (state != AVAILABLE)
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


// read_uci_options() updates number of active threads and other internal
// parameters according to the UCI options values. It is called before
// to start a new search.

void ThreadsManager::read_uci_options() {

  maxThreadsPerSplitPoint = Options["Maximum Number of Threads per Split Point"].value<int>();
  minimumSplitDepth       = Options["Minimum Split Depth"].value<int>() * ONE_PLY;
  useSleepingThreads      = Options["Use Sleeping Threads"].value<bool>();
  activeThreads           = Options["Threads"].value<int>();
}


// init() is called during startup. Initializes locks and condition variables
// and launches all threads sending them immediately to sleep.

void ThreadsManager::init() {

  int threadID[MAX_THREADS];

  // This flag is needed to properly end the threads when program exits
  allThreadsShouldExit = false;

  // Threads will sent to sleep as soon as created, only main thread is kept alive
  activeThreads = 1;
  threads[0].state = Thread::SEARCHING;

  // Allocate pawn and material hash tables for main thread
  init_hash_tables();

  lock_init(&mpLock);

  // Initialize thread and split point locks
  for (int i = 0; i < MAX_THREADS; i++)
  {
      lock_init(&threads[i].sleepLock);
      cond_init(&threads[i].sleepCond);

      for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
          lock_init(&(threads[i].splitPoints[j].lock));
  }

  // Create and startup all the threads but the main that is already running
  for (int i = 1; i < MAX_THREADS; i++)
  {
      threads[i].state = Thread::INITIALIZING;
      threadID[i] = i;

#if defined(_MSC_VER)
      bool ok = (CreateThread(NULL, 0, start_routine, (LPVOID)&threadID[i], 0, NULL) != NULL);
#else
      pthread_t pthreadID;
      bool ok = (pthread_create(&pthreadID, NULL, start_routine, (void*)&threadID[i]) == 0);
      pthread_detach(pthreadID);
#endif
      if (!ok)
      {
          std::cout << "Failed to create thread number " << i << std::endl;
          ::exit(EXIT_FAILURE);
      }

      // Wait until the thread has finished launching and is gone to sleep
      while (threads[i].state == Thread::INITIALIZING) {}
  }
}


// exit() is called to cleanly exit the threads when the program finishes

void ThreadsManager::exit() {

  // Force the woken up threads to exit idle_loop() and hence terminate
  allThreadsShouldExit = true;

  for (int i = 0; i < MAX_THREADS; i++)
  {
      // Wake up all the threads and waits for termination
      if (i != 0)
      {
          threads[i].wake_up();
          while (threads[i].state != Thread::TERMINATED) {}
      }

      // Now we can safely destroy the locks and wait conditions
      lock_destroy(&threads[i].sleepLock);
      cond_destroy(&threads[i].sleepCond);

      for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
          lock_destroy(&(threads[i].splitPoints[j].lock));
  }

  lock_destroy(&mpLock);
}


// init_hash_tables() dynamically allocates pawn and material hash tables
// according to the number of active threads. This avoids preallocating
// memory for all possible threads if only few are used as, for instance,
// on mobile devices where memory is scarce and allocating for MAX_THREADS
// threads could even result in a crash.

void ThreadsManager::init_hash_tables() {

  for (int i = 0; i < activeThreads; i++)
  {
      threads[i].pawnTable.init();
      threads[i].materialTable.init();
  }
}


// available_slave_exists() tries to find an idle thread which is available as
// a slave for the thread with threadID "master".

bool ThreadsManager::available_slave_exists(int master) const {

  assert(master >= 0 && master < activeThreads);

  for (int i = 0; i < activeThreads; i++)
      if (i != master && threads[i].is_available_to(master))
          return true;

  return false;
}


// split() does the actual work of distributing the work at a node between
// several available threads. If it does not succeed in splitting the
// node (because no idle threads are available, or because we have no unused
// split point objects), the function immediately returns. If splitting is
// possible, a SplitPoint object is initialized with all the data that must be
// copied to the helper threads and we tell our helper threads that they have
// been assigned work. This will cause them to instantly leave their idle loops and
// call search().When all threads have returned from search() then split() returns.

template <bool Fake>
void ThreadsManager::split(Position& pos, SearchStack* ss, Value* alpha, const Value beta,
                           Value* bestValue, Depth depth, Move threatMove,
                           int moveCount, MovePicker* mp, bool pvNode) {
  assert(pos.is_ok());
  assert(*bestValue >= -VALUE_INFINITE);
  assert(*bestValue <= *alpha);
  assert(*alpha < beta);
  assert(beta <= VALUE_INFINITE);
  assert(depth > DEPTH_ZERO);
  assert(pos.thread() >= 0 && pos.thread() < activeThreads);
  assert(activeThreads > 1);

  int i, master = pos.thread();
  Thread& masterThread = threads[master];

  lock_grab(&mpLock);

  // If no other thread is available to help us, or if we have too many
  // active split points, don't split.
  if (   !available_slave_exists(master)
      || masterThread.activeSplitPoints >= MAX_ACTIVE_SPLIT_POINTS)
  {
      lock_release(&mpLock);
      return;
  }

  // Pick the next available split point object from the split point stack
  SplitPoint& splitPoint = masterThread.splitPoints[masterThread.activeSplitPoints++];

  // Initialize the split point object
  splitPoint.parent = masterThread.splitPoint;
  splitPoint.master = master;
  splitPoint.is_betaCutoff = false;
  splitPoint.depth = depth;
  splitPoint.threatMove = threatMove;
  splitPoint.alpha = *alpha;
  splitPoint.beta = beta;
  splitPoint.pvNode = pvNode;
  splitPoint.bestValue = *bestValue;
  splitPoint.mp = mp;
  splitPoint.moveCount = moveCount;
  splitPoint.pos = &pos;
  splitPoint.nodes = 0;
  splitPoint.ss = ss;
  for (i = 0; i < activeThreads; i++)
      splitPoint.is_slave[i] = false;

  masterThread.splitPoint = &splitPoint;

  // If we are here it means we are not available
  assert(masterThread.state != Thread::AVAILABLE);

  int workersCnt = 1; // At least the master is included

  // Allocate available threads setting state to THREAD_BOOKED
  for (i = 0; !Fake && i < activeThreads && workersCnt < maxThreadsPerSplitPoint; i++)
      if (i != master && threads[i].is_available_to(master))
      {
          threads[i].state = Thread::BOOKED;
          threads[i].splitPoint = &splitPoint;
          splitPoint.is_slave[i] = true;
          workersCnt++;
      }

  assert(Fake || workersCnt > 1);

  // We can release the lock because slave threads are already booked and master is not available
  lock_release(&mpLock);

  // Tell the threads that they have work to do. This will make them leave
  // their idle loop.
  for (i = 0; i < activeThreads; i++)
      if (i == master || splitPoint.is_slave[i])
      {
          assert(i == master || threads[i].state == Thread::BOOKED);

          threads[i].state = Thread::WORKISWAITING; // This makes the slave to exit from idle_loop()

          if (useSleepingThreads && i != master)
              threads[i].wake_up();
      }

  // Everything is set up. The master thread enters the idle loop, from
  // which it will instantly launch a search, because its state is
  // THREAD_WORKISWAITING.  We send the split point as a second parameter to the
  // idle loop, which means that the main thread will return from the idle
  // loop when all threads have finished their work at this split point.
  idle_loop(master, &splitPoint);

  // We have returned from the idle loop, which means that all threads are
  // finished. Update alpha and bestValue, and return.
  lock_grab(&mpLock);

  *alpha = splitPoint.alpha;
  *bestValue = splitPoint.bestValue;
  masterThread.activeSplitPoints--;
  masterThread.splitPoint = splitPoint.parent;
  pos.set_nodes_searched(pos.nodes_searched() + splitPoint.nodes);

  lock_release(&mpLock);
}

// Explicit template instantiations
template void ThreadsManager::split<false>(Position&, SearchStack*, Value*, const Value, Value*, Depth, Move, int, MovePicker*, bool);
template void ThreadsManager::split<true>(Position&, SearchStack*, Value*, const Value, Value*, Depth, Move, int, MovePicker*, bool);
