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


#if !defined(PIECE_H_INCLUDED)
#define PIECE_H_INCLUDED

////
//// Includes
////

#include <string>

#include "color.h"
#include "square.h"
#include "value.h"


////
//// Types
////

enum PieceType {
  PIECE_TYPE_NONE = 0,
  PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6
};

enum Piece {
  PIECE_NONE_DARK_SQ = 0, WP = 1, WN = 2, WB = 3, WR = 4, WQ = 5, WK = 6,
  BP = 9, BN = 10, BB = 11, BR = 12, BQ = 13, BK = 14, PIECE_NONE = 16
};

ENABLE_OPERATORS_ON(PieceType);
ENABLE_OPERATORS_ON(Piece);


////
//// Constants
////

/// Important: If the material values are changed, one must also
/// adjust the piece square tables, and the method game_phase() in the
/// Position class!
///
/// Values modified by Joona Kiiski

const Value PawnValueMidgame   = Value(0x0C6);
const Value PawnValueEndgame   = Value(0x102);
const Value KnightValueMidgame = Value(0x331);
const Value KnightValueEndgame = Value(0x34E);
const Value BishopValueMidgame = Value(0x344);
const Value BishopValueEndgame = Value(0x359);
const Value RookValueMidgame   = Value(0x4F6);
const Value RookValueEndgame   = Value(0x4FE);
const Value QueenValueMidgame  = Value(0x9D9);
const Value QueenValueEndgame  = Value(0x9FE);


////
//// Inline functions
////

inline PieceType type_of_piece(Piece p)  {
  return PieceType(int(p) & 7);
}

inline Color color_of_piece(Piece p) {
  return Color(int(p) >> 3);
}

inline Piece piece_of_color_and_type(Color c, PieceType pt) {
  return Piece((int(c) << 3) | int(pt));
}

inline SquareDelta pawn_push(Color c) {
    return (c == WHITE ? DELTA_N : DELTA_S);
}

inline bool piece_type_is_ok(PieceType pt) {
  return pt >= PAWN && pt <= KING;
}

inline bool piece_is_ok(Piece p) {
  return piece_type_is_ok(type_of_piece(p)) && color_is_ok(color_of_piece(p));
}

inline char piece_type_to_char(PieceType pt) {
  return std::string(" PNBRQK")[pt];
}

inline PieceType piece_type_from_char(char c) {
  return PieceType(std::string(" PNBRQK").find(c));
}

#endif // !defined(PIECE_H_INCLUDED)
