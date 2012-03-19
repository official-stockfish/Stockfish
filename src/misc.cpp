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

#if defined(_WIN32) || defined(_WIN64)

#define NOMINMAX // disable macros min() and max()
#include <windows.h>

#else

#  include <unistd.h>
#  if defined(__hpux)
#     include <sys/pstat.h>
#  endif

#endif

#if !defined(NO_PREFETCH)
#  include <xmmintrin.h>
#endif

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <streambuf>

#include "misc.h"
#include "thread.h"

using namespace std;

/// Version number. If Version is left empty, then Tag plus current
/// date (in the format YYMMDD) is used as a version number.

static const string Version = "";
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
        << setw(2) << day;
  }
  else
      s << "Stockfish " << Version;

  s << cpu64 << popcnt << (to_uci ? "\nid author ": " by ")
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


/// Our fancy logging facility. The trick here is to replace cout.rdbuf() with
/// this one that sends the output both to console and to a file, this allow us
/// to toggle the logging of std::cout to a file while preserving output to
/// stdout and without changing a single line of code! Idea and code from:
/// http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81

class Tee: public streambuf {
public:
  typedef char_traits<char> traits_type;
  typedef traits_type::int_type int_type;

  Tee(ios& s, ofstream& f) : stream(s), file(f), stream_buf(s.rdbuf()) {}
  ~Tee() { set(false); }

  void set(bool b) { stream.rdbuf(b ? this : stream_buf); }

private:
  int_type overflow(int_type c) {

    if (traits_type::eq_int_type(c, traits_type::eof()))
        return traits_type::not_eof(c);

    c = stream_buf->sputc(traits_type::to_char_type(c));

    if (!traits_type::eq_int_type(c, traits_type::eof()))
        c = file.rdbuf()->sputc(traits_type::to_char_type(c));

    return c;
  }

  int sync() {

    int c = stream_buf->pubsync();

    if (c != -1)
        c = file.rdbuf()->pubsync();

    return c;
  }

  int underflow() { return traits_type::not_eof(stream_buf->sgetc()); }

  int uflow() {

      int c = stream_buf->sbumpc();

      if (!traits_type::eq_int_type(c, traits_type::eof()))
          file.rdbuf()->sputc(traits_type::to_char_type(c));

      return traits_type::not_eof(c);
  }

  ios& stream;
  ofstream& file;
  streambuf* stream_buf;
};

class Logger {
public:
   Logger() : in(cin, file), out(cout, file) {}
  ~Logger() { set(false); }

  void set(bool b) {

    if (b && !file.is_open())
    {
        file.open("io_log.txt", ifstream::out | ifstream::app);
        in.set(true);
        out.set(true);
    }
    else if (!b && file.is_open())
    {
        out.set(false);
        in.set(false);
        file.close();
    }
  }

private:
  Tee in, out;
  ofstream file;
};

void logger_set(bool b) {

  static Logger l;
  l.set(b);
}


/// cpu_count() tries to detect the number of CPU cores

int cpu_count() {

#if defined(_WIN32) || defined(_WIN64)
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

void timed_wait(WaitCondition& sleepCond, Lock& sleepLock, int msec) {

#if defined(_WIN32) || defined(_WIN64)
  int tm = msec;
#else
  timespec ts, *tm = &ts;
  uint64_t ms = Time::current_time().msec() + msec;

  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000LL;
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
