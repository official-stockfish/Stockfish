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


#if !defined(DIRECTION_H_INCLUDED)
#define DIRECTION_H_INCLUDED

////
//// Includes
////

#include "square.h"
#include "types.h"


////
//// Types
////

enum Direction {
  DIR_E = 0, DIR_N = 1, DIR_NE = 2, DIR_NW = 3, DIR_NONE = 4
};

enum SignedDirection {
  SIGNED_DIR_E  = 0, SIGNED_DIR_W  = 1,
  SIGNED_DIR_N  = 2, SIGNED_DIR_S  = 3,
  SIGNED_DIR_NE = 4, SIGNED_DIR_SW = 5,
  SIGNED_DIR_NW = 6, SIGNED_DIR_SE = 7,
  SIGNED_DIR_NONE = 8
};


////
//// Variables
////

extern uint8_t DirectionTable[64][64];
extern uint8_t SignedDirectionTable[64][64];


////
//// Inline functions
////

inline void operator++ (Direction& d, int) {
  d = Direction(int(d) + 1);
}

inline void operator++ (SignedDirection& d, int) {
  d = SignedDirection(int(d) + 1);
}

inline Direction direction_between_squares(Square s1, Square s2) {
  return Direction(DirectionTable[s1][s2]);
}

inline SignedDirection signed_direction_between_squares(Square s1, Square s2) {
  return SignedDirection(SignedDirectionTable[s1][s2]);
}

inline int direction_is_diagonal(Square s1, Square s2) {
  return DirectionTable[s1][s2] & 2;
}

inline bool direction_is_straight(Square s1, Square s2) {
  return DirectionTable[s1][s2] < 2;
}

////
//// Prototypes
////

extern void init_direction_table();


#endif // !defined(DIRECTION_H_INCLUDED)
