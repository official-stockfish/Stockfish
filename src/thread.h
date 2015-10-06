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

#ifndef THREAD_H_INCLUDED
#define THREAD_H_INCLUDED

#include <atomic>
#include <bitset>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "material.h"
#include "movepick.h"
#include "pawns.h"
#include "position.h"
#include "search.h"
#include "thread_win32.h"

struct Thread;

const size_t MAX_THREADS = 128;


/// ThreadBase struct is the base of the hierarchy from where we derive all the
/// specialized thread classes.

struct ThreadBase : public std::thread {

  virtual ~ThreadBase() = default;
  virtual void idle_loop() = 0;
  void notify_one();
  void wait(volatile const bool& b);
  void wait_while(volatile const bool& b);

  Mutex mutex;
  ConditionVariable sleepCondition;
  volatile bool exit = false;
};


/// Thread struct keeps together all the thread related stuff like locks, state
/// and especially split points. We also use per-thread pawn and material hash
/// tables so that once we get a pointer to an entry its life time is unlimited
/// and we don't have to care about someone changing the entry under our feet.

struct Thread : public ThreadBase {

  Thread();
  virtual void idle_loop();
  void search(bool isMainThread = false);

  Pawns::Table pawnsTable;
  Material::Table materialTable;
  Endgames endgames;
  size_t idx, PVIdx;
  int maxPly;
  volatile bool searching;

  Position rootPos;
  Search::RootMoveVector rootMoves;
  Search::Stack stack[MAX_PLY+4];
  HistoryStats History;
  MovesStats Countermoves;
  Depth depth;
};


/// MainThread and TimerThread are derived classes used to characterize the two
/// special threads: the main one and the recurring timer.

struct MainThread : public Thread {
  virtual void idle_loop();
  void join();
  void think();
  volatile bool thinking = true; // Avoid a race with start_thinking()
};

struct TimerThread : public ThreadBase {

  static const int Resolution = 5; // Millisec between two check_time() calls

  virtual void idle_loop();

  bool run = false;
};


/// ThreadPool struct handles all the threads related stuff like init, starting,
/// parking and, most importantly, launching a slave thread at a split point.
/// All the access to shared thread data is done through this class.

struct ThreadPool : public std::vector<Thread*> {

  void init(); // No c'tor and d'tor, threads rely on globals that should be
  void exit(); // initialized and are valid during the whole thread lifetime.

  MainThread* main() { return static_cast<MainThread*>(at(0)); }
  void read_uci_options();
  void start_thinking(const Position&, const Search::LimitsType&, Search::StateStackPtr&);
  int64_t nodes_searched();
  TimerThread* timer;
};

extern ThreadPool Threads;

#endif // #ifndef THREAD_H_INCLUDED
