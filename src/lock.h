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


#if !defined(LOCK_H_INCLUDED)
#define LOCK_H_INCLUDED


// x86 assembly language locks or OS spin locks may perform faster than
// mutex locks on some platforms. On my machine, mutexes seem to be the
// best.

//#define ASM_LOCK
//#define OS_SPIN_LOCK


#if defined(ASM_LOCK)


typedef volatile int Lock;

static inline void LockX86(Lock *lock) {
  int dummy;
  asm __volatile__("1:          movl    $1, %0" "\n\t"
      "            xchgl   (%1), %0" "\n\t" "            testl   %0, %0" "\n\t"
      "            jz      3f" "\n\t" "2:          pause" "\n\t"
      "            movl    (%1), %0" "\n\t" "            testl   %0, %0" "\n\t"
      "            jnz     2b" "\n\t" "            jmp     1b" "\n\t" "3:"
      "\n\t":"=&q"(dummy)
      :"q"(lock)
      :"cc");
}

static inline void UnlockX86(Lock *lock) {
  int dummy;
  asm __volatile__("movl    $0, (%1)":"=&q"(dummy)
      :"q"(lock));
}

#  define lock_init(x, y) (*(x) = 0)
#  define lock_grab(x) LockX86(x)
#  define lock_release(x) UnlockX86(x)
#  define lock_destroy(x)


#elif defined(OS_SPIN_LOCK)


#  include <libkern/OSAtomic.h>

typedef OSSpinLock Lock;

#  define lock_init(x, y) (*(x) = 0)
#  define lock_grab(x) OSSpinLockLock(x)
#  define lock_release(x) OSSpinLockUnlock(x)
#  define lock_destroy(x)


#elif !defined(_MSC_VER)

#  include <pthread.h>

typedef pthread_mutex_t Lock;

#  define lock_init(x, y) pthread_mutex_init(x, y)
#  define lock_grab(x) pthread_mutex_lock(x)
#  define lock_release(x) pthread_mutex_unlock(x)
#  define lock_destroy(x) pthread_mutex_destroy(x)


#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

typedef CRITICAL_SECTION Lock;
#  define lock_init(x, y) InitializeCriticalSection(x)
#  define lock_grab(x) EnterCriticalSection(x)
#  define lock_release(x) LeaveCriticalSection(x)
#  define lock_destroy(x) DeleteCriticalSection(x)


#endif

#endif // !defined(LOCK_H_INCLUDED)
