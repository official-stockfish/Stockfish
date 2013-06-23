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

#if !defined(BITBOARD_H_INCLUDED)
#define BITBOARD_H_INCLUDED

#include "types.h"

namespace Bitboards {

void init();
void print(Bitboard b);

}

namespace Bitbases {

void init_kpk();
bool probe_kpk(Square wksq, Square wpsq, Square bksq, Color us);

}

const Bitboard FileABB = 0x0101010101010101ULL;
const Bitboard FileBBB = FileABB << 1;
const Bitboard FileCBB = FileABB << 2;
const Bitboard FileDBB = FileABB << 3;
const Bitboard FileEBB = FileABB << 4;
const Bitboard FileFBB = FileABB << 5;
const Bitboard FileGBB = FileABB << 6;
const Bitboard FileHBB = FileABB << 7;

const Bitboard Rank1BB = 0xFF;
const Bitboard Rank2BB = Rank1BB << (8 * 1);
const Bitboard Rank3BB = Rank1BB << (8 * 2);
const Bitboard Rank4BB = Rank1BB << (8 * 3);
const Bitboard Rank5BB = Rank1BB << (8 * 4);
const Bitboard Rank6BB = Rank1BB << (8 * 5);
const Bitboard Rank7BB = Rank1BB << (8 * 6);
const Bitboard Rank8BB = Rank1BB << (8 * 7);

CACHE_LINE_ALIGNMENT

extern Bitboard RMasks[SQUARE_NB];
extern Bitboard RMagics[SQUARE_NB];
extern Bitboard* RAttacks[SQUARE_NB];
extern unsigned RShifts[SQUARE_NB];

extern Bitboard BMasks[SQUARE_NB];
extern Bitboard BMagics[SQUARE_NB];
extern Bitboard* BAttacks[SQUARE_NB];
extern unsigned BShifts[SQUARE_NB];

extern Bitboard SquareBB[SQUARE_NB];
extern Bitboard FileBB[FILE_NB];
extern Bitboard RankBB[RANK_NB];
extern Bitboard AdjacentFilesBB[FILE_NB];
extern Bitboard InFrontBB[COLOR_NB][RANK_NB];
extern Bitboard StepAttacksBB[PIECE_NB][SQUARE_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard DistanceRingsBB[SQUARE_NB][8];
extern Bitboard ForwardBB[COLOR_NB][SQUARE_NB];
extern Bitboard PassedPawnMask[COLOR_NB][SQUARE_NB];
extern Bitboard PawnAttackSpan[COLOR_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

extern int SquareDistance[SQUARE_NB][SQUARE_NB];

const Bitboard BlackSquares = 0xAA55AA55AA55AA55ULL;

/// Overloads of bitwise operators between a Bitboard and a Square for testing
/// whether a given bit is set in a bitboard, and for setting and clearing bits.

inline Bitboard operator&(Bitboard b, Square s) {
  return b & SquareBB[s];
}

inline Bitboard& operator|=(Bitboard& b, Square s) {
  return b |= SquareBB[s];
}

inline Bitboard& operator^=(Bitboard& b, Square s) {
  return b ^= SquareBB[s];
}

inline Bitboard operator|(Bitboard b, Square s) {
  return b | SquareBB[s];
}

inline Bitboard operator^(Bitboard b, Square s) {
  return b ^ SquareBB[s];
}

inline bool more_than_one(Bitboard b) {
  return b & (b - 1);
}

inline int square_distance(Square s1, Square s2) {
  return SquareDistance[s1][s2];
}

inline int file_distance(Square s1, Square s2) {
  return abs(file_of(s1) - file_of(s2));
}

inline int rank_distance(Square s1, Square s2) {
  return abs(rank_of(s1) - rank_of(s2));
}


/// shift_bb() moves bitboard one step along direction Delta. Mainly for pawns.

template<Square Delta>
inline Bitboard shift_bb(Bitboard b) {

  return  Delta == DELTA_N  ?  b             << 8 : Delta == DELTA_S  ?  b             >> 8
        : Delta == DELTA_NE ? (b & ~FileHBB) << 9 : Delta == DELTA_SE ? (b & ~FileHBB) >> 7
        : Delta == DELTA_NW ? (b & ~FileABB) << 7 : Delta == DELTA_SW ? (b & ~FileABB) >> 9
        : 0;
}


/// rank_bb() and file_bb() take a file or a square as input and return
/// a bitboard representing all squares on the given file or rank.

inline Bitboard rank_bb(Rank r) {
  return RankBB[r];
}

inline Bitboard rank_bb(Square s) {
  return RankBB[rank_of(s)];
}

inline Bitboard file_bb(File f) {
  return FileBB[f];
}

inline Bitboard file_bb(Square s) {
  return FileBB[file_of(s)];
}


/// adjacent_files_bb() takes a file as input and returns a bitboard representing
/// all squares on the adjacent files.

inline Bitboard adjacent_files_bb(File f) {
  return AdjacentFilesBB[f];
}


/// in_front_bb() takes a color and a rank as input, and returns a bitboard
/// representing all the squares on all ranks in front of the rank, from the
/// given color's point of view. For instance, in_front_bb(BLACK, RANK_3) will
/// give all squares on ranks 1 and 2.

inline Bitboard in_front_bb(Color c, Rank r) {
  return InFrontBB[c][r];
}


/// between_bb() returns a bitboard representing all squares between two squares.
/// For instance, between_bb(SQ_C4, SQ_F7) returns a bitboard with the bits for
/// square d5 and e6 set.  If s1 and s2 are not on the same line, file or diagonal,
/// 0 is returned.

inline Bitboard between_bb(Square s1, Square s2) {
  return BetweenBB[s1][s2];
}


/// forward_bb() takes a color and a square as input, and returns a bitboard
/// representing all squares along the line in front of the square, from the
/// point of view of the given color. Definition of the table is:
/// ForwardBB[c][s] = in_front_bb(c, s) & file_bb(s)

inline Bitboard forward_bb(Color c, Square s) {
  return ForwardBB[c][s];
}


/// pawn_attack_span() takes a color and a square as input, and returns a bitboard
/// representing all squares that can be attacked by a pawn of the given color
/// when it moves along its file starting from the given square. Definition is:
/// PawnAttackSpan[c][s] = in_front_bb(c, s) & adjacent_files_bb(s);

inline Bitboard pawn_attack_span(Color c, Square s) {
  return PawnAttackSpan[c][s];
}


/// passed_pawn_mask() takes a color and a square as input, and returns a
/// bitboard mask which can be used to test if a pawn of the given color on
/// the given square is a passed pawn. Definition of the table is:
/// PassedPawnMask[c][s] = pawn_attack_span(c, s) | forward_bb(c, s)

inline Bitboard passed_pawn_mask(Color c, Square s) {
  return PassedPawnMask[c][s];
}


/// squares_aligned() returns true if the squares s1, s2 and s3 are aligned
/// either on a straight or on a diagonal line.

inline bool squares_aligned(Square s1, Square s2, Square s3) {
  return  (BetweenBB[s1][s2] | BetweenBB[s1][s3] | BetweenBB[s2][s3])
        & (     SquareBB[s1] |      SquareBB[s2] |      SquareBB[s3]);
}


/// same_color_squares() returns a bitboard representing all squares with
/// the same color of the given square.

inline Bitboard same_color_squares(Square s) {
  return BlackSquares & s ? BlackSquares : ~BlackSquares;
}


/// Functions for computing sliding attack bitboards. Function attacks_bb() takes
/// a square and a bitboard of occupied squares as input, and returns a bitboard
/// representing all squares attacked by Pt (bishop or rook) on the given square.
template<PieceType Pt>
FORCE_INLINE unsigned magic_index(Square s, Bitboard occ) {

  Bitboard* const Masks  = Pt == ROOK ? RMasks  : BMasks;
  Bitboard* const Magics = Pt == ROOK ? RMagics : BMagics;
  unsigned* const Shifts = Pt == ROOK ? RShifts : BShifts;

  if (Is64Bit)
      return unsigned(((occ & Masks[s]) * Magics[s]) >> Shifts[s]);

  unsigned lo = unsigned(occ) & unsigned(Masks[s]);
  unsigned hi = unsigned(occ >> 32) & unsigned(Masks[s] >> 32);
  return (lo * unsigned(Magics[s]) ^ hi * unsigned(Magics[s] >> 32)) >> Shifts[s];
}

template<PieceType Pt>
inline Bitboard attacks_bb(Square s, Bitboard occ) {
  return (Pt == ROOK ? RAttacks : BAttacks)[s][magic_index<Pt>(s, occ)];
}


/// lsb()/msb() finds the least/most significant bit in a nonzero bitboard.
/// pop_lsb() finds and clears the least significant bit in a nonzero bitboard.

#if defined(USE_BSFQ)

#  if defined(_MSC_VER) && !defined(__INTEL_COMPILER)

FORCE_INLINE Square lsb(Bitboard b) {
  unsigned long index;
  _BitScanForward64(&index, b);
  return (Square) index;
}

FORCE_INLINE Square msb(Bitboard b) {
  unsigned long index;
  _BitScanReverse64(&index, b);
  return (Square) index;
}

#  elif defined(__arm__)

FORCE_INLINE int lsb32(uint32_t v) {
  __asm__("rbit %0, %1" : "=r"(v) : "r"(v));
  return __builtin_clz(v);
}

FORCE_INLINE Square msb(Bitboard b) {
  return (Square) (63 - __builtin_clzll(b));
}

FORCE_INLINE Square lsb(Bitboard b) {
  return (Square) (uint32_t(b) ? lsb32(uint32_t(b)) : 32 + lsb32(uint32_t(b >> 32)));
}

#  else

FORCE_INLINE Square lsb(Bitboard b) { // Assembly code by Heinz van Saanen
  Bitboard index;
  __asm__("bsfq %1, %0": "=r"(index): "rm"(b) );
  return (Square) index;
}

FORCE_INLINE Square msb(Bitboard b) {
  Bitboard index;
  __asm__("bsrq %1, %0": "=r"(index): "rm"(b) );
  return (Square) index;
}

#  endif

FORCE_INLINE Square pop_lsb(Bitboard* b) {
  const Square s = lsb(*b);
  *b &= *b - 1;
  return s;
}

#else // if !defined(USE_BSFQ)

extern Square msb(Bitboard b);
extern Square lsb(Bitboard b);
extern Square pop_lsb(Bitboard* b);

#endif

#endif // !defined(BITBOARD_H_INCLUDED)
