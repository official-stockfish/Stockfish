/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include "types.h"

namespace PSQT {

#define S(mg, eg) make_score(mg, eg)

// Bonus[PieceType][Square / 2] contains Piece-Square scores. For each piece
// type on a given square a (middlegame, endgame) score pair is assigned. Table
// is defined for files A..D and white side: it is symmetric for black side and
// second half of the files.
const Score Bonus[][RANK_NB][int(FILE_NB) / 2] = {
  { },
  { // Pawn
   { S(  0, 0), S(  0, 0), S( 0, 0), S( 0, 0) },
   { S(-22, 4), S(  3,-6), S( 7, 8), S( 3,-1) },
   { S(-25,-3), S( -7,-4), S(18, 4), S(24, 5) },
   { S(-27, 1), S(-15, 2), S(15,-8), S(30,-2) },
   { S(-14, 7), S(  0,12), S(-2, 4), S(18,-3) },
   { S(-12, 8), S(-13,-5), S(-6, 1), S(-4, 7) },
   { S(-17, 1), S( 10,-9), S(-4, 1), S(-6,16) },
   { S(  0, 0), S(  0, 0), S( 0, 0), S( 0, 0) }
  },
  { // Knight
   { S(-144,-98), S(-109,-83), S(-85,-51), S(-73,-16) },
   { S( -88,-68), S( -43,-53), S(-19,-21), S( -7, 14) },
   { S( -69,-53), S( -24,-38), S(  0, -6), S( 12, 29) },
   { S( -28,-42), S(  17,-27), S( 41,  5), S( 53, 40) },
   { S( -30,-42), S(  15,-27), S( 39,  5), S( 51, 40) },
   { S( -10,-53), S(  35,-38), S( 59, -6), S( 71, 29) },
   { S( -64,-68), S( -19,-53), S(  5,-21), S( 17, 14) },
   { S(-200,-98), S( -65,-83), S(-41,-51), S(-29,-16) }
  },
  { // Bishop
   { S(-54,-65), S(-27,-42), S(-34,-44), S(-43,-26) },
   { S(-29,-43), S(  8,-20), S(  1,-22), S( -8, -4) },
   { S(-20,-33), S( 17,-10), S( 10,-12), S(  1,  6) },
   { S(-19,-35), S( 18,-12), S( 11,-14), S(  2,  4) },
   { S(-22,-35), S( 15,-12), S(  8,-14), S( -1,  4) },
   { S(-28,-33), S(  9,-10), S(  2,-12), S( -7,  6) },
   { S(-32,-43), S(  5,-20), S( -2,-22), S(-11, -4) },
   { S(-49,-65), S(-22,-42), S(-29,-44), S(-38,-26) }
  },
  { // Rook
   { S(-22, 3), S(-17, 3), S(-12, 3), S(-8, 3) },
   { S(-22, 3), S( -7, 3), S( -2, 3), S( 2, 3) },
   { S(-22, 3), S( -7, 3), S( -2, 3), S( 2, 3) },
   { S(-22, 3), S( -7, 3), S( -2, 3), S( 2, 3) },
   { S(-22, 3), S( -7, 3), S( -2, 3), S( 2, 3) },
   { S(-22, 3), S( -7, 3), S( -2, 3), S( 2, 3) },
   { S(-11, 3), S(  4, 3), S(  9, 3), S(13, 3) },
   { S(-22, 3), S(-17, 3), S(-12, 3), S(-8, 3) }
  },
  { // Queen
   { S(-2,-80), S(-2,-54), S(-2,-42), S(-2,-30) },
   { S(-2,-54), S( 8,-30), S( 8,-18), S( 8, -6) },
   { S(-2,-42), S( 8,-18), S( 8, -6), S( 8,  6) },
   { S(-2,-30), S( 8, -6), S( 8,  6), S( 8, 18) },
   { S(-2,-30), S( 8, -6), S( 8,  6), S( 8, 18) },
   { S(-2,-42), S( 8,-18), S( 8, -6), S( 8,  6) },
   { S(-2,-54), S( 8,-30), S( 8,-18), S( 8, -6) },
   { S(-2,-80), S(-2,-54), S(-2,-42), S(-2,-30) }
  },
  { // King
   { S(298, 27), S(332, 81), S(273,108), S(225,116) },
   { S(287, 74), S(321,128), S(262,155), S(214,163) },
   { S(224,111), S(258,165), S(199,192), S(151,200) },
   { S(196,135), S(230,189), S(171,216), S(123,224) },
   { S(173,135), S(207,189), S(148,216), S(100,224) },
   { S(146,111), S(180,165), S(121,192), S( 73,200) },
   { S(119, 74), S(153,128), S( 94,155), S( 46,163) },
   { S( 98, 27), S(132, 81), S( 73,108), S( 25,116) }
  }
};

#undef S

Score psq[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];

// init() initializes piece square tables: the white halves of the tables are
// copied from Bonus[] adding the piece value, then the black halves of the
// tables are initialized by flipping and changing the sign of the white scores.
void init() {

  for (PieceType pt = PAWN; pt <= KING; ++pt)
  {
      PieceValue[MG][make_piece(BLACK, pt)] = PieceValue[MG][pt];
      PieceValue[EG][make_piece(BLACK, pt)] = PieceValue[EG][pt];

      Score v = make_score(PieceValue[MG][pt], PieceValue[EG][pt]);

      for (Square s = SQ_A1; s <= SQ_H8; ++s)
      {
          int edgeDistance = file_of(s) < FILE_E ? file_of(s) : FILE_H - file_of(s);
          psq[BLACK][pt][~s] = -(psq[WHITE][pt][s] = v + Bonus[pt][rank_of(s)][edgeDistance]);
      }
  }
}

} // namespace PSQT
