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

#if defined(_MSC_VER)

#define _CRT_SECURE_NO_DEPRECATE
#define NOMINMAX // disable macros min() and max()
#include <windows.h>
#include <sys/timeb.h>

#else

#  include <sys/time.h>
#  include <sys/types.h>
#  include <unistd.h>
#  if defined(__hpux)
#     include <sys/pstat.h>
#  endif

#endif

#if !defined(NO_PREFETCH)
#  include <xmmintrin.h>
#endif

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "bitcount.h"
#include "misc.h"
#include "thread.h"

using namespace std;

/// Version number. If Version is left empty, then Tag plus current
/// date (in the format YYMMDD) is used as a version number.

static const string Version = "2.2.1";
static const string Tag = "";


/// engine_info() returns the full name of the current Stockfish version.
/// This will be either "Stockfish YYMMDD" (where YYMMDD is the date when
/// the program was compiled) or "Stockfish <version number>", depending
/// on whether Version is empty.

const string engine_info(bool to_uci) {

  const string months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");
  const string cpu64(Is64Bit ? " 64bit" : "");
  const string popcnt(HasPopCnt ? " SSE4.2" : "");

  string month, day, year;
  stringstream s, date(__DATE__); // From compiler, format is "Sep 21 2008"

  if (Version.empty())
  {
      date >> month >> day >> year;

      s << "Stockfish " << Tag
        << setfill('0') << " " << year.substr(2)
        << setw(2) << (1 + months.find(month) / 4)
        << setw(2) << day << cpu64 << popcnt;
  }
  else
      s << "Stockfish " << Version << cpu64 << popcnt;

  s << (to_uci ? "\nid author ": " by ")
    << "Tord Romstad, Marco Costalba and Joona Kiiski";

  return s.str();
}


/// Debug functions used mainly to collect run-time statistics

static uint64_t hits[2], means[2];

void dbg_hit_on(bool b) { hits[0]++; if (b) hits[1]++; }
void dbg_hit_on_c(bool c, bool b) { if (c) dbg_hit_on(b); }
void dbg_mean_of(int v) { means[0]++; means[1] += v; }

void dbg_print() {

  if (hits[0])
      cerr << "Total " << hits[0] << " Hits " << hits[1]
           << " hit rate (%) " << 100 * hits[1] / hits[0] << endl;

  if (means[0])
      cerr << "Total " << means[0] << " Mean "
           << (float)means[1] / means[0] << endl;
}


/// system_time() returns the current system time, measured in milliseconds

int system_time() {

#if defined(_MSC_VER)
  struct _timeb t;
  _ftime(&t);
  return int(t.time * 1000 + t.millitm);
#else
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec * 1000 + t.tv_usec / 1000;
#endif
}


/// cpu_count() tries to detect the number of CPU cores

int cpu_count() {

#if defined(_MSC_VER)
  SYSTEM_INFO s;
  GetSystemInfo(&s);
  return std::min(int(s.dwNumberOfProcessors), MAX_THREADS);
#else

#  if defined(_SC_NPROCESSORS_ONLN)
  return std::min((int)sysconf(_SC_NPROCESSORS_ONLN), MAX_THREADS);
#  elif defined(__hpux)
  struct pst_dynamic psd;
  if (pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0) == -1)
      return 1;
  return std::min((int)psd.psd_proc_cnt, MAX_THREADS);
#  else
  return 1;
#  endif

#endif
}


/// timed_wait() waits for msec milliseconds. It is mainly an helper to wrap
/// conversion from milliseconds to struct timespec, as used by pthreads.

void timed_wait(WaitCondition* sleepCond, Lock* sleepLock, int msec) {

#if defined(_MSC_VER)
  int tm = msec;
#else
  struct timeval t;
  struct timespec abstime, *tm = &abstime;

  gettimeofday(&t, NULL);

  abstime.tv_sec = t.tv_sec + (msec / 1000);
  abstime.tv_nsec = (t.tv_usec + (msec % 1000) * 1000) * 1000;

  if (abstime.tv_nsec > 1000000000LL)
  {
      abstime.tv_sec += 1;
      abstime.tv_nsec -= 1000000000LL;
  }
#endif

  cond_timedwait(sleepCond, sleepLock, tm);
}


/// prefetch() preloads the given address in L1/L2 cache. This is a non
/// blocking function and do not stalls the CPU waiting for data to be
/// loaded from memory, that can be quite slow.
#if defined(NO_PREFETCH)

void prefetch(char*) {}

#else

void prefetch(char* addr) {

#  if defined(__INTEL_COMPILER) || defined(__ICL)
   // This hack prevents prefetches to be optimized away by
   // Intel compiler. Both MSVC and gcc seems not affected.
   __asm__ ("");
#  endif

  _mm_prefetch(addr, _MM_HINT_T2);
  _mm_prefetch(addr+64, _MM_HINT_T2); // 64 bytes ahead
}

#endif
