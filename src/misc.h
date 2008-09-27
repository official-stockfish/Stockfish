/*
  Glaurung, a UCI chess playing engine.
  Copyright (C) 2004-2008 Tord Romstad

  Glaurung is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Glaurung is distributed in the hope that it will be useful,
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

const std::string EngineVersion = "";


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
extern void dbg_print_hit_rate();

#define dbg_hit_on(x) { dbg_cnt0++; if (x) dbg_cnt1++; }
#define dbg_hit_on_c(c, x) { if (c) dbg_hit_on(x) }

#endif // !defined(MISC_H_INCLUDED)
