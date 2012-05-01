/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

extern void init();
extern void print(Bitboard b);

}

CACHE_LINE_ALIGNMENT

extern Bitboard RMasks[64];
extern Bitboard RMagics[64];
extern Bitboard* RAttacks[64];
extern unsigned RShifts[64];

extern Bitboard BMasks[64];
extern Bitboard BMagics[64];
extern Bitboard* BAttacks[64];
extern unsigned BShifts[64];

extern Bitboard SquareBB[64];
extern Bitboard FileBB[8];
extern Bitboard RankBB[8];
extern Bitboard AdjacentFilesBB[8];
extern Bitboard ThisAndAdjacentFilesBB[8];
extern Bitboard InFrontBB[2][8];
extern Bitboard StepAttacksBB[16][64];
extern Bitboard BetweenBB[64][64];
extern Bitboard ForwardBB[2][64];
extern Bitboard PassedPawnMask[2][64];
extern Bitboard AttackSpanMask[2][64];
extern Bitboard PseudoAttacks[6][64];


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


/// more_than_one() returns true if in 'b' there is more than one bit set

inline bool more_than_one(Bitboard b) {
  return b & (b - 1);
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


/// adjacent_files_bb takes a file as input and returns a bitboard representing
/// all squares on the adjacent files.

inline Bitboard adjacent_files_bb(File f) {
  return AdjacentFilesBB[f];
}


/// this_and_adjacent_files_bb takes a file as input and returns a bitboard
/// representing all squares on the given and adjacent files.

inline Bitboard this_and_adjacent_files_bb(File f) {
  return ThisAndAdjacentFilesBB[f];
}


/// in_front_bb() takes a color and a rank or square as input, and returns a
/// bitboard representing all the squares on all ranks in front of the rank
/// (or square), from the given color's point of view.  For instance,
/// in_front_bb(WHITE, RANK_5) will give all squares on ranks 6, 7 and 8, while
/// in_front_bb(BLACK, SQ_D3) will give all squares on ranks 1 and 2.

inline Bitboard in_front_bb(Color c, Rank r) {
  return InFrontBB[c][r];
}

inline Bitboard in_front_bb(Color c, Square s) {
  return InFrontBB[c][rank_of(s)];
}


/// between_bb returns a bitboard representing all squares between two squares.
/// For instance, between_bb(SQ_C4, SQ_F7) returns a bitboard with the bits for
/// square d5 and e6 set.  If s1 and s2 are not on the same line, file or diagonal,
/// 0 is returned.

inline Bitboard between_bb(Square s1, Square s2) {
  return BetweenBB[s1][s2];
}


/// forward_bb takes a color and a square as input, and returns a bitboard
/// representing all squares along the line in front of the square, from the
/// point of view of the given color. Definition of the table is:
/// ForwardBB[c][s] = in_front_bb(c, s) & file_bb(s)

inline Bitboard forward_bb(Color c, Square s) {
  return ForwardBB[c][s];
}


/// passed_pawn_mask takes a color and a square as input, and returns a
/// bitboard mask which can be used to test if a pawn of the given color on
/// the given square is a passed pawn. Definition of the table is:
/// PassedPawnMask[c][s] = in_front_bb(c, s) & this_and_adjacent_files_bb(s)

inline Bitboard passed_pawn_mask(Color c, Square s) {
  return PassedPawnMask[c][s];
}


/// attack_span_mask takes a color and a square as input, and returns a bitboard
/// representing all squares that can be attacked by a pawn of the given color
/// when it moves along its file starting from the given square. Definition is:
/// AttackSpanMask[c][s] = in_front_bb(c, s) & adjacent_files_bb(s);

inline Bitboard attack_span_mask(Color c, Square s) {
  return AttackSpanMask[c][s];
}


/// squares_aligned returns true if the squares s1, s2 and s3 are aligned
/// either on a straight or on a diagonal line.

inline bool squares_aligned(Square s1, Square s2, Square s3) {
  return  (BetweenBB[s1][s2] | BetweenBB[s1][s3] | BetweenBB[s2][s3])
        & (     SquareBB[s1] |      SquareBB[s2] |      SquareBB[s3]);
}


/// same_color_squares() returns a bitboard representing all squares with
/// the same color of the given square.

inline Bitboard same_color_squares(Square s) {
  return Bitboard(0xAA55AA55AA55AA55ULL) & s ?  0xAA55AA55AA55AA55ULL
                                             : ~0xAA55AA55AA55AA55ULL;
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


/// first_1() finds the least significant nonzero bit in a nonzero bitboard.
/// pop_1st_bit() finds and clears the least significant nonzero bit in a
/// nonzero bitboard.

#if defined(USE_BSFQ)

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)

FORCE_INLINE Square first_1(Bitboard b) {
  unsigned long index;
  _BitScanForward64(&index, b);
  return (Square) index;
}

FORCE_INLINE Square last_1(Bitboard b) {
  unsigned long index;
  _BitScanReverse64(&index, b);
  return (Square) index;
}
#else

FORCE_INLINE Square first_1(Bitboard b) { // Assembly code by Heinz van Saanen
  Bitboard dummy;
  __asm__("bsfq %1, %0": "=r"(dummy): "rm"(b) );
  return (Square) dummy;
}

FORCE_INLINE Square last_1(Bitboard b) {
  Bitboard dummy;
  __asm__("bsrq %1, %0": "=r"(dummy): "rm"(b) );
  return (Square) dummy;
}
#endif

FORCE_INLINE Square pop_1st_bit(Bitboard* b) {
  const Square s = first_1(*b);
  *b &= ~(1ULL<<s);
  return s;
}

#else // if !defined(USE_BSFQ)

extern Square first_1(Bitboard b);
extern Square last_1(Bitboard b);
extern Square pop_1st_bit(Bitboard* b);

#endif

#endif // !defined(BITBOARD_H_INCLUDED)
