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


#if !defined(EVALUATE_H_INCLUDED)
#define EVALUATE_H_INCLUDED

////
//// Includes
////

#include "material.h"
#include "pawns.h"
#include "position.h"


////
//// Types
////

/// The EvalInfo struct contains various information computed and collected
/// by the evaluation function.  An EvalInfo object is passed as one of the
/// arguments to the evaluation function, and the search can make use of its
/// contents to make intelligent search decisions.
///
/// At the moment, this is not utilized very much:  The only part of the
/// EvalInfo object which is used by the search is futilityMargin.

struct EvalInfo {
  
  // Middle game and endgame evaluations:
  Value mgValue, egValue;

  // Pointers to material and pawn hash table entries:
  MaterialInfo *mi;
  PawnInfo *pi;

  // attackedBy[color][piece type] is a bitboard representing all squares
  // attacked by a given color and piece type.  attackedBy[color][0] contains
  // all squares attacked by the given color.
  Bitboard attackedBy[2][8];
  Bitboard attacked_by(Color c) const { return attackedBy[c][0]; }
  Bitboard attacked_by(Color c, PieceType pt) const { return attackedBy[c][pt]; }
  // attackZone[color] is the zone around the enemy king which is considered
  // by the king safety evaluation.  This consists of the squares directly
  // adjacent to the king, and the three (or two, for a king on an edge file)
  // squares two ranks in front of the king.  For instance, if black's king
  // is on g8, attackZone[WHITE] is a bitboard containing the squares f8, h8,
  // f7, g7, h7, f6, g6 and h6.
  Bitboard attackZone[2];

  // attackCount[color] is the number of pieces of the given color which
  // attack a square adjacent to the enemy king.
  int attackCount[2];

  // attackWeight[color] is the sum of the "weight" of the pieces of the given
  // color which attack a square adjacent to the enemy king.  The weights of
  // the individual piece types are given by the variables QueenAttackWeight,
  // RookAttackWeight, BishopAttackWeight and KnightAttackWeight in
  // evaluate.cpp.
  int attackWeight[2];

  // attacked[color] is the number of enemy piece attacks to squares directly
  // adjacent to the king of the given color.  Pieces which attack more
  // than one square are counted multiple times.  For instance, if black's
  // king is on g8 and there's a white knight on g5, this knight adds
  // 2 to attacked[BLACK].
  int attacked[2];

  // mateThreat[color] is a move for the given side which gives a direct mate.
  Move mateThreat[2];

  // Middle game and endgame mobility scores.
  Value mgMobility, egMobility;

  // Extra futility margin.  This is added to the standard futility margin
  // in the quiescence search.  
  Value futilityMargin;
};


////
//// Prototypes
////

extern Value evaluate(const Position &pos, EvalInfo &ei, int threadID);
extern Value quick_evaluate(const Position &pos);
extern void init_eval(int threads);
extern void quit_eval();
extern void read_weights(Color sideToMove);


#endif // !defined(EVALUATE_H_INCLUDED)
