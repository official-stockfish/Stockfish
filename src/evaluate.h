/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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

#if !defined(EVALUATE_H_INCLUDED)
#define EVALUATE_H_INCLUDED

#include "material.h"
#include "pawns.h"
#include "types.h"

class Position;

namespace Eval {

// Struct Eval::Info contains various information computed and collected
// by the evaluation functions.
struct Info {

  // Pointers to material and pawn hash table entries
  Material::Entry* mi;
  Pawns::Entry* pi;

  // attackedBy[color][piece type] is a bitboard representing all squares
  // attacked by a given color and piece type, attackedBy[color][ALL_PIECES]
  // contains all squares attacked by the given color.
  Bitboard attackedBy[COLOR_NB][PIECE_TYPE_NB];

  // kingRing[color] is the zone around the king which is considered
  // by the king safety evaluation. This consists of the squares directly
  // adjacent to the king, and the three (or two, for a king on an edge file)
  // squares two ranks in front of the king. For instance, if black's king
  // is on g8, kingRing[BLACK] is a bitboard containing the squares f8, h8,
  // f7, g7, h7, f6, g6 and h6.
  Bitboard kingRing[COLOR_NB];

  // kingAttackersCount[color] is the number of pieces of the given color
  // which attack a square in the kingRing of the enemy king.
  int kingAttackersCount[COLOR_NB];

  // kingAttackersWeight[color] is the sum of the "weight" of the pieces of the
  // given color which attack a square in the kingRing of the enemy king. The
  // weights of the individual piece types are given by the variables
  // QueenAttackWeight, RookAttackWeight, BishopAttackWeight and
  // KnightAttackWeight in evaluate.cpp
  int kingAttackersWeight[COLOR_NB];

  // kingAdjacentZoneAttacksCount[color] is the number of attacks to squares
  // directly adjacent to the king of the given color. Pieces which attack
  // more than one square are counted multiple times. For instance, if black's
  // king is on g8 and there's a white knight on g5, this knight adds
  // 2 to kingAdjacentZoneAttacksCount[BLACK].
  int kingAdjacentZoneAttacksCount[COLOR_NB];
};

extern void init();
extern Value evaluate(const Position& pos, Value& margin, Info* ei);
extern std::string trace(const Position& pos);

}

#endif // !defined(EVALUATE_H_INCLUDED)
