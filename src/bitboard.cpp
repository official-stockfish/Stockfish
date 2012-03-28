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

#include <algorithm>
#include <cstring>
#include <iostream>

#include "bitboard.h"
#include "bitcount.h"
#include "rkiss.h"

CACHE_LINE_ALIGNMENT

Bitboard RMasks[64];
Bitboard RMagics[64];
Bitboard* RAttacks[64];
unsigned RShifts[64];

Bitboard BMasks[64];
Bitboard BMagics[64];
Bitboard* BAttacks[64];
unsigned BShifts[64];

Bitboard SquareBB[64];
Bitboard FileBB[8];
Bitboard RankBB[8];
Bitboard AdjacentFilesBB[8];
Bitboard ThisAndAdjacentFilesBB[8];
Bitboard InFrontBB[2][8];
Bitboard StepAttacksBB[16][64];
Bitboard BetweenBB[64][64];
Bitboard SquaresInFrontMask[2][64];
Bitboard PassedPawnMask[2][64];
Bitboard AttackSpanMask[2][64];
Bitboard PseudoAttacks[6][64];

uint8_t BitCount8Bit[256];
int SquareDistance[64][64];

namespace {

  CACHE_LINE_ALIGNMENT

  int BSFTable[64];
  int MS1BTable[256];
  Bitboard RTable[0x19000]; // Storage space for rook attacks
  Bitboard BTable[0x1480];  // Storage space for bishop attacks

  typedef unsigned (Fn)(Square, Bitboard);

  void init_magics(Bitboard table[], Bitboard* attacks[], Bitboard magics[],
                   Bitboard masks[], unsigned shifts[], Square deltas[], Fn index);
}


/// print_bitboard() prints a bitboard in an easily readable format to the
/// standard output. This is sometimes useful for debugging.

void print_bitboard(Bitboard b) {

  for (Rank r = RANK_8; r >= RANK_1; r--)
  {
      std::cout << "+---+---+---+---+---+---+---+---+" << '\n';
      for (File f = FILE_A; f <= FILE_H; f++)
          std::cout << "| " << ((b & make_square(f, r)) ? "X " : "  ");

      std::cout << "|\n";
  }
  std::cout << "+---+---+---+---+---+---+---+---+" << std::endl;
}


/// first_1() finds the least significant nonzero bit in a nonzero bitboard.
/// pop_1st_bit() finds and clears the least significant nonzero bit in a
/// nonzero bitboard.

#if defined(IS_64BIT) && !defined(USE_BSFQ)

Square first_1(Bitboard b) {
  return Square(BSFTable[((b & -b) * 0x218A392CD3D5DBFULL) >> 58]);
}

Square pop_1st_bit(Bitboard* b) {
  Bitboard bb = *b;
  *b &= (*b - 1);
  return Square(BSFTable[((bb & -bb) * 0x218A392CD3D5DBFULL) >> 58]);
}

#elif !defined(USE_BSFQ)

Square first_1(Bitboard b) {
  b ^= (b - 1);
  uint32_t fold = unsigned(b) ^ unsigned(b >> 32);
  return Square(BSFTable[(fold * 0x783A9B23) >> 26]);
}

// Use type-punning
union b_union {

    Bitboard dummy;
    struct {
#if defined (BIGENDIAN)
        uint32_t h;
        uint32_t l;
#else
        uint32_t l;
        uint32_t h;
#endif
    } b;
};

Square pop_1st_bit(Bitboard* b) {

   const b_union u = *((b_union*)b);

   if (u.b.l)
   {
       ((b_union*)b)->b.l = u.b.l & (u.b.l - 1);
       return Square(BSFTable[((u.b.l ^ (u.b.l - 1)) * 0x783A9B23) >> 26]);
   }

   ((b_union*)b)->b.h = u.b.h & (u.b.h - 1);
   return Square(BSFTable[((~(u.b.h ^ (u.b.h - 1))) * 0x783A9B23) >> 26]);
}

Square last_1(Bitboard b) {

  int result = 0;

  if (b > 0xFFFFFFFF)
  {
      b >>= 32;
      result = 32;
  }

  if (b > 0xFFFF)
  {
      b >>= 16;
      result += 16;
  }

  if (b > 0xFF)
  {
      b >>= 8;
      result += 8;
  }

  return Square(result + MS1BTable[b]);
}

#endif // !defined(USE_BSFQ)

/// bitboards_init() initializes various bitboard arrays. It is called during
/// program initialization.

void bitboards_init() {

  for (int k = 0, i = 0; i < 8; i++)
      while (k < (2 << i))
          MS1BTable[k++] = i;

  for (Bitboard b = 0; b < 256; b++)
      BitCount8Bit[b] = (uint8_t)popcount<Max15>(b);

  for (Square s = SQ_A1; s <= SQ_H8; s++)
      SquareBB[s] = 1ULL << s;

  FileBB[FILE_A] = FileABB;
  RankBB[RANK_1] = Rank1BB;

  for (int f = FILE_B; f <= FILE_H; f++)
  {
      FileBB[f] = FileBB[f - 1] << 1;
      RankBB[f] = RankBB[f - 1] << 8;
  }

  for (int f = FILE_A; f <= FILE_H; f++)
  {
      AdjacentFilesBB[f] = (f > FILE_A ? FileBB[f - 1] : 0) | (f < FILE_H ? FileBB[f + 1] : 0);
      ThisAndAdjacentFilesBB[f] = FileBB[f] | AdjacentFilesBB[f];
  }

  for (int rw = RANK_7, rb = RANK_2; rw >= RANK_1; rw--, rb++)
  {
      InFrontBB[WHITE][rw] = InFrontBB[WHITE][rw + 1] | RankBB[rw + 1];
      InFrontBB[BLACK][rb] = InFrontBB[BLACK][rb - 1] | RankBB[rb - 1];
  }

  for (Color c = WHITE; c <= BLACK; c++)
      for (Square s = SQ_A1; s <= SQ_H8; s++)
      {
          SquaresInFrontMask[c][s] = in_front_bb(c, s) & file_bb(s);
          PassedPawnMask[c][s]     = in_front_bb(c, s) & this_and_adjacent_files_bb(file_of(s));
          AttackSpanMask[c][s]     = in_front_bb(c, s) & adjacent_files_bb(file_of(s));
      }

  for (Square s1 = SQ_A1; s1 <= SQ_H8; s1++)
      for (Square s2 = SQ_A1; s2 <= SQ_H8; s2++)
          SquareDistance[s1][s2] = std::max(file_distance(s1, s2), rank_distance(s1, s2));

  for (int i = 0; i < 64; i++)
      if (!Is64Bit) // Matt Taylor's folding trick for 32 bit systems
      {
          Bitboard b = 1ULL << i;
          b ^= b - 1;
          b ^= b >> 32;
          BSFTable[(uint32_t)(b * 0x783A9B23) >> 26] = i;
      }
      else
          BSFTable[((1ULL << i) * 0x218A392CD3D5DBFULL) >> 58] = i;

  int steps[][9] = { {}, { 7, 9 }, { 17, 15, 10, 6, -6, -10, -15, -17 },
                     {}, {}, {}, { 9, 7, -7, -9, 8, 1, -1, -8 } };

  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= KING; pt++)
          for (Square s = SQ_A1; s <= SQ_H8; s++)
              for (int k = 0; steps[pt][k]; k++)
              {
                  Square to = s + Square(c == WHITE ? steps[pt][k] : -steps[pt][k]);

                  if (square_is_ok(to) && square_distance(s, to) < 3)
                      StepAttacksBB[make_piece(c, pt)][s] |= to;
              }

  Square RDeltas[] = { DELTA_N,  DELTA_E,  DELTA_S,  DELTA_W  };
  Square BDeltas[] = { DELTA_NE, DELTA_SE, DELTA_SW, DELTA_NW };

  init_magics(RTable, RAttacks, RMagics, RMasks, RShifts, RDeltas, magic_index<ROOK>);
  init_magics(BTable, BAttacks, BMagics, BMasks, BShifts, BDeltas, magic_index<BISHOP>);

  for (Square s = SQ_A1; s <= SQ_H8; s++)
  {
      PseudoAttacks[BISHOP][s] = attacks_bb<BISHOP>(s, 0);
      PseudoAttacks[ROOK][s]   = attacks_bb<ROOK>(s, 0);
      PseudoAttacks[QUEEN][s]  = PseudoAttacks[BISHOP][s] | PseudoAttacks[ROOK][s];
  }

  for (Square s1 = SQ_A1; s1 <= SQ_H8; s1++)
      for (Square s2 = SQ_A1; s2 <= SQ_H8; s2++)
          if (PseudoAttacks[QUEEN][s1] & s2)
          {
              Square delta = (s2 - s1) / square_distance(s1, s2);

              for (Square s = s1 + delta; s != s2; s += delta)
                  BetweenBB[s1][s2] |= s;
          }
}


namespace {

  Bitboard sliding_attack(Square deltas[], Square sq, Bitboard occupied) {

    Bitboard attack = 0;

    for (int i = 0; i < 4; i++)
        for (Square s = sq + deltas[i];
             square_is_ok(s) && square_distance(s, s - deltas[i]) == 1;
             s += deltas[i])
        {
            attack |= s;

            if (occupied & s)
                break;
        }

    return attack;
  }


  Bitboard pick_random(Bitboard mask, RKISS& rk, int booster) {

    Bitboard magic;

    // Values s1 and s2 are used to rotate the candidate magic of a
    // quantity known to be the optimal to quickly find the magics.
    int s1 = booster & 63, s2 = (booster >> 6) & 63;

    while (true)
    {
        magic = rk.rand<Bitboard>();
        magic = (magic >> s1) | (magic << (64 - s1));
        magic &= rk.rand<Bitboard>();
        magic = (magic >> s2) | (magic << (64 - s2));
        magic &= rk.rand<Bitboard>();

        if (BitCount8Bit[(mask * magic) >> 56] >= 6)
            return magic;
    }
  }


  // init_magics() computes all rook and bishop attacks at startup. Magic
  // bitboards are used to look up attacks of sliding pieces. As a reference see
  // chessprogramming.wikispaces.com/Magic+Bitboards. In particular, here we
  // use the so called "fancy" approach.

  void init_magics(Bitboard table[], Bitboard* attacks[], Bitboard magics[],
                   Bitboard masks[], unsigned shifts[], Square deltas[], Fn index) {

    int MagicBoosters[][8] = { { 3191, 2184, 1310, 3618, 2091, 1308, 2452, 3996 },
                               { 1059, 3608,  605, 3234, 3326,   38, 2029, 3043 } };
    RKISS rk;
    Bitboard occupancy[4096], reference[4096], edges, b;
    int i, size, booster;

    // attacks[s] is a pointer to the beginning of the attacks table for square 's'
    attacks[SQ_A1] = table;

    for (Square s = SQ_A1; s <= SQ_H8; s++)
    {
        // Board edges are not considered in the relevant occupancies
        edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));

        // Given a square 's', the mask is the bitboard of sliding attacks from
        // 's' computed on an empty board. The index must be big enough to contain
        // all the attacks for each possible subset of the mask and so is 2 power
        // the number of 1s of the mask. Hence we deduce the size of the shift to
        // apply to the 64 or 32 bits word to get the index.
        masks[s]  = sliding_attack(deltas, s, 0) & ~edges;
        shifts[s] = (Is64Bit ? 64 : 32) - popcount<Max15>(masks[s]);

        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in reference[].
        b = size = 0;
        do {
            occupancy[size] = b;
            reference[size++] = sliding_attack(deltas, s, b);
            b = (b - masks[s]) & masks[s];
        } while (b);

        // Set the offset for the table of the next square. We have individual
        // table sizes for each square with "Fancy Magic Bitboards".
        if (s < SQ_H8)
            attacks[s + 1] = attacks[s] + size;

        booster = MagicBoosters[Is64Bit][rank_of(s)];

        // Find a magic for square 's' picking up an (almost) random number
        // until we find the one that passes the verification test.
        do {
            magics[s] = pick_random(masks[s], rk, booster);
            memset(attacks[s], 0, size * sizeof(Bitboard));

            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[s] database.
            // Note that we build up the database for square 's' as a side
            // effect of verifying the magic.
            for (i = 0; i < size; i++)
            {
                Bitboard& attack = attacks[s][index(s, occupancy[i])];

                if (attack && attack != reference[i])
                    break;

                attack = reference[i];
            }
        } while (i != size);
    }
  }
}
