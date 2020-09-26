/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

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

#ifndef THREAD_WIN32_OSX_H_INCLUDED
#define THREAD_WIN32_OSX_H_INCLUDED

#include <thread>

/// On OSX threads other than the main thread are created with a reduced stack
/// size of 512KB by default, this is too low for deep searches, which require
/// somewhat more than 1MB stack, so adjust it to TH_STACK_SIZE.

#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)

#include <pthread.h>

static constexpr size_t TH_STACK_SIZE = 7 * 1024 * 1024 + 512 * 1024; // 7.5 MB, makes Thread+stack nicely fit in 16MB for large pages
static constexpr size_t TH_RANDOM_OFFSET_WINDOW = 2 * 1024 * 1024; // this is too big

template <typename FnType, class ThreadArgs>
void* start_routine(void* rawArgsPtr)
{
  std::pair<FnType*, ThreadArgs*> *argsPtr = static_cast<std::pair<FnType*, ThreadArgs*>*>(rawArgsPtr);
  FnType* fn = argsPtr->first;
  ThreadArgs* threadArgs = argsPtr->second;
  delete argsPtr;

  fn(threadArgs, 0);
  return nullptr;
}

class NativeThread {

   pthread_t thread;

public:
  NativeThread() : thread()
  {
  }

  template<class FnType, class ThreadArgs>
  explicit NativeThread(FnType* fn, ThreadArgs* threadArgs, size_t stackOffset)
  {
    std::pair<FnType*, ThreadArgs*> *argsPtr =
        new std::pair<FnType*, ThreadArgs*>(fn, threadArgs);

    char *stackMem = static_cast<char *>(threadArgs->threadMem) + stackOffset;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stackMem, TH_STACK_SIZE);
    pthread_create(&thread, &attr, start_routine<FnType, ThreadArgs>, argsPtr);
  }
  void join() { pthread_join(thread, NULL); }
};

#else // Default case: use STL classes

typedef std::thread NativeThread;
static constexpr size_t TH_STACK_SIZE = 0;
static constexpr size_t TH_RANDOM_OFFSET_WINDOW = 4096;

#endif

#endif // #ifndef THREAD_WIN32_OSX_H_INCLUDED
