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

#if !defined(THREAD_H_INCLUDED)
#define THREAD_H_INCLUDED

#include <vector>

#include "material.h"
#include "movepick.h"
#include "pawns.h"
#include "position.h"
#include "search.h"

const int MAX_THREADS = 32;
const int MAX_SPLITPOINTS_PER_THREAD = 8;

struct SplitPoint {

  // Const data after split point has been setup
  const Position* pos;
  const Search::Stack* ss;
  Depth depth;
  Value beta;
  int nodeType;
  int master;
  Move threatMove;

  // Const pointers to shared data
  MovePicker* mp;
  SplitPoint* parent;

  // Shared data
  Lock lock;
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

class Thread {

  Thread(const Thread&);            // Only declared to disable the default ones
  Thread& operator=(const Thread&); // that are not suitable in this case.

  typedef void (Thread::* Fn) ();

public:
  Thread(Fn fn);
  ~Thread();

  void wake_up();
  bool cutoff_occurred() const;
  bool is_available_to(int master) const;
  void idle_loop(SplitPoint* sp_master);
  void idle_loop() { idle_loop(NULL); } // Hack to allow storing in start_fn
  void main_loop();
  void timer_loop();
  void wait_for_stop_or_ponderhit();

  SplitPoint splitPoints[MAX_SPLITPOINTS_PER_THREAD];
  MaterialTable materialTable;
  PawnTable pawnTable;
  int threadID;
  int maxPly;
  Lock sleepLock;
  WaitCondition sleepCond;
  NativeHandle handle;
  Fn start_fn;
  SplitPoint* volatile curSplitPoint;
  volatile int splitPointsCnt;
  volatile bool is_searching;
  volatile bool do_sleep;
  volatile bool do_exit;
};


/// ThreadsManager class handles all the threads related stuff like init, starting,
/// parking and, the most important, launching a slave thread at a split point.
/// All the access to shared thread data is done through this class.

class ThreadsManager {
  /* As long as the single ThreadsManager object is defined as a global we don't
     need to explicitly initialize to zero its data members because variables with
     static storage duration are automatically set to zero before enter main()
  */
public:
  void init(); // No c'tor becuase Threads is static and we need engine initialized
  ~ThreadsManager();

  Thread& operator[](int id) { return *threads[id]; }
  bool use_sleeping_threads() const { return useSleepingThreads; }
  int min_split_depth() const { return minimumSplitDepth; }
  int size() const { return (int)threads.size(); }

  void wake_up() const;
  void sleep() const;
  void read_uci_options();
  bool available_slave_exists(int master) const;
  void set_timer(int msec);
  void wait_for_search_finished();
  void start_searching(const Position& pos, const Search::LimitsType& limits,
                       const std::vector<Move>& searchMoves);

  template <bool Fake>
  Value split(Position& pos, Search::Stack* ss, Value alpha, Value beta, Value bestValue, Move* bestMove,
              Depth depth, Move threatMove, int moveCount, MovePicker* mp, int nodeType);
private:
  friend class Thread;

  std::vector<Thread*> threads;
  Thread* timer;
  Lock splitLock;
  WaitCondition sleepCond;
  Depth minimumSplitDepth;
  int maxThreadsPerSplitPoint;
  bool useSleepingThreads;
};

extern ThreadsManager Threads;

#endif // !defined(THREAD_H_INCLUDED)
