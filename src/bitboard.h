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


#if !defined(BITBOARD_H_INCLUDED)
#define BITBOARD_H_INCLUDED


////
//// Defines
////

//#define USE_COMPACT_ROOK_ATTACKS
//#define USE_32BIT_ATTACKS 
#define USE_FOLDED_BITSCAN

#define BITCOUNT_SWAR_64
//#define BITCOUNT_SWAR_32
//#define BITCOUNT_LOOP



////
//// Includes
////

#include "direction.h"
#include "piece.h"
#include "square.h"
#include "types.h"


////
//// Types
////

typedef uint64_t Bitboard;


////
//// Constants and variables
////

const Bitboard EmptyBoardBB = 0ULL;

const Bitboard WhiteSquaresBB = 0x55AA55AA55AA55AAULL;
const Bitboard BlackSquaresBB = 0xAA55AA55AA55AA55ULL;

extern const Bitboard SquaresByColorBB[2];

const Bitboard FileABB = 0x0101010101010101ULL;
const Bitboard FileBBB = 0x0202020202020202ULL;
const Bitboard FileCBB = 0x0404040404040404ULL;
const Bitboard FileDBB = 0x0808080808080808ULL;
const Bitboard FileEBB = 0x1010101010101010ULL;
const Bitboard FileFBB = 0x2020202020202020ULL;
const Bitboard FileGBB = 0x4040404040404040ULL;
const Bitboard FileHBB = 0x8080808080808080ULL;

extern const Bitboard FileBB[8];
extern const Bitboard NeighboringFilesBB[8];
extern const Bitboard ThisAndNeighboringFilesBB[8];  

const Bitboard Rank1BB = 0xFFULL;
const Bitboard Rank2BB = 0xFF00ULL;
const Bitboard Rank3BB = 0xFF0000ULL;
const Bitboard Rank4BB = 0xFF000000ULL;
const Bitboard Rank5BB = 0xFF00000000ULL;
const Bitboard Rank6BB = 0xFF0000000000ULL;
const Bitboard Rank7BB = 0xFF000000000000ULL;
const Bitboard Rank8BB = 0xFF00000000000000ULL;

extern const Bitboard RankBB[8];
extern const Bitboard RelativeRankBB[2][8];
extern const Bitboard InFrontBB[2][8];

extern Bitboard SetMaskBB[64];
extern Bitboard ClearMaskBB[64];

extern Bitboard StepAttackBB[16][64];
extern Bitboard RayBB[64][8];
extern Bitboard BetweenBB[64][64];

extern Bitboard PassedPawnMask[2][64];
extern Bitboard OutpostMask[2][64];

#if defined(USE_COMPACT_ROOK_ATTACKS)
extern Bitboard RankAttacks[8][64], FileAttacks[8][64];
#else
extern const uint64_t RMult[64];
extern const int RShift[64];
extern Bitboard RMask[64];
extern int RAttackIndex[64];
extern Bitboard RAttacks[0x19000];
#endif // defined(USE_COMPACT_ROOK_ATTACKS)

extern const uint64_t BMult[64]; 
extern const int BShift[64];
extern Bitboard BMask[64];
extern int BAttackIndex[64];
extern Bitboard BAttacks[0x1480];

extern Bitboard BishopPseudoAttacks[64];
extern Bitboard RookPseudoAttacks[64];
extern Bitboard QueenPseudoAttacks[64];


////
//// Inline functions
////

/// Functions for testing whether a given bit is set in a bitboard, and for 
/// setting and clearing bits.

inline Bitboard set_mask_bb(Square s) {
  //  return 1ULL << s;
  return SetMaskBB[s];
}

inline Bitboard clear_mask_bb(Square s) {
  //  return ~set_mask_bb(s);
  return ClearMaskBB[s];
}

inline Bitboard bit_is_set(Bitboard b, Square s) {
  return b & set_mask_bb(s);
}

inline void set_bit(Bitboard *b, Square s) {
  *b |= set_mask_bb(s);
}

inline void clear_bit(Bitboard *b, Square s) {
  *b &= clear_mask_bb(s);
}


/// rank_bb() and file_bb() gives a bitboard containing all squares on a given
/// file or rank.  It is also possible to pass a square as input to these
/// functions.

inline Bitboard rank_bb(Rank r) {
  return RankBB[r];
}

inline Bitboard rank_bb(Square s) {
  return rank_bb(square_rank(s));
}

inline Bitboard file_bb(File f) {
  return FileBB[f];
}

inline Bitboard file_bb(Square s) {
  return file_bb(square_file(s));
}


/// neighboring_files_bb takes a file or a square as input, and returns a
/// bitboard representing all squares on the neighboring files.

inline Bitboard neighboring_files_bb(File f) {
  return NeighboringFilesBB[f];
}

inline Bitboard neighboring_files_bb(Square s) {
  return neighboring_files_bb(square_file(s));
}
  

/// this_and_neighboring_files_bb takes a file or a square as input, and
/// returns a bitboard representing all squares on the given and neighboring
/// files.

inline Bitboard this_and_neighboring_files_bb(File f) {
  return ThisAndNeighboringFilesBB[f];
}

inline Bitboard this_and_neighboring_files_bb(Square s) {
  return this_and_neighboring_files_bb(square_file(s));
}


/// relative_rank_bb() takes a color and a rank as input, and returns a bitboard
/// representing all squares on the given rank from the given color's point of
/// view.  For instance, relative_rank_bb(WHITE, 7) gives all squares on the
/// 7th rank, while relative_rank_bb(BLACK, 7) gives all squares on the 2nd
/// rank.

inline Bitboard relative_rank_bb(Color c, Rank r) {
  return RelativeRankBB[c][r];
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
  return in_front_bb(c, square_rank(s));
}


/// ray_bb() gives a bitboard representing all squares along the ray in a
/// given direction from a given square.

inline Bitboard ray_bb(Square s, SignedDirection d) {
  return RayBB[s][d];
}


/// Functions for computing sliding attack bitboards.  rook_attacks_bb(),
/// bishop_attacks_bb() and queen_attacks_bb() all take a square and a
/// bitboard of occupied squares as input, and return a bitboard representing
/// all squares attacked by a rook, bishop or queen on the given square.

#if defined(USE_COMPACT_ROOK_ATTACKS)

inline Bitboard file_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = (blockers >> square_file(s)) & 0x01010101010100ULL;
  return
    FileAttacks[square_rank(s)][(b*0xd6e8802041d0c441ULL)>>58] & file_bb(s);
}

inline Bitboard rank_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = (blockers >> ((s & 56) + 1)) & 63;
  return RankAttacks[square_file(s)][b] & rank_bb(s);
}

inline Bitboard rook_attacks_bb(Square s, Bitboard blockers) {
  return file_attacks_bb(s, blockers) | rank_attacks_bb(s, blockers);
}

#elif defined(USE_32BIT_ATTACKS)

inline Bitboard rook_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = blockers & RMask[s];
  return RAttacks[RAttackIndex[s] + 
                  (unsigned(int(b) * int(RMult[s]) ^
                            int(b >> 32) * int(RMult[s] >> 32)) 
                   >> RShift[s])];
}

#else

inline Bitboard rook_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = blockers & RMask[s];
  return RAttacks[RAttackIndex[s] + ((b * RMult[s]) >> RShift[s])];
}

#endif

#if defined(USE_32BIT_ATTACKS)

inline Bitboard bishop_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = blockers & BMask[s];
  return BAttacks[BAttackIndex[s] + 
                  (unsigned(int(b) * int(BMult[s]) ^
                            int(b >> 32) * int(BMult[s] >> 32)) 
                   >> BShift[s])];
}

#else // defined(USE_32BIT_ATTACKS)

inline Bitboard bishop_attacks_bb(Square s, Bitboard blockers) {
  Bitboard b = blockers & BMask[s];
  return BAttacks[BAttackIndex[s] + ((b * BMult[s]) >> BShift[s])];
}

#endif // defined(USE_32BIT_ATTACKS)

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
/// from the point of view of the given color.  For instance, 
/// squares_in_front_of(BLACK, SQ_E4) returns a bitboard with the squares
/// e3, e2 and e1 set.

inline Bitboard squares_in_front_of(Color c, Square s) {
  return in_front_bb(c, s) & file_bb(s);
}


/// squares_behind is similar to squares_in_front, but returns the squares
/// behind the square instead of in front of the square.

inline Bitboard squares_behind(Color c, Square s) {
  return in_front_bb(opposite_color(c), s) & file_bb(s);
}


/// passed_pawn_mask takes a color and a square as input, and returns a 
/// bitboard mask which can be used to test if a pawn of the given color on 
/// the given square is a passed pawn.

inline Bitboard passed_pawn_mask(Color c, Square s) {
  return PassedPawnMask[c][s];
}


/// outpost_mask takes a color and a square as input, and returns a bitboard
/// mask which can be used to test whether a piece on the square can possibly
/// be driven away by an enemy pawn.

inline Bitboard outpost_mask(Color c, Square s) {
  return OutpostMask[c][s];
}


/// isolated_pawn_mask takes a square as input, and returns a bitboard mask 
/// which can be used to test whether a pawn on the given square is isolated.

inline Bitboard isolated_pawn_mask(Square s) {
  return neighboring_files_bb(s);
}


/// count_1s() counts the number of nonzero bits in a bitboard.

#if defined(BITCOUNT_LOOP)

inline int count_1s(Bitboard b) {
  int r;
  for(r = 0; b; r++, b &= b - 1);
  return r;
}

inline int count_1s_max_15(Bitboard b) {
  return count_1s(b);
}

#elif defined(BITCOUNT_SWAR_32)

inline int count_1s(Bitboard b) {
  unsigned w = unsigned(b >> 32), v = unsigned(b);
  v = v - ((v >> 1) & 0x55555555);
  w = w - ((w >> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
  w = (w & 0x33333333) + ((w >> 2) & 0x33333333);
  v = (v + (v >> 4)) & 0x0F0F0F0F;
  w = (w + (w >> 4)) & 0x0F0F0F0F;
  v = ((v+w) * 0x01010101) >> 24; // mul is fast on amd procs
  return int(v);
}

inline int count_1s_max_15(Bitboard b) {
  unsigned w = unsigned(b >> 32), v = unsigned(b);
  v = v - ((v >> 1) & 0x55555555);
  w = w - ((w >> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
  w = (w & 0x33333333) + ((w >> 2) & 0x33333333);
  v = ((v+w) * 0x11111111) >> 28;
  return int(v);
}

#elif defined(BITCOUNT_SWAR_64)

inline int count_1s(Bitboard b) {
  b -= ((b>>1) & 0x5555555555555555ULL);
  b = ((b>>2) & 0x3333333333333333ULL) + (b & 0x3333333333333333ULL);
  b = ((b>>4) + b) & 0x0F0F0F0F0F0F0F0FULL;
  b *= 0x0101010101010101ULL;
  return int(b >> 56);
}

inline int count_1s_max_15(Bitboard b) {
  b -= (b>>1) & 0x5555555555555555ULL;
  b = ((b>>2) & 0x3333333333333333ULL) + (b & 0x3333333333333333ULL);
  b *= 0x1111111111111111ULL;
  return int(b >> 60);
}

#endif // BITCOUNT


////
//// Prototypes
////

extern void print_bitboard(Bitboard b);
extern void init_bitboards();
extern Square first_1(Bitboard b);
extern Square pop_1st_bit(Bitboard *b);


#endif // !defined(BITBOARD_H_INCLUDED)
