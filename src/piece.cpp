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

#include <cstring>

#include "piece.h"


////
//// Constants and variables
////

const int SlidingArray[18] = {
  0, 0, 0, 1, 2, 3, 0, 0, 0, 0, 0, 1, 2, 3, 0, 0, 0, 0
};

const SquareDelta Directions[16][16] = {
  {DELTA_ZERO},
  {DELTA_NW, DELTA_NE, DELTA_ZERO},
  {DELTA_SSW, DELTA_SSE, DELTA_SWW, DELTA_SEE,
   DELTA_NWW, DELTA_NEE, DELTA_NNW, DELTA_NNE, DELTA_ZERO},
  {DELTA_SE, DELTA_SW, DELTA_NE, DELTA_NW, DELTA_ZERO},
  {DELTA_S, DELTA_E, DELTA_W, DELTA_N, DELTA_ZERO},
  {DELTA_S, DELTA_E, DELTA_W, DELTA_N, 
   DELTA_SE, DELTA_SW, DELTA_NE, DELTA_NW, DELTA_ZERO},
  {DELTA_S, DELTA_E, DELTA_W, DELTA_N, 
   DELTA_SE, DELTA_SW, DELTA_NE, DELTA_NW, DELTA_ZERO},
  {DELTA_ZERO},
  {DELTA_ZERO},
  {DELTA_SW, DELTA_SE, DELTA_ZERO},
  {DELTA_SSW, DELTA_SSE, DELTA_SWW, DELTA_SEE,
   DELTA_NWW, DELTA_NEE, DELTA_NNW, DELTA_NNE, DELTA_ZERO},
  {DELTA_SE, DELTA_SW, DELTA_NE, DELTA_NW, DELTA_ZERO},
  {DELTA_S, DELTA_E, DELTA_W, DELTA_N, DELTA_ZERO},
  {DELTA_S, DELTA_E, DELTA_W, DELTA_N, 
   DELTA_SE, DELTA_SW, DELTA_NE, DELTA_NW, DELTA_ZERO},
  {DELTA_S, DELTA_E, DELTA_W, DELTA_N, 
   DELTA_SE, DELTA_SW, DELTA_NE, DELTA_NW, DELTA_ZERO},
};   

const SquareDelta PawnPush[2] = {
  DELTA_N, DELTA_S
};


////
//// Functions
////

/// Translating piece types to/from English piece letters:

static const char PieceChars[] = " pnbrqk";

char piece_type_to_char(PieceType pt, bool upcase = false) {
  return upcase? toupper(PieceChars[pt]) : PieceChars[pt];
}

PieceType piece_type_from_char(char c) {
  const char *ch = strchr(PieceChars, tolower(c));
  return ch? PieceType(ch - PieceChars) : NO_PIECE_TYPE;
}


/// piece_is_ok() and piece_type_is_ok(), for debugging:

bool piece_is_ok(Piece pc) {
  return
    piece_type_is_ok(type_of_piece(pc)) &&
    color_is_ok(color_of_piece(pc));
}

bool piece_type_is_ok(PieceType pc) {
  return pc >= PAWN && pc <= KING;
}
