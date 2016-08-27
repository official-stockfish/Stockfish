/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "misc.h"
#include "thread.h"

namespace {

/// Version number. If Version is left empty, then compile date in the format
/// DD-MM-YY and show in engine_info.
const std::string Version = "";

/// Our fancy logging facility. The trick here is to replace cin.rdbuf() and
/// cout.rdbuf() with two Tie objects that tie cin and cout to a file stream. We
/// can toggle the logging of std::cout and std:cin at runtime whilst preserving
/// usual I/O functionality, all without changing a single line of code!
/// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81

struct Tie: public std::streambuf { // MSVC requires split streambuf for cin and cout

  Tie(std::streambuf* b, std::streambuf* l) : buf(b), logBuf(l) {}

  int sync() { return logBuf->pubsync(), buf->pubsync(); }
  int overflow(int c) { return log(buf->sputc((char)c), "<< "); }
  int underflow() { return buf->sgetc(); }
  int uflow() { return log(buf->sbumpc(), ">> "); }

  std::streambuf *buf, *logBuf;

  int log(int c, const char* prefix) {

    static int last = '\n'; // Single log file

    if (last == '\n')
        logBuf->sputn(prefix, 3);

    return last = logBuf->sputc((char)c);
  }
};

class Logger {

  Logger()
    :
    in(std::cin.rdbuf(),
       file.rdbuf()
    ),
    out(std::cout.rdbuf(),
	file.rdbuf()
    )
    {}
 ~Logger() { start(false); }

  std::ofstream file;
  Tie in, out;

public:
  static void start(bool b) {

    static Logger l;

    if (b && !l.file.is_open())
    {
        l.file.open("io_log.txt", std::ifstream::out);
	std::cin.rdbuf(&l.in);
	std::cout.rdbuf(&l.out);
    }
    else if (!b && l.file.is_open())
    {
        std::cout.rdbuf(l.out.buf);
	std::cin.rdbuf(l.in.buf);
        l.file.close();
    }
  }
};

} // namespace

/// engine_info() returns the full name of the current Stockfish version. This
/// will be either "Stockfish <Tag> DD-MM-YY" (where DD-MM-YY is the date when
/// the program was compiled) or "Stockfish <Version>", depending on whether
/// Version is empty.

const std::string engine_info(bool to_uci) {

  const std::string months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");
  std::string month, day, year;
  std::stringstream ss, date(__DATE__); // From compiler, format is "Sep 21 2008"

  ss << "Stockfish " << Version << std::setfill('0');

  if (Version.empty())
  {
      date >> month >> day >> year;
      ss << std::setw(2) << day << std::setw(2)
	 << (1 + months.find(month) / 4) << year.substr(2);
  }

  ss << (Is64Bit ? " 64" : "")
     << (HasPext ? " BMI2" : (HasPopCnt ? " POPCNT" : ""))
     << (to_uci  ? "\nid author ": " by ")
     << "T. Romstad, M. Costalba, J. Kiiski, G. Linscott";

  return ss.str();
}


/// Debug functions used mainly to collect run-time statistics
static int64_t hits[2], means[2];

void dbg_hit_on(bool b) { ++hits[0]; if (b) ++hits[1]; }
void dbg_hit_on(bool c, bool b) { if (c) dbg_hit_on(b); }
void dbg_mean_of(int v) { ++means[0]; means[1] += v; }

void dbg_print() {

  if (hits[0])
      std::cerr << "Total " << hits[0] << " Hits " << hits[1]
		<< " hit rate (%) " << 100 * hits[1] / hits[0] << std::endl;

  if (means[0])
      std::cerr << "Total " << means[0] << " Mean "
		<< (double)means[1] / means[0] << std::endl;
}


/// Used to serialize access to std::cout to avoid multiple threads writing at
/// the same time.

std::ostream& operator<<(std::ostream& os, SyncCout sc) {

  static Mutex m;

  if (sc == IO_LOCK)
      m.lock();

  if (sc == IO_UNLOCK)
      m.unlock();

  return os;
}


/// Trampoline helper to avoid moving Logger to misc.h
void start_logger(bool b) { Logger::start(b); }


/// prefetch() preloads the given address in L1/L2 cache. This is a non-blocking
/// function that doesn't stall the CPU waiting for data to be loaded from memory,
/// which can be quite slow.
#ifdef NO_PREFETCH

void prefetch(void*) {}

#else

void prefetch(void* addr) {

#  if defined(__INTEL_COMPILER)
   // This hack prevents prefetches from being optimized away by
   // Intel compiler. Both MSVC and gcc seem not be affected by this.
   __asm__ ("");
#  endif

#  if defined(__INTEL_COMPILER) || defined(_MSC_VER)
  _mm_prefetch((char*)addr, _MM_HINT_T0);
#  else
  __builtin_prefetch(addr);
#  endif
}

#endif
