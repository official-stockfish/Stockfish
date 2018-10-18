/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2018 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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
constexpr Score Bonus[][RANK_NB][int(FILE_NB) / 2] = {
  { },
  { // Pawn
   { S(  0, 0), S(  0,  0), S(  0, 0), S( 0, 0) },
   { S(-11,-3), S(  7, -1), S(  7, 7), S(17, 2) },
   { S(-16,-2), S( -3,  2), S( 23, 6), S(23,-1) },
   { S(-14, 7), S( -7, -4), S( 20,-8), S(24, 2) },
   { S( -5,13), S( -2, 10), S( -1,-1), S(12,-8) },
   { S(-11,16), S(-12,  6), S( -2, 1), S( 4,16) },
   { S( -2, 1), S( 20,-12), S(-10, 6), S(-2,25) }
  },
  { // Knight
   { S(-161+44,-105+38), S(-96+44,-82+38), S(-80+44,-46+38), S(-73+44,-14+38) },
   { S( -83+44, -69+38), S(-43+44,-54+38), S(-21+44,-17+38), S(-10+44,  9+38) },
   { S( -71+44, -50+38), S(-22+44,-39+38), S(  0+44, -7+38), S(  9+44, 28+38) },
   { S( -25+44, -41+38), S( 18+44,-25+38), S( 43+44,  6+38), S( 47+44, 38+38) },
   { S( -26+44, -46+38), S( 16+44,-25+38), S( 38+44,  3+38), S( 50+44, 40+38) },
   { S( -11+44, -54+38), S( 37+44,-38+38), S( 56+44, -7+38), S( 65+44, 27+38) },
   { S( -63+44, -65+38), S(-19+44,-50+38), S(  5+44,-24+38), S( 14+44, 13+38) },
   { S(-195+44,-109+38), S(-67+44,-89+38), S(-42+44,-50+38), S(-29+44,-13+38) }
  },
  { // Bishop
   { S(-49+73,-58+70), S(- 7+73,-31+70), S(-10+73,-37+70), S(-34+73,-19+70) },
   { S(-24+73,-34+70), S(  9+73, -9+70), S( 15+73,-14+70), S(  1+73,  4+70) },
   { S( -9+73,-23+70), S( 22+73,  0+70), S( -3+73, -3+70), S( 12+73, 16+70) },
   { S(  4+73,-26+70), S(  9+73, -3+70), S( 18+73, -5+70), S( 40+73, 16+70) },
   { S( -8+73,-26+70), S( 27+73, -4+70), S( 13+73, -7+70), S( 30+73, 14+70) },
   { S(-17+73,-24+70), S( 14+73, -2+70), S( -6+73,  0+70), S(  6+73, 13+70) },
   { S(-19+73,-34+70), S(-13+73,-10+70), S(  7+73,-12+70), S(-11+73,  6+70) },
   { S(-47+73,-55+70), S( -7+73,-32+70), S(-17+73,-36+70), S(-29+73,-17+70) }
  },
  { // Rook
   { S(-25, 0+142), S(-16, 0+142), S(-16, 0+142), S(-9, 0+142) },
   { S(-21, 0+142), S( -8, 0+142), S( -3, 0+142), S( 0, 0+142) },
   { S(-21, 0+142), S( -9, 0+142), S( -4, 0+142), S( 2, 0+142) },
   { S(-22, 0+142), S( -6, 0+142), S( -1, 0+142), S( 2, 0+142) },
   { S(-22, 0+142), S( -7, 0+142), S(  0, 0+142), S( 1, 0+142) },
   { S(-21, 0+142), S( -7, 0+142), S(  0, 0+142), S( 2, 0+142) },
   { S(-12, 0+142), S(  4, 0+142), S(  8, 0+142), S(12, 0+142) },
   { S(-23, 0+142), S(-15, 0+142), S(-11, 0+142), S(-5, 0+142) }
  },
  { // Queen
   { S( 0+52,-71+48), S(-4+52,-56+48), S(-3+52,-42+48), S(-1+52,-29+48) },
   { S(-4+52,-56+48), S( 6+52,-30+48), S( 9+52,-21+48), S( 8+52, -5+48) },
   { S(-2+52,-39+48), S( 6+52,-17+48), S( 9+52, -8+48), S( 9+52,  5+48) },
   { S(-1+52,-29+48), S( 8+52, -5+48), S(10+52,  9+48), S( 7+52, 19+48) },
   { S(-3+52,-27+48), S( 9+52, -5+48), S( 8+52, 10+48), S( 7+52, 21+48) },
   { S(-2+52,-40+48), S( 6+52,-16+48), S( 8+52,-10+48), S(10+52,  3+48) },
   { S(-2+52,-55+48), S( 7+52,-30+48), S( 7+52,-21+48), S( 6+52, -6+48) },
   { S(-1+52,-74+48), S(-4+52,-55+48), S(-1+52,-43+48), S( 0+52,-30+48) }
  },
  { // King
   { S(272,  0), S(325, 41), S(273, 80), S(190, 93) },
   { S(277, 57), S(305, 98), S(241,138), S(183,131) },
   { S(198, 86), S(253,138), S(168,165), S(120,173) },
   { S(169,103), S(191,152), S(136,168), S(108,169) },
   { S(145, 98), S(176,166), S(112,197), S(69, 194) },
   { S(122, 87), S(159,164), S(85, 174), S(36, 189) },
   { S(87,  40), S(120, 99), S(64, 128), S(25, 141) },
   { S(64,   5), S(87,  60), S(49,  75), S(0,   75) }
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

      Score score = make_score(PieceValue[MG][pc], PieceValue[EG][pc]);

      for (Square s = SQ_A1; s <= SQ_H8; ++s)
      {
          File f = std::min(file_of(s), ~file_of(s));
          psq[ pc][ s] = score + Bonus[pc][rank_of(s)][f];
          psq[~pc][~s] = -psq[pc][s];
      }
  }
}

} // namespace PSQT
