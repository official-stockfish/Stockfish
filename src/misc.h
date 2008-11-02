/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008 Marco Costalba

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


////
//// Includes
////

#include <string>


////
//// Constants
////


/// Version number.  If this is left empty, the current date (in the format
/// YYMMDD) is used as a version number.

const std::string EngineVersion = "1.0";


////
//// Macros
////

#define Min(x, y) (((x) < (y))? (x) : (y))
#define Max(x, y) (((x) < (y))? (y) : (x))


////
//// Prototypes
////

extern const std::string engine_name();
extern int get_system_time();
extern int cpu_count();
extern int Bioskey();

////
//// Debug
////
extern long dbg_cnt0;
extern long dbg_cnt1;

inline void dbg_hit_on(bool b) { dbg_cnt0++; if (b) dbg_cnt1++; }
inline void dbg_hit_on_c(bool c, bool b) { if (c) dbg_hit_on(b); }

inline void dbg_before() { dbg_cnt0++; }
inline void dbg_after() { dbg_cnt1++; }

inline void dbg_mean_of(int v) { dbg_cnt0++; dbg_cnt1 += v; }

extern void dbg_print_hit_rate();
extern void dbg_print_mean();

extern bool dbg_show_mean;
extern bool dbg_show_hit_rate;

#endif // !defined(MISC_H_INCLUDED)
