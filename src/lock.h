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


#if !defined(_MSC_VER)

#  include <pthread.h>

typedef pthread_mutex_t Lock;

#  define lock_init(x) pthread_mutex_init(x, NULL)
#  define lock_grab(x) pthread_mutex_lock(x)
#  define lock_release(x) pthread_mutex_unlock(x)
#  define lock_destroy(x) pthread_mutex_destroy(x)


#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

typedef CRITICAL_SECTION Lock;
#  define lock_init(x) InitializeCriticalSection(x)
#  define lock_grab(x) EnterCriticalSection(x)
#  define lock_release(x) LeaveCriticalSection(x)
#  define lock_destroy(x) DeleteCriticalSection(x)


#endif

#endif // !defined(LOCK_H_INCLUDED)
