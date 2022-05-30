/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

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
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>

#include "material.h"
#include "movepick.h"
#include "pawns.h"
#include "position.h"
#include "search.h"
#include "thread_win32_osx.h"

namespace Stockfish {

/// Thread class keeps together all the thread-related stuff. We use
/// per-thread pawn and material hash tables so that once we get a
/// pointer to an entry its life time is unlimited and we don't have
/// to care about someone changing the entry under our feet.

namespace Detail {

  template <typename T>
  struct TypeIdentity {
    using Type = T;
  };

}

class Thread {

  std::mutex mutex;
  std::condition_variable cv;
  size_t idx;
  bool exit = false, searching = true; // Set before starting std::thread
  std::function<void(Thread&)> worker;
  std::function<void(Position&)> on_eval_callback;
  NativeThread stdThread;

public:
  explicit Thread(size_t);
  virtual ~Thread();
  virtual void search();

  // The function object to be executed is taken by value to remove
  // the need for separate lvalue and rvalue overloads.
  // The worker thread needs to have ownership of the task
  // to be executed because otherwise there's no way to manage its lifetime.
  virtual void execute_with_worker(std::function<void(Thread&)> t);

  void clear();
  void idle_loop();
  void start_searching();
  void wait_for_search_finished();
  size_t id() const { return idx; }

  void wait_for_worker_finished();

  template <typename FuncT>
  void set_eval_callback(FuncT&& f) { on_eval_callback = std::forward<FuncT>(f); }

  void clear_eval_callback() { on_eval_callback = nullptr; }

  void on_eval() { if (on_eval_callback) on_eval_callback(rootPos); }

  Pawns::Table pawnsTable;
  Material::Table materialTable;
  size_t pvIdx, pvLast;
  RunningAverage complexityAverage;
  std::atomic<uint64_t> nodes, tbHits, bestMoveChanges;
  int selDepth, nmpMinPly;
  Color nmpColor;
  Value bestValue, optimism[COLOR_NB];
  uint64_t maxNodes;

  Position rootPos;
  StateInfo rootState;
  Search::RootMoves rootMoves;
  Depth rootDepth, completedDepth, depth, previousDepth;
  Value rootDelta;
  CounterMoveHistory counterMoves;
  ButterflyHistory mainHistory;
  CapturePieceToHistory captureHistory;
  ContinuationHistory continuationHistory[2][2];
  Score trend;
  int failedHighCnt;
  bool rootInTB;
  int Cardinality;
  bool UseRule50;
  Depth ProbeDepth;
};


/// MainThread is a derived class specific for main thread

struct MainThread : public Thread {

  using Thread::Thread;

  void search() override;
  void check_time();

  double previousTimeReduction;
  Value bestPreviousScore;
  Value bestPreviousAverageScore;
  Value iterValue[4];
  int callsCnt;
  bool stopOnPonderhit;
  std::atomic_bool ponder;
};


/// ThreadPool struct handles all the threads-related stuff like init, starting,
/// parking and, most importantly, launching a thread. All the access to threads
/// is done through this class.

struct ThreadPool : public std::vector<Thread*> {

  // Each thread gets its own copy of the `worker` function object.
  // This means that each worker thread will have exclusive access
  // to the state of the `worker` function object.
  void execute_with_workers(const std::function<void(Thread&)>& worker);

  template <typename IndexT, typename FuncT>
  void for_each_index_with_workers(
    IndexT begin,
    typename Detail::TypeIdentity<IndexT>::Type end,
    FuncT func)
  {
    // This value must outlive the function call.
    // It's fairly safe if we make it static
    // because for_each_index_with_workers
    // is not reentrant nor thread safe.
    static std::atomic<IndexT> i_atomic;
    i_atomic.store(begin);

    execute_with_workers(
      [end, func](Thread& th) mutable {
        for(;;) {
          const auto i = i_atomic.fetch_add(1);
          if (i >= end)
            break;

          func(th, i);
        }
      });
  }

  template <typename IndexT, typename FuncT>
  void for_each_index_chunk_with_workers(
    IndexT begin,
    typename Detail::TypeIdentity<IndexT>::Type end,
    FuncT func)
  {
    // This value must outlive the function call.
    // It's fairly safe if we make it static
    // because for_each_index_with_workers
    // is not reentrant nor thread safe.
    const IndexT size = end - begin;
    const IndexT chunk_size = (size + this->size()) / this->size();

    execute_with_workers(
      [chunk_size, end, func](Thread& th) mutable {
        const IndexT thread_id = th.id();
        const IndexT offset = chunk_size * thread_id;
        if (offset >= end)
          return;

        const IndexT count = offset + chunk_size > end ? end - offset : chunk_size;
        func(th, offset, count);
      });
  }

  void start_thinking(Position&, StateListPtr&, const Search::LimitsType&, bool = false);
  void clear();
  void set(size_t);

  MainThread* main()        const { return static_cast<MainThread*>(front()); }
  uint64_t nodes_searched() const { return accumulate(&Thread::nodes); }
  uint64_t tb_hits()        const { return accumulate(&Thread::tbHits); }
  Thread* get_best_thread() const;
  void start_searching();
  void wait_for_search_finished() const;
  void wait_for_workers_finished() const;

  std::atomic_bool stop, increaseDepth;

private:
  StateListPtr setupStates;

  uint64_t accumulate(std::atomic<uint64_t> Thread::* member) const {

    uint64_t sum = 0;
    for (Thread* th : *this)
        sum += (th->*member).load(std::memory_order_relaxed);
    return sum;
  }
};

extern ThreadPool Threads;

} // namespace Stockfish

#endif // #ifndef THREAD_H_INCLUDED
