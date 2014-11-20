/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#ifndef BITBOARD_H_INCLUDED
#define BITBOARD_H_INCLUDED

#include <string>

#include "types.h"

namespace Bitboards {

void init();
const std::string pretty(Bitboard bitboard);

}

namespace Bitbases {

void init_kpk();
bool probe_kpk(Square whiteKingSquare, Square whitePawnSquare, Square blackKingSquare, Color us);

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

extern Bitboard RookMasks[SQUARE_NB];
extern Bitboard RookMagics[SQUARE_NB];
extern Bitboard* RookAttacks[SQUARE_NB];
extern unsigned RookShifts[SQUARE_NB];

extern Bitboard BishopMasks[SQUARE_NB];
extern Bitboard BishopMagics[SQUARE_NB];
extern Bitboard* BishopAttacks[SQUARE_NB];
extern unsigned BishopShifts[SQUARE_NB];

extern Bitboard SquareBB[SQUARE_NB];
extern Bitboard FileBB[FILE_NB];
extern Bitboard RankBB[RANK_NB];
extern Bitboard AdjacentFilesBB[FILE_NB];
extern Bitboard InFrontBB[COLOR_NB][RANK_NB];
extern Bitboard StepAttacksBB[PIECE_NB][SQUARE_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];
extern Bitboard DistanceRingsBB[SQUARE_NB][8];
extern Bitboard ForwardBB[COLOR_NB][SQUARE_NB];
extern Bitboard PassedPawnMask[COLOR_NB][SQUARE_NB];
extern Bitboard PawnAttackSpan[COLOR_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

extern int SquareDistance[SQUARE_NB][SQUARE_NB];

const Bitboard DarkSquares = 0xAA55AA55AA55AA55ULL;

/// Overloads of bitwise operators between a Bitboard and a Square for testing
/// whether a given bit is set in a bitboard, and for setting and clearing bits.

inline Bitboard operator&(Bitboard bitboard, Square square) {
  return bitboard & SquareBB[square];
}

inline Bitboard& operator|=(Bitboard& bitboard, Square square) {
  return bitboard |= SquareBB[square];
}

inline Bitboard& operator^=(Bitboard& bitboard, Square square) {
  return bitboard ^= SquareBB[square];
}

inline Bitboard operator|(Bitboard bitboard, Square square) {
  return bitboard | SquareBB[square];
}

inline Bitboard operator^(Bitboard bitboard, Square square) {
  return bitboard ^ SquareBB[square];
}

inline bool more_than_one(Bitboard bitboard) {
  return bitboard & (bitboard - 1);
}

template<typename T> inline int distance(T x, T y) { return x < y ? y - x : x - y; }
template<> inline int distance<Square>(Square x, Square y) { return SquareDistance[x][y]; }

template<typename T1, typename T2> inline int distance(T2 x, T2 y);
template<> inline int distance<File>(Square x, Square y) { return distance(file_of(x), file_of(y)); }
template<> inline int distance<Rank>(Square x, Square y) { return distance(rank_of(x), rank_of(y)); }


/// shift_bb() moves bitboard one step along direction Delta. Mainly for pawns.

template<Square Delta>
inline Bitboard shift_bb(Bitboard bitboard) {

  return  Delta == DELTA_N  ?  bitboard             << 8 : Delta == DELTA_S  ?  bitboard             >> 8
        : Delta == DELTA_NE ? (bitboard & ~FileHBB) << 9 : Delta == DELTA_SE ? (bitboard & ~FileHBB) >> 7
        : Delta == DELTA_NW ? (bitboard & ~FileABB) << 7 : Delta == DELTA_SW ? (bitboard & ~FileABB) >> 9
        : 0;
}


/// rank_bb() and file_bb() take a file or a square as input and return
/// a bitboard representing all squares on the given file or rank.

inline Bitboard rank_bb(Rank rank) {
  return RankBB[rank];
}

inline Bitboard rank_bb(Square square) {
  return RankBB[rank_of(square)];
}

inline Bitboard file_bb(File file) {
  return FileBB[file];
}

inline Bitboard file_bb(Square square) {
  return FileBB[file_of(square)];
}


/// adjacent_files_bb() takes a file as input and returns a bitboard representing
/// all squares on the adjacent files.

inline Bitboard adjacent_files_bb(File file) {
  return AdjacentFilesBB[file];
}


/// in_front_bb() takes a color and a rank as input, and returns a bitboard
/// representing all the squares on all ranks in front of the rank, from the
/// given color's point of view. For instance, in_front_bb(BLACK, RANK_3) will
/// give all squares on ranks 1 and 2.

inline Bitboard in_front_bb(Color color, Rank rank) {
  return InFrontBB[color][rank];
}


/// between_bb() returns a bitboard representing all squares between two squares.
/// For instance, between_bb(SQ_C4, SQ_F7) returns a bitboard with the bits for
/// square d5 and e6 set.  If square1 and square2 are not on the same rank, file or diagonal,
/// 0 is returned.

inline Bitboard between_bb(Square square1, Square square2) {
  return BetweenBB[square1][square2];
}


/// forward_bb() takes a color and a square as input, and returns a bitboard
/// representing all squares along the line in front of the square, from the
/// point of view of the given color. Definition of the table is:
/// ForwardBB[color][square] = in_front_bb(color, square) & file_bb(square)

inline Bitboard forward_bb(Color color, Square square) {
  return ForwardBB[color][square];
}


/// pawn_attack_span() takes a color and a square as input, and returns a bitboard
/// representing all squares that can be attacked by a pawn of the given color
/// when it moves along its file starting from the given square. Definition is:
/// PawnAttackSpan[color][square] = in_front_bb(color, square) & adjacent_files_bb(square);

inline Bitboard pawn_attack_span(Color color, Square square) {
  return PawnAttackSpan[color][square];
}


/// passed_pawn_mask() takes a color and a square as input, and returns a
/// bitboard mask which can be used to test if a pawn of the given color on
/// the given square is a passed pawn. Definition of the table is:
/// PassedPawnMask[color][square] = pawn_attack_span(color, square) | forward_bb(color, square)

inline Bitboard passed_pawn_mask(Color color, Square square) {
  return PassedPawnMask[color][square];
}


/// squares_of_color() returns a bitboard representing all squares with the same
/// color of the given square.

inline Bitboard squares_of_color(Square square) {
  return DarkSquares & square ? DarkSquares : ~DarkSquares;
}


/// aligned() returns true if the squares square1, square2 and square3 are aligned
/// either on a straight or on a diagonal line.

inline bool aligned(Square square1, Square square2, Square square3) {
  return LineBB[square1][square2] & square3;
}


/// Functions for computing sliding attack bitboards. Function attacks_bb() takes
/// a square and a bitboard of occupied squares as input, and returns a bitboard
/// representing all squares attacked by Pt (bishop or rook) on the given square.
template<PieceType Pt>
FORCE_INLINE unsigned magic_index(Square square, Bitboard occupied) {

  Bitboard* const Masks  = Pt == ROOK ? RookMasks  : BishopMasks;
  Bitboard* const Magics = Pt == ROOK ? RookMagics : BishopMagics;
  unsigned* const Shifts = Pt == ROOK ? RookShifts : BishopShifts;

  if (HasPext)
      return unsigned(_pext_u64(occupied, Masks[square]));

  if (Is64Bit)
      return unsigned(((occupied & Masks[square]) * Magics[square]) >> Shifts[square]);

  unsigned lo = unsigned(occupied) & unsigned(Masks[square]);
  unsigned hi = unsigned(occupied >> 32) & unsigned(Masks[square] >> 32);
  return (lo * unsigned(Magics[square]) ^ hi * unsigned(Magics[square] >> 32)) >> Shifts[square];
}

template<PieceType Pt>
inline Bitboard attacks_bb(Square square, Bitboard occupied) {
  return (Pt == ROOK ? RookAttacks : BishopAttacks)[square][magic_index<Pt>(square, occupied)];
}

inline Bitboard attacks_bb(Piece piece, Square square, Bitboard occupied) {

  switch (type_of(piece))
  {
  case BISHOP: return attacks_bb<BISHOP>(square, occupied);
  case ROOK  : return attacks_bb<ROOK>(square, occupied);
  case QUEEN : return attacks_bb<BISHOP>(square, occupied) | attacks_bb<ROOK>(square, occupied);
  default    : return StepAttacksBB[piece][square];
  }
}

/// lsb()/msb() finds the least/most significant bit in a non-zero bitboard.
/// pop_lsb() finds and clears the least significant bit in a non-zero bitboard.

#ifdef USE_BSFQ

#  if defined(_MSC_VER) && !defined(__INTEL_COMPILER)

FORCE_INLINE Square lsb(Bitboard bitboard) {
  unsigned long index;
  _BitScanForward64(&index, bitboard);
  return (Square) index;
}

FORCE_INLINE Square msb(Bitboard bitboard) {
  unsigned long index;
  _BitScanReverse64(&index, bitboard);
  return (Square) index;
}

#  elif defined(__arm__)

FORCE_INLINE int lsb32(uint32_t value) {
  __asm__("rbit %0, %1" : "=r"(value) : "r"(value));
  return __builtin_clz(value);
}

FORCE_INLINE Square msb(Bitboard bitboard) {
  return (Square) (63 - __builtin_clzll(bitboard));
}

FORCE_INLINE Square lsb(Bitboard bitboard) {
  return (Square) (uint32_t(bitboard) ? lsb32(uint32_t(bitboard)) : 32 + lsb32(uint32_t(bitboard >> 32)));
}

#  else

FORCE_INLINE Square lsb(Bitboard bitboard) { // Assembly code by Heinz van Saanen
  Bitboard index;
  __asm__("bsfq %1, %0": "=r"(index): "rm"(bitboard) );
  return (Square) index;
}

FORCE_INLINE Square msb(Bitboard bitboard) {
  Bitboard index;
  __asm__("bsrq %1, %0": "=r"(index): "rm"(bitboard) );
  return (Square) index;
}

#  endif

FORCE_INLINE Square pop_lsb(Bitboard* bitboard) {
  const Square square = lsb(*bitboard);
  *bitboard &= *bitboard - 1;
  return square;
}

#else // if defined(USE_BSFQ)

extern Square msb(Bitboard bitboard);
extern Square lsb(Bitboard bitboard);
extern Square pop_lsb(Bitboard* bitboard);

#endif

/// frontmost_sq() and backmost_sq() find the square corresponding to the
/// most/least advanced bit relative to the given color.

inline Square frontmost_sq(Color color, Bitboard bitboard) { return color == WHITE ? msb(bitboard) : lsb(bitboard); }
inline Square  backmost_sq(Color color, Bitboard bitboard) { return color == WHITE ? lsb(bitboard) : msb(bitboard); }

#endif // #ifndef BITBOARD_H_INCLUDED
