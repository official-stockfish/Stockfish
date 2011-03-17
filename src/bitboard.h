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

#if !defined(BITBOARD_H_INCLUDED)
#define BITBOARD_H_INCLUDED

#include "types.h"

const Bitboard EmptyBoardBB = 0;

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

extern Bitboard SquaresByColorBB[2];
extern Bitboard FileBB[8];
extern Bitboard NeighboringFilesBB[8];
extern Bitboard ThisAndNeighboringFilesBB[8];
extern Bitboard RankBB[8];
extern Bitboard InFrontBB[2][8];

extern Bitboard SetMaskBB[65];
extern Bitboard ClearMaskBB[65];

extern Bitboard StepAttacksBB[16][64];
extern Bitboard BetweenBB[64][64];

extern Bitboard SquaresInFrontMask[2][64];
extern Bitboard PassedPawnMask[2][64];
extern Bitboard AttackSpanMask[2][64];

extern const uint64_t RMult[64];
extern const int RShift[64];
extern Bitboard RMask[64];
extern int RAttackIndex[64];
extern Bitboard RAttacks[0x19000];

extern const uint64_t BMult[64];
extern const int BShift[64];
extern Bitboard BMask[64];
extern int BAttackIndex[64];
extern Bitboard BAttacks[0x1480];

extern Bitboard BishopPseudoAttacks[64];
extern Bitboard RookPseudoAttacks[64];
extern Bitboard QueenPseudoAttacks[64];

extern uint8_t BitCount8Bit[256];


/// Functions for testing whether a given bit is set in a bitboard, and for
/// setting and clearing bits.

inline Bitboard bit_is_set(Bitboard b, Square s) {
  return b & SetMaskBB[s];
}

inline void set_bit(Bitboard *b, Square s) {
  *b |= SetMaskBB[s];
}

inline void clear_bit(Bitboard *b, Square s) {
  *b &= ClearMaskBB[s];
}


/// Functions used to update a bitboard after a move. This is faster
/// then calling a sequence of clear_bit() + set_bit()

inline Bitboard make_move_bb(Square from, Square to) {
  return SetMaskBB[from] | SetMaskBB[to];
}

inline void do_move_bb(Bitboard *b, Bitboard move_bb) {
  *b ^= move_bb;
}


/// rank_bb() and file_bb() take a file or a square as input and return
/// a bitboard representing all squares on the given file or rank.

inline Bitboard rank_bb(Rank r) {
  return RankBB[r];
}

inline Bitboard rank_bb(Square s) {
  return RankBB[square_rank(s)];
}

inline Bitboard file_bb(File f) {
  return FileBB[f];
}

inline Bitboard file_bb(Square s) {
  return FileBB[square_file(s)];
}


/// neighboring_files_bb takes a file or a square as input and returns a
/// bitboard representing all squares on the neighboring files.

inline Bitboard neighboring_files_bb(File f) {
  return NeighboringFilesBB[f];
}

inline Bitboard neighboring_files_bb(Square s) {
  return NeighboringFilesBB[square_file(s)];
}


/// this_and_neighboring_files_bb takes a file or a square as input and returns
/// a bitboard representing all squares on the given and neighboring files.

inline Bitboard this_and_neighboring_files_bb(File f) {
  return ThisAndNeighboringFilesBB[f];
}

inline Bitboard this_and_neighboring_files_bb(Square s) {
  return ThisAndNeighboringFilesBB[square_file(s)];
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
  return InFrontBB[c][square_rank(s)];
}


/// Functions for computing sliding attack bitboards. rook_attacks_bb(),
/// bishop_attacks_bb() and queen_attacks_bb() all take a square and a
/// bitboard of occupied squares as input, and return a bitboard representing
/// all squares attacked by a rook, bishop or queen on the given square.

#if defined(IS_64BIT)

inline Bitboard rook_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = blockers & RMask[s];
  return RAttacks[RAttackIndex[s] + ((b * RMult[s]) >> RShift[s])];
}

inline Bitboard bishop_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = blockers & BMask[s];
  return BAttacks[BAttackIndex[s] + ((b * BMult[s]) >> BShift[s])];
}

#else // if !defined(IS_64BIT)

inline Bitboard rook_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = blockers & RMask[s];
  return RAttacks[RAttackIndex[s] +
        (unsigned(int(b) * int(RMult[s]) ^ int(b >> 32) * int(RMult[s] >> 32)) >> RShift[s])];
}

inline Bitboard bishop_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = blockers & BMask[s];
  return BAttacks[BAttackIndex[s] +
        (unsigned(int(b) * int(BMult[s]) ^ int(b >> 32) * int(BMult[s] >> 32)) >> BShift[s])];
}

#endif

inline Bitboard queen_attacks_bb(Square s, Bitboard blockers) {
  return rook_attacks_bb(s, blockers) | bishop_attacks_bb(s, blockers);
}


/// squares_between returns a bitboard representing all squares between
/// two squares.  For instance, squares_between(SQ_C4, SQ_F7) returns a
/// bitboard with the bits for square d5 and e6 set.  If s1 and s2 are not
/// on the same line, file or diagonal, EmptyBoardBB is returned.

inline Bitboard squares_between(Square s1, Square s2) {
  return BetweenBB[s1][s2];
}


/// squares_in_front_of takes a color and a square as input, and returns a
/// bitboard representing all squares along the line in front of the square,
/// from the point of view of the given color. Definition of the table is:
/// SquaresInFrontOf[c][s] = in_front_bb(c, s) & file_bb(s)

inline Bitboard squares_in_front_of(Color c, Square s) {
  return SquaresInFrontMask[c][s];
}


/// passed_pawn_mask takes a color and a square as input, and returns a
/// bitboard mask which can be used to test if a pawn of the given color on
/// the given square is a passed pawn. Definition of the table is:
/// PassedPawnMask[c][s] = in_front_bb(c, s) & this_and_neighboring_files_bb(s)

inline Bitboard passed_pawn_mask(Color c, Square s) {
  return PassedPawnMask[c][s];
}


/// attack_span_mask takes a color and a square as input, and returns a bitboard
/// representing all squares that can be attacked by a pawn of the given color
/// when it moves along its file starting from the given square. Definition is:
/// AttackSpanMask[c][s] = in_front_bb(c, s) & neighboring_files_bb(s);

inline Bitboard attack_span_mask(Color c, Square s) {
  return AttackSpanMask[c][s];
}


/// squares_aligned returns true if the squares s1, s2 and s3 are aligned
/// either on a straight or on a diagonal line.

inline bool squares_aligned(Square s1, Square s2, Square s3) {
  return  (BetweenBB[s1][s2] | BetweenBB[s1][s3] | BetweenBB[s2][s3])
        & ((1ULL << s1) | (1ULL << s2) | (1ULL << s3));
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
#else

FORCE_INLINE Square first_1(Bitboard b) { // Assembly code by Heinz van Saanen
  Bitboard dummy;
  __asm__("bsfq %1, %0": "=r"(dummy): "rm"(b) );
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
extern Square pop_1st_bit(Bitboard* b);

#endif


extern void print_bitboard(Bitboard b);
extern void init_bitboards();

#endif // !defined(BITBOARD_H_INCLUDED)
