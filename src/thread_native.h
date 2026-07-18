/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#ifndef THREAD_NATIVE_H_INCLUDED
#define THREAD_NATIVE_H_INCLUDED

#ifdef _MSC_VER
    #include <thread>
#else
    #include <pthread.h>
    #include <functional>
    #include <utility>

    #include "misc.h"
#endif

namespace Stockfish {

#ifdef _MSC_VER

// MSVC-compatible toolchains use std::thread because they do not provide
// pthreads by default. On all other platforms, pthreads is required and used.

using NativeThread = std::thread;

#else

// On OSX threads other than the main thread are created with a reduced stack
// size of 512KB by default, this is too low for deep searches, which require
// somewhat more than 1MB stack, so adjust it to TH_STACK_SIZE.
// The implementation calls pthread_create() with the stack size parameter
// equal to the Linux 8MB default, on platforms that support it.

class NativeThread {
    pthread_t thread;

    static constexpr usize TH_STACK_SIZE = 8 * 1024 * 1024;

   public:
    template<class Function, class... Args>
    explicit NativeThread(Function&& fun, Args&&... args) {
        auto func = new std::function<void()>(
          std::bind(std::forward<Function>(fun), std::forward<Args>(args)...));

        pthread_attr_t attr_storage, *attr = &attr_storage;
        pthread_attr_init(attr);
        pthread_attr_setstacksize(attr, TH_STACK_SIZE);

        auto start_routine = [](void* ptr) -> void* {
            auto f = reinterpret_cast<std::function<void()>*>(ptr);
            // Call the function
            (*f)();
            delete f;
            return nullptr;
        };

        pthread_create(&thread, attr, start_routine, func);
    }

    void join() { pthread_join(thread, nullptr); }
};

#endif  // _MSC_VER

}  // namespace Stockfish

#endif  // #ifndef THREAD_NATIVE_H_INCLUDED
