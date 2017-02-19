/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include <algorithm>

#include "types.h"

Value PieceValue[PHASE_NB][PIECE_NB] = {
  { VALUE_ZERO, PawnValueMg, KnightValueMg, BishopValueMg, RookValueMg, QueenValueMg },
  { VALUE_ZERO, PawnValueEg, KnightValueEg, BishopValueEg, RookValueEg, QueenValueEg }
};

namespace PSQT {

#define S(mg, eg) make_score(mg, eg)

// Bonus[PieceType][Square / 2] contains Piece-Square scores. For each piece
// type on a given square a (middlegame, endgame) score pair is assigned. Table
// is defined for files A..D and white side: it is symmetric for black side and
// second half of the files.
const Score Bonus[][RANK_NB][int(FILE_NB) / 2] = {
  { },
  { // Pawn
   { S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0) },
   { S(-11, 7), S(  6,-4), S(  7, 8), S( 3,-2) },
   { S(-18,-4), S( -2,-5), S( 19, 5), S(24, 4) },
   { S(-17, 3), S( -9, 3), S( 20,-8), S(35,-3) },
   { S( -6, 8), S(  5, 9), S(  3, 7), S(21,-6) },
   { S( -6, 8), S( -8,-5), S( -6, 2), S(-2, 4) },
   { S( -4, 3), S( 20,-9), S( -8, 1), S(-4,18) }
  },
  { // Knight
   { S(-121, -77), S(-74,-62), S(-58,-26), S(-51,  6) },
   { S( -61, -49), S(-21,-35), S(  1,  3), S( 12, 29) },
   { S( -49, -30), S(  0,-19), S( 22, 12), S( 31, 48) },
   { S(  -3, -21), S( 40, -5), S( 65, 27), S( 69, 58) },
   { S(  -4, -26), S( 38, -5), S( 60, 22), S( 72, 61) },
   { S(  11, -35), S( 57,-18), S( 78, 12), S( 93, 47) },
   { S( -40, -44), S(  5,-30), S( 27, -4), S( 36, 33) },
   { S(-173, -90), S(-44,-70), S(-20,-30), S( -7,  7) }
  },
  { // Bishop
   { S(-42,-44), S(-11,-16), S(-23,-22), S(-32, -4) },
   { S(-18,-19), S( 22,  7), S( 14,  1), S(  3, 19) },
   { S( -7, -8), S( 29, 15), S( 23, 11), S( 13, 32) },
   { S( -9,-12), S( 30, 11), S( 23,  9), S( 12, 31) },
   { S( -9,-12), S( 26, 10), S( 18,  7), S( 11, 27) },
   { S(-15,-11), S( 18, 11), S( 14, 14), S(  4, 25) },
   { S(-21,-20), S( 19,  3), S(  8,  2), S(  0, 20) },
   { S(-33,-41), S( -9,-18), S(-17,-22), S(-27, -3) }
  },
  { // Rook
   { S(-17, 0), S( -8, 0), S( -8, 0), S(-1, 0) },
   { S(-13, 0), S(  0, 0), S(  5, 0), S( 0, 0) },
   { S(-13, 0), S( -1, 0), S(  4, 0), S(10, 0) },
   { S(-14, 0), S(  2, 0), S(  7, 0), S(10, 0) },
   { S(-14, 0), S(  1, 0), S(  8, 0), S( 9, 0) },
   { S(-13, 0), S(  1, 0), S(  8, 0), S(10, 0) },
   { S( -4, 0), S( 12, 0), S( 16, 0), S(20, 0) },
   { S(-15, 0), S( -7, 0), S( -3, 0), S( 3, 0) }
  },
  { // Queen
   { S(-3,-46), S(-6,-33), S(-7,-17), S(-4, -5) },
   { S(-7,-34), S( 3, -6), S( 6,  3), S( 5, 20) },
   { S(-5,-15), S( 3,  7), S( 6, 17), S( 6, 29) },
   { S(-4, -5), S( 5, 19), S( 7, 33), S( 4, 41) },
   { S(-6, -3), S( 6, 19), S( 5, 34), S( 4, 47) },
   { S(-5,-16), S( 3,  8), S( 5, 13), S( 7, 27) },
   { S(-5,-30), S( 4, -6), S( 4,  3), S( 3, 17) },
   { S(-4,-51), S(-7,-30), S(-4,-21), S(-3, -6) }
  },
  { // King
   { S(260,  0), S(313, 48), S(263, 75), S(188, 84) },
   { S(258, 42), S(298, 91), S(232,142), S(174,131) },
   { S(195, 81), S(240,136), S(171,167), S(105,163) },
   { S(173,103), S(181,166), S(144,166), S(106,176) },
   { S(146,104), S(174,159), S(112,196), S( 63,199) },
   { S(116, 90), S(157,150), S( 82,171), S( 39,169) },
   { S( 85, 44), S(127, 93), S( 62,114), S( 17,133) },
   { S( 63,  2), S( 89, 48), S( 47, 73), S(  0, 83) }
  }
};

#undef S

Score psq[PIECE_NB][SQUARE_NB];

// init() initializes piece-square tables: the white halves of the tables are
// copied from Bonus[] adding the piece value, then the black halves of the
// tables are initialized by flipping and changing the sign of the white scores.
void init() {

  for (Piece pc = W_PAWN; pc <= W_KING; ++pc)
  {
      PieceValue[MG][~pc] = PieceValue[MG][pc];
      PieceValue[EG][~pc] = PieceValue[EG][pc];

      Score v = make_score(PieceValue[MG][pc], PieceValue[EG][pc]);

      for (Square s = SQ_A1; s <= SQ_H8; ++s)
      {
          File f = std::min(file_of(s), FILE_H - file_of(s));
          psq[ pc][ s] = v + Bonus[pc][rank_of(s)][f];
          psq[~pc][~s] = -psq[pc][s];
      }
  }
}

} // namespace PSQT
