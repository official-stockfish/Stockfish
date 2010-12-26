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


////
//// Includes
////

#include "square.h"

int8_t DirectionTable[64][64];


static SquareDelta direction(Square orig, Square dest) {

  const SquareDelta directions[] = {
      DELTA_E, DELTA_N, DELTA_NE, DELTA_NW, DELTA_W, DELTA_S, DELTA_SW, DELTA_SE
  };

  Square from, to;

  for (int idx = 0; idx < 8; idx++)
  {
      from = orig;
      to = from + directions[idx];

      while (to != dest && square_distance(to, from) == 1 && square_is_ok(to))
      {
          from = to;
          to += directions[idx];
      }

      if (to == dest && square_distance(from, to) == 1)
          return directions[idx];
  }
  return DELTA_NONE;
}

void init_direction_table() {

  for (Square s1 = SQ_A1; s1 <= SQ_H8; s1++)
      for (Square s2 = SQ_A1; s2 <= SQ_H8; s2++)
          DirectionTable[s1][s2] = uint8_t(direction(s1, s2));
}
