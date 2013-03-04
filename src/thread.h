/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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

#if !defined(THREAD_H_INCLUDED)
#define THREAD_H_INCLUDED

#include <vector>

#include "material.h"
#include "movepick.h"
#include "pawns.h"
#include "position.h"
#include "search.h"

const int MAX_THREADS = 64; // Because SplitPoint::slavesMask is a uint64_t
const int MAX_SPLITPOINTS_PER_THREAD = 8;

struct Mutex {
  Mutex() { lock_init(l); }
 ~Mutex() { lock_destroy(l); }

  void lock() { lock_grab(l); }
  void unlock() { lock_release(l); }

private:
  friend struct ConditionVariable;

  Lock l;
};

struct ConditionVariable {
  ConditionVariable() { cond_init(c); }
 ~ConditionVariable() { cond_destroy(c); }

  void wait(Mutex& m) { cond_wait(c, m.l); }
  void wait_for(Mutex& m, int ms) { timed_wait(c, m.l, ms); }
  void notify_one() { cond_signal(c); }

private:
  WaitCondition c;
};

struct Thread;

struct SplitPoint {

  // Const data after split point has been setup
  const Position* pos;
  const Search::Stack* ss;
  Thread* masterThread;
  Depth depth;
  Value beta;
  int nodeType;
  Move threatMove;

  // Const pointers to shared data
  MovePicker* movePicker;
  SplitPoint* parentSplitPoint;

  // Shared data
  Mutex mutex;
  volatile uint64_t slavesMask;
  volatile int64_t nodes;
  volatile Value alpha;
  volatile Value bestValue;
  volatile Move bestMove;
  volatile int moveCount;
  volatile bool cutoff;
};


/// Thread struct keeps together all the thread related stuff like locks, state
/// and especially split points. We also use per-thread pawn and material hash
/// tables so that once we get a pointer to an entry its life time is unlimited
/// and we don't have to care about someone changing the entry under our feet.

struct Thread {

  Thread();
  virtual ~Thread();

  virtual void idle_loop();
  void notify_one();
  bool cutoff_occurred() const;
  bool is_available_to(Thread* master) const;
  void wait_for(volatile const bool& b);

  template <bool Fake>
  void split(Position& pos, Search::Stack* ss, Value alpha, Value beta, Value* bestValue, Move* bestMove,
             Depth depth, Move threatMove, int moveCount, MovePicker* movePicker, int nodeType);

  SplitPoint splitPoints[MAX_SPLITPOINTS_PER_THREAD];
  Material::Table materialTable;
  Endgames endgames;
  Pawns::Table pawnsTable;
  Position* activePosition;
  size_t idx;
  int maxPly;
  Mutex mutex;
  ConditionVariable sleepCondition;
  NativeHandle handle;
  SplitPoint* volatile activeSplitPoint;
  volatile int splitPointsSize;
  volatile bool searching;
  volatile bool exit;
};


/// MainThread and TimerThread are sublassed from Thread to characterize the two
/// special threads: the main one and the recurring timer.

struct MainThread : public Thread {
  MainThread() : thinking(true) {} // Avoid a race with start_thinking()
  virtual void idle_loop();
  volatile bool thinking;
};

struct TimerThread : public Thread {
  TimerThread() : msec(0) {}
  virtual void idle_loop();
  int msec;
};


/// ThreadPool struct handles all the threads related stuff like init, starting,
/// parking and, the most important, launching a slave thread at a split point.
/// All the access to shared thread data is done through this class.

struct ThreadPool : public std::vector<Thread*> {

  void init(); // No c'tor and d'tor, threads rely on globals that should
  void exit(); // be initialized and valid during the whole thread lifetime.

  MainThread* main_thread() { return static_cast<MainThread*>((*this)[0]); }
  void read_uci_options();
  Thread* available_slave(Thread* master) const;
  void wait_for_think_finished();
  void start_thinking(const Position&, const Search::LimitsType&,
                      const std::vector<Move>&, Search::StateStackPtr&);

  bool sleepWhileIdle;
  Depth minimumSplitDepth;
  size_t maxThreadsPerSplitPoint;
  Mutex mutex;
  ConditionVariable sleepCondition;
  TimerThread* timer;
};

extern ThreadPool Threads;

#endif // !defined(THREAD_H_INCLUDED)
