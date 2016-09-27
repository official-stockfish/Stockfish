/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

namespace Bitbases {

void init();
bool probe(Square wksq, Square wpsq, Square bksq, Color us);

}

namespace Bitboards {

void init();
const std::string pretty(Bitboard b);

}

const Bitboard DarkSquares = 0xAA55AA55AA55AA55ULL;

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

extern int SquareDistance[SQUARE_NB][SQUARE_NB];

extern Bitboard SquareBB[SQUARE_NB];
extern Bitboard FileBB[FILE_NB];
extern Bitboard RankBB[RANK_NB];
extern Bitboard AdjacentFilesBB[FILE_NB];
extern Bitboard InFrontBB[COLOR_NB][RANK_NB];
extern Bitboard StepAttacksBB[PIECE_NB][SQUARE_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];
extern Bitboard DistanceRingBB[SQUARE_NB][8];
extern Bitboard ForwardBB[COLOR_NB][SQUARE_NB];
extern Bitboard PassedPawnMask[COLOR_NB][SQUARE_NB];
extern Bitboard PawnAttackSpan[COLOR_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];


/// Overloads of bitwise operators between a Bitboard and a Square for testing
/// whether a given bit is set in a bitboard, and for setting and clearing bits.

inline Bitboard operator&(Bitboard b, Square s) {
  return b & SquareBB[s];
}

inline Bitboard operator|(Bitboard b, Square s) {
  return b | SquareBB[s];
}

inline Bitboard operator^(Bitboard b, Square s) {
  return b ^ SquareBB[s];
}

inline Bitboard& operator|=(Bitboard& b, Square s) {
  return b |= SquareBB[s];
}

inline Bitboard& operator^=(Bitboard& b, Square s) {
  return b ^= SquareBB[s];
}

inline bool more_than_one(Bitboard b) {
  return b & (b - 1);
}


/// rank_bb() and file_bb() return a bitboard representing all the squares on
/// the given file or rank.

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


/// shift() moves a bitboard one step along direction D. Mainly for pawns

template<Square D>
inline Bitboard shift(Bitboard b) {
  return  D == NORTH      ?  b             << 8 : D == SOUTH      ?  b             >> 8
        : D == NORTH_EAST ? (b & ~FileHBB) << 9 : D == SOUTH_EAST ? (b & ~FileHBB) >> 7
        : D == NORTH_WEST ? (b & ~FileABB) << 7 : D == SOUTH_WEST ? (b & ~FileABB) >> 9
        : 0;
}


/// adjacent_files_bb() returns a bitboard representing all the squares on the
/// adjacent files of the given one.

inline Bitboard adjacent_files_bb(File f) {
  return AdjacentFilesBB[f];
}


/// between_bb() returns a bitboard representing all the squares between the two
/// given ones. For instance, between_bb(SQ_C4, SQ_F7) returns a bitboard with
/// the bits for square d5 and e6 set. If s1 and s2 are not on the same rank, file
/// or diagonal, 0 is returned.

inline Bitboard between_bb(Square s1, Square s2) {
  return BetweenBB[s1][s2];
}


/// in_front_bb() returns a bitboard representing all the squares on all the ranks
/// in front of the given one, from the point of view of the given color. For
/// instance, in_front_bb(BLACK, RANK_3) will return the squares on ranks 1 and 2.

inline Bitboard in_front_bb(Color c, Rank r) {
  return InFrontBB[c][r];
}


/// forward_bb() returns a bitboard representing all the squares along the line
/// in front of the given one, from the point of view of the given color:
///        ForwardBB[c][s] = in_front_bb(c, s) & file_bb(s)

inline Bitboard forward_bb(Color c, Square s) {
  return ForwardBB[c][s];
}


/// pawn_attack_span() returns a bitboard representing all the squares that can be
/// attacked by a pawn of the given color when it moves along its file, starting
/// from the given square:
///       PawnAttackSpan[c][s] = in_front_bb(c, s) & adjacent_files_bb(s);

inline Bitboard pawn_attack_span(Color c, Square s) {
  return PawnAttackSpan[c][s];
}


/// passed_pawn_mask() returns a bitboard mask which can be used to test if a
/// pawn of the given color and on the given square is a passed pawn:
///       PassedPawnMask[c][s] = pawn_attack_span(c, s) | forward_bb(c, s)

inline Bitboard passed_pawn_mask(Color c, Square s) {
  return PassedPawnMask[c][s];
}


/// aligned() returns true if the squares s1, s2 and s3 are aligned either on a
/// straight or on a diagonal line.

inline bool aligned(Square s1, Square s2, Square s3) {
  return LineBB[s1][s2] & s3;
}


/// distance() functions return the distance between x and y, defined as the
/// number of steps for a king in x to reach y. Works with squares, ranks, files.

template<typename T> inline int distance(T x, T y) { return x < y ? y - x : x - y; }
template<> inline int distance<Square>(Square x, Square y) { return SquareDistance[x][y]; }

template<typename T1, typename T2> inline int distance(T2 x, T2 y);
template<> inline int distance<File>(Square x, Square y) { return distance(file_of(x), file_of(y)); }
template<> inline int distance<Rank>(Square x, Square y) { return distance(rank_of(x), rank_of(y)); }


/// attacks_bb() returns a bitboard representing all the squares attacked by a
/// piece of type Pt (bishop or rook) placed on 's'. The helper magic_index()
/// looks up the index using the 'magic bitboards' approach.
template<PieceType Pt>
inline unsigned magic_index(Square s, Bitboard occupied) {

  extern Bitboard RookMasks[SQUARE_NB];
  extern Bitboard RookMagics[SQUARE_NB];
  extern unsigned RookShifts[SQUARE_NB];
  extern Bitboard BishopMasks[SQUARE_NB];
  extern Bitboard BishopMagics[SQUARE_NB];
  extern unsigned BishopShifts[SQUARE_NB];

  Bitboard* const Masks  = Pt == ROOK ? RookMasks  : BishopMasks;
  Bitboard* const Magics = Pt == ROOK ? RookMagics : BishopMagics;
  unsigned* const Shifts = Pt == ROOK ? RookShifts : BishopShifts;

  if (HasPext)
      return unsigned(pext(occupied, Masks[s]));

  if (Is64Bit)
      return unsigned(((occupied & Masks[s]) * Magics[s]) >> Shifts[s]);

  unsigned lo = unsigned(occupied) & unsigned(Masks[s]);
  unsigned hi = unsigned(occupied >> 32) & unsigned(Masks[s] >> 32);
  return (lo * unsigned(Magics[s]) ^ hi * unsigned(Magics[s] >> 32)) >> Shifts[s];
}

template<PieceType Pt>
inline Bitboard attacks_bb(Square s, Bitboard occupied) {

  extern Bitboard* RookAttacks[SQUARE_NB];
  extern Bitboard* BishopAttacks[SQUARE_NB];

  return (Pt == ROOK ? RookAttacks : BishopAttacks)[s][magic_index<Pt>(s, occupied)];
}

inline Bitboard attacks_bb(Piece pc, Square s, Bitboard occupied) {

  switch (type_of(pc))
  {
  case BISHOP: return attacks_bb<BISHOP>(s, occupied);
  case ROOK  : return attacks_bb<ROOK>(s, occupied);
  case QUEEN : return attacks_bb<BISHOP>(s, occupied) | attacks_bb<ROOK>(s, occupied);
  default    : return StepAttacksBB[pc][s];
  }
}


/// popcount() counts the number of non-zero bits in a bitboard

inline int popcount(Bitboard b) {

#ifndef USE_POPCNT

  extern uint8_t PopCnt16[1 << 16];
  union { Bitboard bb; uint16_t u[4]; } v = { b };
  return PopCnt16[v.u[0]] + PopCnt16[v.u[1]] + PopCnt16[v.u[2]] + PopCnt16[v.u[3]];

#elif defined(_MSC_VER) || defined(__INTEL_COMPILER)

  return (int)_mm_popcnt_u64(b);

#else // Assumed gcc or compatible compiler

  return __builtin_popcountll(b);

#endif
}


/// lsb() and msb() return the least/most significant bit in a non-zero bitboard

#if defined(__GNUC__)

inline Square lsb(Bitboard b) {
  assert(b);
  return Square(__builtin_ctzll(b));
}

inline Square msb(Bitboard b) {
  assert(b);
  return Square(63 - __builtin_clzll(b));
}

#elif defined(_WIN64) && defined(_MSC_VER)

inline Square lsb(Bitboard b) {
  assert(b);
  unsigned long idx;
  _BitScanForward64(&idx, b);
  return (Square) idx;
}

inline Square msb(Bitboard b) {
  assert(b);
  unsigned long idx;
  _BitScanReverse64(&idx, b);
  return (Square) idx;
}

#else

#define NO_BSF // Fallback on software implementation for other cases

Square lsb(Bitboard b);
Square msb(Bitboard b);

#endif


/// pop_lsb() finds and clears the least significant bit in a non-zero bitboard

inline Square pop_lsb(Bitboard* b) {
  const Square s = lsb(*b);
  *b &= *b - 1;
  return s;
}


/// frontmost_sq() and backmost_sq() return the square corresponding to the
/// most/least advanced bit relative to the given color.

inline Square frontmost_sq(Color c, Bitboard b) { return c == WHITE ? msb(b) : lsb(b); }
inline Square  backmost_sq(Color c, Bitboard b) { return c == WHITE ? lsb(b) : msb(b); }

#endif // #ifndef BITBOARD_H_INCLUDED
