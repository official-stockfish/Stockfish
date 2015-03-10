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

#ifndef THREAD_WIN32_H_INCLUDED
#define THREAD_WIN32_H_INCLUDED

/// STL thread library uded by gcc and mingw compilers is implemented above
/// POSIX pthread. Unfortunatly this yields to a much slower speed (about 30%)
/// than the native Win32 calls. So use our own implementation that relies on
/// the Windows specific low level calls.

#if defined(_WIN32) && !defined(_MSC_VER)

#ifndef NOMINMAX
#  define NOMINMAX // disable macros min() and max()
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NOMINMAX

// We use critical sections on Windows to support Windows XP and older versions.
// Unfortunately, cond_wait() is racy between lock_release() and WaitForSingleObject()
// but apart from this they have the same speed performance of SRW locks.
typedef CRITICAL_SECTION Lock;
typedef HANDLE WaitCondition;
typedef HANDLE NativeHandle;

// On Windows 95 and 98 parameter lpThreadId may not be null
inline DWORD* dwWin9xKludge() { static DWORD dw; return &dw; }

#  define lock_init(x) InitializeCriticalSection(&(x))
#  define lock_grab(x) EnterCriticalSection(&(x))
#  define lock_release(x) LeaveCriticalSection(&(x))
#  define lock_destroy(x) DeleteCriticalSection(&(x))
#  define cond_init(x) { x = CreateEvent(0, FALSE, FALSE, 0); }
#  define cond_destroy(x) CloseHandle(x)
#  define cond_signal(x) SetEvent(x)
#  define cond_wait(x,y) { lock_release(y); WaitForSingleObject(x, INFINITE); lock_grab(y); }
#  define cond_timedwait(x,y,z) { lock_release(y); WaitForSingleObject(x,z); lock_grab(y); }

/// Mutex and ConditionVariable struct are wrappers of the low level locking
/// machinery and are modeled after the corresponding C++11 classes.

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

  void notify_one() { cond_signal(c); }
  void wait(std::unique_lock<Mutex>& lk) { cond_wait(c, lk.mutex()->l); }

  template<class Predicate>
  void wait(std::unique_lock<Mutex>& lk, Predicate p) { while (!p()) this->wait(lk); }

  void wait_for(std::unique_lock<Mutex>& lk, const std::chrono::milliseconds& ms) {
    cond_timedwait(c, lk.mutex()->l, ms.count());
  }

private:
  WaitCondition c;
};

#else // Default case: use STL classes

typedef std::mutex Mutex;
typedef std::condition_variable ConditionVariable;

#endif

#endif // #ifndef THREAD_WIN32_H_INCLUDED
