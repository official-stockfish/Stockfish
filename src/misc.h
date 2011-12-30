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

#if !defined(MISC_H_INCLUDED)
#define MISC_H_INCLUDED

#include <fstream>
#include <string>

#include "lock.h"
#include "types.h"

extern const std::string engine_info(bool to_uci = false);
extern int system_time();
extern int cpu_count();
extern void timed_wait(WaitCondition*, Lock*, int);
extern void prefetch(char* addr);

extern void dbg_hit_on(bool b);
extern void dbg_hit_on_c(bool c, bool b);
extern void dbg_mean_of(int v);
extern void dbg_print();

class Position;
extern Move move_from_uci(const Position& pos, const std::string& str);
extern const std::string move_to_uci(Move m, bool chess960);
extern const std::string move_to_san(Position& pos, Move m);

struct Log : public std::ofstream {
  Log(const std::string& f = "log.txt") : std::ofstream(f.c_str(), std::ios::out | std::ios::app) {}
 ~Log() { if (is_open()) close(); }
};

#endif // !defined(MISC_H_INCLUDED)
