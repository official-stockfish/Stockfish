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


#if !defined(EVALUATE_H_INCLUDED)
#define EVALUATE_H_INCLUDED

////
//// Includes
////

#include <iostream>

#include "material.h"
#include "pawns.h"


////
//// Types
////


/// The EvalInfo struct contains various information computed and collected
/// by the evaluation function. An EvalInfo object is passed as one of the
/// arguments to the evaluation function, and the search can make use of its
/// contents to make intelligent search decisions.
///
/// At the moment, this is not utilized very much: The only part of the
/// EvalInfo object which is used by the search is futilityMargin.
class Position;

struct EvalInfo {

  EvalInfo() { kingDanger[0] = kingDanger[1] = Value(0); }

  // Middle game and endgame evaluations
  Score value;

  // Pointers to material and pawn hash table entries
  MaterialInfo* mi;
  PawnInfo* pi;

  // attackedBy[color][piece type] is a bitboard representing all squares
  // attacked by a given color and piece type, attackedBy[color][0] contains
  // all squares attacked by the given color.
  Bitboard attackedBy[2][8];
  Bitboard attacked_by(Color c) const { return attackedBy[c][0]; }
  Bitboard attacked_by(Color c, PieceType pt) const { return attackedBy[c][pt]; }

  // kingZone[color] is the zone around the enemy king which is considered
  // by the king safety evaluation. This consists of the squares directly
  // adjacent to the king, and the three (or two, for a king on an edge file)
  // squares two ranks in front of the king. For instance, if black's king
  // is on g8, kingZone[WHITE] is a bitboard containing the squares f8, h8,
  // f7, g7, h7, f6, g6 and h6.
  Bitboard kingZone[2];

  // kingAttackersCount[color] is the number of pieces of the given color
  // which attack a square in the kingZone of the enemy king.
  int kingAttackersCount[2];

  // kingAttackersWeight[color] is the sum of the "weight" of the pieces of the
  // given color which attack a square in the kingZone of the enemy king. The
  // weights of the individual piece types are given by the variables
  // QueenAttackWeight, RookAttackWeight, BishopAttackWeight and
  // KnightAttackWeight in evaluate.cpp
  int kingAttackersWeight[2];

  // kingAdjacentZoneAttacksCount[color] is the number of attacks to squares
  // directly adjacent to the king of the given color. Pieces which attack
  // more than one square are counted multiple times. For instance, if black's
  // king is on g8 and there's a white knight on g5, this knight adds
  // 2 to kingAdjacentZoneAttacksCount[BLACK].
  int kingAdjacentZoneAttacksCount[2];

  // Middle game and endgame mobility scores
  Score mobility;

  // Value of the danger for the king of the given color
  Value kingDanger[2];
};


////
//// Prototypes
////

extern Value evaluate(const Position& pos, EvalInfo& ei);
extern void init_eval(int threads);
extern void quit_eval();
extern void read_weights(Color sideToMove);


#endif // !defined(EVALUATE_H_INCLUDED)
