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

ThreadsManager ThreadsMgr; // Global object definition

namespace {

 // init_thread() is the function which is called when a new thread is
 // launched. It simply calls the idle_loop() function with the supplied
 // threadID. There are two versions of this function; one for POSIX
 // threads and one for Windows threads.

#if !defined(_MSC_VER)

  void* init_thread(void* threadID) {

    ThreadsMgr.idle_loop(*(int*)threadID, NULL);
    return NULL;
  }

#else

  DWORD WINAPI init_thread(LPVOID threadID) {

    ThreadsMgr.idle_loop(*(int*)threadID, NULL);
    return 0;
  }

#endif

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

// init_threads() is called during startup. Initializes locks and condition
// variables and launches all threads sending them immediately to sleep.

void ThreadsManager::init_threads() {

  int i, arg[MAX_THREADS];
  bool ok;

  // This flag is needed to properly end the threads when program exits
  allThreadsShouldExit = false;

  // Threads will sent to sleep as soon as created, only main thread is kept alive
  activeThreads = 1;

  lock_init(&mpLock);

  for (i = 0; i < MAX_THREADS; i++)
  {
      // Initialize thread and split point locks
      lock_init(&threads[i].sleepLock);
      cond_init(&threads[i].sleepCond);

      for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
          lock_init(&(threads[i].splitPoints[j].lock));

      // All threads but first should be set to THREAD_INITIALIZING
      threads[i].state = (i == 0 ? THREAD_SEARCHING : THREAD_INITIALIZING);

      // Not in Threads c'tor to avoid global initialization order issues
      threads[i].pawnTable.init();
      threads[i].materialTable.init();
  }

  // Create and startup the threads
  for (i = 1; i < MAX_THREADS; i++)
  {
      arg[i] = i;

#if !defined(_MSC_VER)
      pthread_t pthread[1];
      ok = (pthread_create(pthread, NULL, init_thread, (void*)(&arg[i])) == 0);
      pthread_detach(pthread[0]);
#else
      ok = (CreateThread(NULL, 0, init_thread, (LPVOID)(&arg[i]), 0, NULL) != NULL);
#endif
      if (!ok)
      {
          std::cout << "Failed to create thread number " << i << std::endl;
          exit(EXIT_FAILURE);
      }

      // Wait until the thread has finished launching and is gone to sleep
      while (threads[i].state == THREAD_INITIALIZING) {}
  }
}


// exit_threads() is called when the program exits. It makes all the
// helper threads exit cleanly.

void ThreadsManager::exit_threads() {

  // Force the woken up threads to exit idle_loop() and hence terminate
  allThreadsShouldExit = true;

  for (int i = 0; i < MAX_THREADS; i++)
  {
      // Wake up all the threads and waits for termination
      if (i != 0)
      {
          threads[i].wake_up();
          while (threads[i].state != THREAD_TERMINATED) {}
      }

      // Now we can safely destroy the locks and wait conditions
      lock_destroy(&threads[i].sleepLock);
      cond_destroy(&threads[i].sleepCond);

      for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
          lock_destroy(&(threads[i].splitPoints[j].lock));
  }

  lock_destroy(&mpLock);
}


// cutoff_at_splitpoint() checks whether a beta cutoff has occurred in
// the thread's currently active split point, or in some ancestor of
// the current split point.

bool ThreadsManager::cutoff_at_splitpoint(int threadID) const {

  assert(threadID >= 0 && threadID < activeThreads);

  SplitPoint* sp = threads[threadID].splitPoint;

  for ( ; sp && !sp->betaCutoff; sp = sp->parent) {}
  return sp != NULL;
}


// thread_is_available() checks whether the thread with threadID "slave" is
// available to help the thread with threadID "master" at a split point. An
// obvious requirement is that "slave" must be idle. With more than two
// threads, this is not by itself sufficient:  If "slave" is the master of
// some active split point, it is only available as a slave to the other
// threads which are busy searching the split point at the top of "slave"'s
// split point stack (the "helpful master concept" in YBWC terminology).

bool ThreadsManager::thread_is_available(int slave, int master) const {

  assert(slave >= 0 && slave < activeThreads);
  assert(master >= 0 && master < activeThreads);
  assert(activeThreads > 1);

  if (threads[slave].state != THREAD_AVAILABLE || slave == master)
      return false;

  // Make a local copy to be sure doesn't change under our feet
  int localActiveSplitPoints = threads[slave].activeSplitPoints;

  // No active split points means that the thread is available as
  // a slave for any other thread.
  if (localActiveSplitPoints == 0 || activeThreads == 2)
      return true;

  // Apply the "helpful master" concept if possible. Use localActiveSplitPoints
  // that is known to be > 0, instead of threads[slave].activeSplitPoints that
  // could have been set to 0 by another thread leading to an out of bound access.
  if (threads[slave].splitPoints[localActiveSplitPoints - 1].slaves[master])
      return true;

  return false;
}


// available_thread_exists() tries to find an idle thread which is available as
// a slave for the thread with threadID "master".

bool ThreadsManager::available_thread_exists(int master) const {

  assert(master >= 0 && master < activeThreads);
  assert(activeThreads > 1);

  for (int i = 0; i < activeThreads; i++)
      if (thread_is_available(i, master))
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
  if (   !available_thread_exists(master)
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
  splitPoint.betaCutoff = false;
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
      splitPoint.slaves[i] = 0;

  masterThread.splitPoint = &splitPoint;

  // If we are here it means we are not available
  assert(masterThread.state != THREAD_AVAILABLE);

  int workersCnt = 1; // At least the master is included

  // Allocate available threads setting state to THREAD_BOOKED
  for (i = 0; !Fake && i < activeThreads && workersCnt < maxThreadsPerSplitPoint; i++)
      if (thread_is_available(i, master))
      {
          threads[i].state = THREAD_BOOKED;
          threads[i].splitPoint = &splitPoint;
          splitPoint.slaves[i] = 1;
          workersCnt++;
      }

  assert(Fake || workersCnt > 1);

  // We can release the lock because slave threads are already booked and master is not available
  lock_release(&mpLock);

  // Tell the threads that they have work to do. This will make them leave
  // their idle loop.
  for (i = 0; i < activeThreads; i++)
      if (i == master || splitPoint.slaves[i])
      {
          assert(i == master || threads[i].state == THREAD_BOOKED);

          threads[i].state = THREAD_WORKISWAITING; // This makes the slave to exit from idle_loop()

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
template void ThreadsManager::split<0>(Position&, SearchStack*, Value*, const Value, Value*, Depth, Move, int, MovePicker*, bool);
template void ThreadsManager::split<1>(Position&, SearchStack*, Value*, const Value, Value*, Depth, Move, int, MovePicker*, bool);
