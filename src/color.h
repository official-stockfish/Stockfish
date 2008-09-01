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


#if !defined(COLOR_H_INCLUDED)
#define COLOR_H_INCLUDED

////
//// Includes
////

#include "misc.h"


////
//// Types
////

enum Color {
  WHITE, 
  BLACK, 
  COLOR_NONE
};


////
//// Inline functions
////

inline Color operator+ (Color c, int i) { return Color(int(c) + i); }
inline void operator++ (Color &c, int i) { c = Color(int(c) + 1); }

inline Color opposite_color(Color c) {
  return Color(int(c) ^ 1);
}


////
//// Prototypes
////

extern bool color_is_ok(Color c);


#endif // !defined(COLOR_H_INCLUDED)
