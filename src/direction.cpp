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


////
//// Includes
////

#include "direction.h"
#include "square.h"


////
//// Variables
////

uint8_t DirectionTable[64][64];
uint8_t SignedDirectionTable[64][64];


////
//// Functions
////

void init_direction_table() {
  SquareDelta deltas[8] = {
    DELTA_E, DELTA_W, DELTA_N, DELTA_S, DELTA_NE, DELTA_SW, DELTA_NW, DELTA_SE
  };
  for(Square s1 = SQ_A1; s1 <= SQ_H8; s1++)
    for(Square s2 = SQ_A1; s2 <= SQ_H8; s2++) {
      DirectionTable[s1][s2] = uint8_t(DIR_NONE);
      SignedDirectionTable[s1][s2] = uint8_t(SIGNED_DIR_NONE);
      if(s1 == s2) continue;
      for(SignedDirection d = SIGNED_DIR_E; d <= SIGNED_DIR_SE; d++) {
        SquareDelta delta = deltas[d];
        Square s3, s4;
        for(s4 = s1 + delta, s3 = s1;
            square_distance(s4, s3) == 1 && s4 != s2 && square_is_ok(s4);
            s3 = s4, s4 += delta);
        if(s4 == s2 && square_distance(s4, s3) == 1) {
          SignedDirectionTable[s1][s2] = uint8_t(d);
          DirectionTable[s1][s2] = uint8_t(d/2);
          break;
        }
      }
    }
}
