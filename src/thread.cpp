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

#include "search.h"
#include "thread.h"
#include "ucioption.h"

ThreadsManager Threads; // Global object definition

namespace { extern "C" {

 // start_routine() is the C function which is called when a new thread
 // is launched. It simply calls idle_loop() of the supplied thread. The
 // last two threads are dedicated to read input from GUI and to mimic a
 // timer, so they run in listener_loop() and timer_loop() respectively.

#if defined(_MSC_VER)
  DWORD WINAPI start_routine(LPVOID thread) {
#else
  void* start_routine(void* thread) {
#endif

    if (((Thread*)thread)->threadID == MAX_THREADS)
        ((Thread*)thread)->listener_loop();

    else if (((Thread*)thread)->threadID == MAX_THREADS + 1)
        ((Thread*)thread)->timer_loop();
    else
        ((Thread*)thread)->idle_loop(NULL);

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


// read_uci_options() updates number of active threads and other internal
// parameters according to the UCI options values. It is called before
// to start a new search.

void ThreadsManager::read_uci_options() {

  maxThreadsPerSplitPoint = Options["Maximum Number of Threads per Split Point"].value<int>();
  minimumSplitDepth       = Options["Minimum Split Depth"].value<int>() * ONE_PLY;
  useSleepingThreads      = Options["Use Sleeping Threads"].value<bool>();

  set_size(Options["Threads"].value<int>());
}


// set_size() changes the number of active threads and raises do_sleep flag for
// all the unused threads that will go immediately to sleep.

void ThreadsManager::set_size(int cnt) {

  assert(cnt > 0 && cnt <= MAX_THREADS);

  activeThreads = cnt;

  for (int i = 0; i < MAX_THREADS; i++)
      if (i < activeThreads)
      {
          // Dynamically allocate pawn and material hash tables according to the
          // number of active threads. This avoids preallocating memory for all
          // possible threads if only few are used as, for instance, on mobile
          // devices where memory is scarce and allocating for MAX_THREADS could
          // even result in a crash.
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

  // Initialize sleep condition used to block waiting for GUI input
  cond_init(&sleepCond);

  // Initialize threads lock, used when allocating slaves during splitting
  lock_init(&threadsLock);

  // Initialize sleep and split point locks
  for (int i = 0; i < MAX_THREADS + 2; i++)
  {
      lock_init(&threads[i].sleepLock);
      cond_init(&threads[i].sleepCond);

      for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
          lock_init(&(threads[i].splitPoints[j].lock));
  }

  // Initialize main thread's associated data
  threads[0].is_searching = true;
  threads[0].threadID = 0;
  set_size(1); // This makes all the threads but the main to go to sleep

  // Create and launch all the threads but the main that is already running,
  // threads will go immediately to sleep.
  for (int i = 1; i < MAX_THREADS + 2; i++)
  {
      threads[i].is_searching = false;
      threads[i].threadID = i;

#if defined(_MSC_VER)
      threads[i].handle = CreateThread(NULL, 0, start_routine, (LPVOID)&threads[i], 0, NULL);
      bool ok = (threads[i].handle != NULL);
#else
      bool ok = (pthread_create(&threads[i].handle, NULL, start_routine, (void*)&threads[i]) == 0);
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

  for (int i = 0; i < MAX_THREADS + 2; i++)
  {
      if (i != 0)
      {
          threads[i].do_terminate = true;
          threads[i].wake_up();

          // Wait for slave termination
#if defined(_MSC_VER)
          WaitForSingleObject(threads[i].handle, 0);
          CloseHandle(threads[i].handle);
#else
          pthread_join(threads[i].handle, NULL);
#endif
      }

      // Now we can safely destroy locks and wait conditions
      lock_destroy(&threads[i].sleepLock);
      cond_destroy(&threads[i].sleepCond);

      for (int j = 0; j < MAX_ACTIVE_SPLIT_POINTS; j++)
          lock_destroy(&(threads[i].splitPoints[j].lock));
  }

  lock_destroy(&threadsLock);
  cond_destroy(&sleepCond);
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
Value ThreadsManager::split(Position& pos, SearchStack* ss, Value alpha, Value beta,
                            Value bestValue, Depth depth, Move threatMove,
                            int moveCount, MovePicker* mp, int nodeType) {
  assert(pos.pos_is_ok());
  assert(bestValue >= -VALUE_INFINITE);
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

  // Pick the next available split point object from the split point stack
  SplitPoint* sp = masterThread.splitPoints + masterThread.activeSplitPoints;

  // Initialize the split point object
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
  // the state to Thread::WORKISWAITING, this must be done under lock protection
  // to avoid concurrent allocation of the same slave by another master.
  lock_grab(&threadsLock);

  for (i = 0; !Fake && i < activeThreads && workersCnt < maxThreadsPerSplitPoint; i++)
      if (i != master && threads[i].is_available_to(master))
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

  // In helpful master concept a master can help only a sub-tree, and
  // because here is all finished is not possible master is booked.
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
template Value ThreadsManager::split<false>(Position&, SearchStack*, Value, Value, Value, Depth, Move, int, MovePicker*, int);
template Value ThreadsManager::split<true>(Position&, SearchStack*, Value, Value, Value, Depth, Move, int, MovePicker*, int);


// Thread::timer_loop() is where the timer thread waits maxPly milliseconds
// and then calls do_timer_event().

void Thread::timer_loop() {

  while (!do_terminate)
  {
      lock_grab(&sleepLock);
      timed_wait(&sleepCond, &sleepLock, maxPly ? maxPly : INT_MAX);
      lock_release(&sleepLock);
      do_timer_event();
  }
}


// ThreadsManager::set_timer() is used to set the timer to trigger after msec
// milliseconds. If msec is 0 then timer is stopped.

void ThreadsManager::set_timer(int msec) {

  Thread& timer = threads[MAX_THREADS + 1];

  lock_grab(&timer.sleepLock);
  timer.maxPly = msec;
  cond_signal(&timer.sleepCond); // Wake up and restart the timer
  lock_release(&timer.sleepLock);
}


// Thread::listener_loop() is where the listener thread, used for I/O, waits for
// input. When is_searching is false then input is read in sync with main thread
// (that blocks), otherwise the listener thread reads any input asynchronously
// and processes the input line calling do_uci_async_cmd().

void Thread::listener_loop() {

  std::string cmd;

  while (true)
  {
      lock_grab(&sleepLock);

      Threads.inputLine = cmd;
      do_sleep = !is_searching;

      // Here the thread is parked in sync mode after a line has been read
      while (do_sleep && !do_terminate) // Catches spurious wake ups
      {
          cond_signal(&Threads.sleepCond);   // Wake up main thread
          cond_wait(&sleepCond, &sleepLock); // Sleep here
      }

      lock_release(&sleepLock);

      if (do_terminate)
          return;

      if (!std::getline(std::cin, cmd)) // Block waiting for input
          cmd = "quit";

      lock_grab(&sleepLock);

      // If we are in async mode then process the command now
      if (is_searching)
      {
          // Command "quit" is the last one received by the GUI, so park the
          // thread waiting for exiting.
          if (cmd == "quit")
              is_searching = false;

          do_uci_async_cmd(cmd);
          cmd = ""; // Input has been consumed
      }

      lock_release(&sleepLock);
  }
}


// ThreadsManager::getline() is used by main thread to block and wait for input,
// the behaviour mimics std::getline().

void ThreadsManager::getline(std::string& cmd) {

  Thread& listener = threads[MAX_THREADS];

  lock_grab(&listener.sleepLock);

  listener.is_searching = false; // Set sync mode

  // If there is already some input to grab then skip without to wake up the
  // listener. This can happen if after we send the "bestmove", the GUI sends
  // a command that the listener buffers in inputLine before going to sleep.
  if (inputLine.empty())
  {
      listener.do_sleep = false;
      cond_signal(&listener.sleepCond); // Wake up listener thread

      while (!listener.do_sleep)
          cond_wait(&sleepCond, &listener.sleepLock); // Wait for input
  }

  cmd = inputLine;
  inputLine = ""; // Input has been consumed

  lock_release(&listener.sleepLock);
}


// ThreadsManager::start_listener() is called at the beginning of the search to
// swith from sync behaviour (default) to async and so be able to read from UCI
// while other threads are searching. This avoids main thread polling for input.

void ThreadsManager::start_listener() {

  Thread& listener = threads[MAX_THREADS];

  lock_grab(&listener.sleepLock);
  listener.is_searching = true;
  listener.do_sleep = false;
  cond_signal(&listener.sleepCond); // Wake up listener thread
  lock_release(&listener.sleepLock);
}


// ThreadsManager::stop_listener() is called before to send "bestmove" to GUI to
// return to in-sync behaviour. This is needed because while in async mode any
// command is discarded without being processed (except for a very few ones).

void ThreadsManager::stop_listener() {

  Thread& listener = threads[MAX_THREADS];

  lock_grab(&listener.sleepLock);
  listener.is_searching = false;
  lock_release(&listener.sleepLock);
}
