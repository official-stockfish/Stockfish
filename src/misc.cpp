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

#if !defined(_MSC_VER)

#  include <sys/time.h>
#  include <sys/types.h>
#  include <unistd.h>
#  if defined(__hpux)
#     include <sys/pstat.h>
#  endif

#else

#define _CRT_SECURE_NO_DEPRECATE
#include <windows.h>
#include <sys/timeb.h>

#endif

#if !defined(NO_PREFETCH)
#  include <xmmintrin.h>
#endif

#include <cassert>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "bitcount.h"
#include "misc.h"
#include "thread.h"

using namespace std;

/// Version number. If EngineVersion is left empty, then AppTag plus
/// current date (in the format YYMMDD) is used as a version number.

static const string AppName = "Stockfish";
static const string EngineVersion = "2.1.1";
static const string AppTag  = "";


/// engine_name() returns the full name of the current Stockfish version.
/// This will be either "Stockfish YYMMDD" (where YYMMDD is the date when
/// the program was compiled) or "Stockfish <version number>", depending
/// on whether the constant EngineVersion is empty.

const string engine_name() {

  const string months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");
  const string cpu64(CpuIs64Bit ? " 64bit" : "");

  if (!EngineVersion.empty())
      return AppName + " " + EngineVersion + cpu64;

  stringstream s, date(__DATE__); // From compiler, format is "Sep 21 2008"
  string month, day, year;

  date >> month >> day >> year;

  s << setfill('0') << AppName + " " + AppTag + " "
    << year.substr(2, 2) << setw(2)
    << (1 + months.find(month) / 4) << setw(2)
    << day << cpu64;

  return s.str();
}


/// Our brave developers! Required by UCI

const string engine_authors() {

  return "Tord Romstad, Marco Costalba and Joona Kiiski";
}


/// Debug stuff. Helper functions used mainly for debugging purposes

static uint64_t dbg_hit_cnt0;
static uint64_t dbg_hit_cnt1;
static uint64_t dbg_mean_cnt0;
static uint64_t dbg_mean_cnt1;

void dbg_print_hit_rate() {

  if (dbg_hit_cnt0)
      cout << "Total " << dbg_hit_cnt0 << " Hit " << dbg_hit_cnt1
           << " hit rate (%) " << 100 * dbg_hit_cnt1 / dbg_hit_cnt0 << endl;
}

void dbg_print_mean() {

  if (dbg_mean_cnt0)
      cout << "Total " << dbg_mean_cnt0 << " Mean "
           << (float)dbg_mean_cnt1 / dbg_mean_cnt0 << endl;
}

void dbg_mean_of(int v) {

  dbg_mean_cnt0++;
  dbg_mean_cnt1 += v;
}

void dbg_hit_on(bool b) {

  dbg_hit_cnt0++;
  if (b)
      dbg_hit_cnt1++;
}

void dbg_hit_on_c(bool c, bool b) { if (c) dbg_hit_on(b); }
void dbg_before() { dbg_hit_on(false); }
void dbg_after()  { dbg_hit_on(true); dbg_hit_cnt0--; }


/// get_system_time() returns the current system time, measured in milliseconds

int get_system_time() {

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
  return Min(s.dwNumberOfProcessors, MAX_THREADS);
#else

#  if defined(_SC_NPROCESSORS_ONLN)
  return Min(sysconf(_SC_NPROCESSORS_ONLN), MAX_THREADS);
#  elif defined(__hpux)
  struct pst_dynamic psd;
  if (pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0) == -1)
      return 1;
  return Min(psd.psd_proc_cnt, MAX_THREADS);
#  else
  return 1;
#  endif

#endif
}


/// Check for console input. Original code from Beowulf, Olithink and Greko

#ifndef _WIN32

int input_available() {

  fd_set readfds;
  struct timeval  timeout;

  FD_ZERO(&readfds);
  FD_SET(fileno(stdin), &readfds);
  timeout.tv_sec = 0; // Set to timeout immediately
  timeout.tv_usec = 0;
  select(16, &readfds, 0, 0, &timeout);

  return (FD_ISSET(fileno(stdin), &readfds));
}

#else

int input_available() {

  static HANDLE inh = NULL;
  static bool usePipe = false;
  INPUT_RECORD rec[256];
  DWORD nchars, recCnt;

  if (!inh)
  {
      inh = GetStdHandle(STD_INPUT_HANDLE);
      if (GetConsoleMode(inh, &nchars))
      {
          SetConsoleMode(inh, nchars & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
          FlushConsoleInputBuffer(inh);
      } else
          usePipe = true;
  }

  // When using Standard C input functions, also check if there
  // is anything in the buffer. After a call to such functions,
  // the input waiting in the pipe will be copied to the buffer,
  // and the call to PeekNamedPipe can indicate no input available.
  // Setting stdin to unbuffered was not enough. [from Greko]
  if (stdin->_cnt > 0)
      return 1;

  // When running under a GUI the input commands are sent to us
  // directly over the internal pipe. If PeekNamedPipe() returns 0
  // then something went wrong. Probably the parent program exited.
  // Returning 1 will make the next call to the input function
  // return EOF, where this should be catched then.
  if (usePipe)
      return PeekNamedPipe(inh, NULL, 0, NULL, &nchars, NULL) ? nchars : 1;

  // Count the number of unread input records, including keyboard,
  // mouse, and window-resizing input records.
  GetNumberOfConsoleInputEvents(inh, &nchars);

  // Read data from console without removing it from the buffer
  if (nchars <= 0 || !PeekConsoleInput(inh, rec, Min(nchars, 256), &recCnt))
      return 0;

  // Search for at least one keyboard event
  for (DWORD i = 0; i < recCnt; i++)
      if (rec[i].EventType == KEY_EVENT)
          return 1;

  return 0;
}

#endif


/// prefetch() preloads the given address in L1/L2 cache. This is a non
/// blocking function and do not stalls the CPU waiting for data to be
/// loaded from memory, that can be quite slow.
#if defined(NO_PREFETCH)

void prefetch(char*) {}

#else

void prefetch(char* addr) {

#if defined(__INTEL_COMPILER) || defined(__ICL)
   // This hack prevents prefetches to be optimized away by
   // Intel compiler. Both MSVC and gcc seems not affected.
   __asm__ ("");
#endif

  _mm_prefetch(addr, _MM_HINT_T2);
  _mm_prefetch(addr+64, _MM_HINT_T2); // 64 bytes ahead
}

#endif
