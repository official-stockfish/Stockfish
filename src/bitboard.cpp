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

#include <cstring>
#include <iostream>

#include "bitboard.h"
#include "bitcount.h"
#include "rkiss.h"

// Global bitboards definitions with static storage duration are
// automatically set to zero before enter main().
Bitboard RMask[64];
Bitboard RMult[64];
Bitboard* RAttacks[64];
int RShift[64];

Bitboard BMask[64];
Bitboard BMult[64];
Bitboard* BAttacks[64];
int BShift[64];

Bitboard SetMaskBB[65];
Bitboard ClearMaskBB[65];

Bitboard SquaresByColorBB[2];
Bitboard FileBB[8];
Bitboard RankBB[8];
Bitboard NeighboringFilesBB[8];
Bitboard ThisAndNeighboringFilesBB[8];
Bitboard InFrontBB[2][8];
Bitboard StepAttacksBB[16][64];
Bitboard BetweenBB[64][64];
Bitboard SquaresInFrontMask[2][64];
Bitboard PassedPawnMask[2][64];
Bitboard AttackSpanMask[2][64];

Bitboard BishopPseudoAttacks[64];
Bitboard RookPseudoAttacks[64];
Bitboard QueenPseudoAttacks[64];

uint8_t BitCount8Bit[256];

namespace {

  CACHE_LINE_ALIGNMENT

  int BSFTable[64];
  Bitboard RAttacksTable[0x19000];
  Bitboard BAttacksTable[0x1480];

  void init_sliding_attacks(Bitboard magic[], Bitboard* attack[], Bitboard attTable[],
                            Bitboard mask[], int shift[], Square delta[]);
}


/// print_bitboard() prints a bitboard in an easily readable format to the
/// standard output. This is sometimes useful for debugging.

void print_bitboard(Bitboard b) {

  for (Rank r = RANK_8; r >= RANK_1; r--)
  {
      std::cout << "+---+---+---+---+---+---+---+---+" << '\n';
      for (File f = FILE_A; f <= FILE_H; f++)
          std::cout << "| " << (bit_is_set(b, make_square(f, r)) ? "X " : "  ");

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

    Bitboard b;
    struct {
#if defined (BIGENDIAN)
        uint32_t h;
        uint32_t l;
#else
        uint32_t l;
        uint32_t h;
#endif
    } dw;
};

Square pop_1st_bit(Bitboard* bb) {

   b_union u;
   Square ret;

   u.b = *bb;

   if (u.dw.l)
   {
       ret = Square(BSFTable[((u.dw.l ^ (u.dw.l - 1)) * 0x783A9B23) >> 26]);
       u.dw.l &= (u.dw.l - 1);
       *bb = u.b;
       return ret;
   }
   ret = Square(BSFTable[((~(u.dw.h ^ (u.dw.h - 1))) * 0x783A9B23) >> 26]);
   u.dw.h &= (u.dw.h - 1);
   *bb = u.b;
   return ret;
}

#endif // !defined(USE_BSFQ)


/// init_bitboards() initializes various bitboard arrays. It is called during
/// program initialization.

void init_bitboards() {

  SquaresByColorBB[DARK]  =  0xAA55AA55AA55AA55ULL;
  SquaresByColorBB[LIGHT] = ~SquaresByColorBB[DARK];

  for (Square s = SQ_A1; s <= SQ_H8; s++)
  {
      SetMaskBB[s] = 1ULL << s;
      ClearMaskBB[s] = ~SetMaskBB[s];
  }

  ClearMaskBB[SQ_NONE] = ~EmptyBoardBB;

  FileBB[FILE_A] = FileABB;
  RankBB[RANK_1] = Rank1BB;

  for (int f = FILE_B; f <= FILE_H; f++)
  {
      FileBB[f] = FileBB[f - 1] << 1;
      RankBB[f] = RankBB[f - 1] << 8;
  }

  for (int f = FILE_A; f <= FILE_H; f++)
  {
      NeighboringFilesBB[f] = (f > FILE_A ? FileBB[f - 1] : 0) | (f < FILE_H ? FileBB[f + 1] : 0);
      ThisAndNeighboringFilesBB[f] = FileBB[f] | NeighboringFilesBB[f];
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
          PassedPawnMask[c][s]     = in_front_bb(c, s) & this_and_neighboring_files_bb(s);
          AttackSpanMask[c][s]     = in_front_bb(c, s) & neighboring_files_bb(s);
      }

  for (Bitboard b = 0; b < 256; b++)
      BitCount8Bit[b] = (uint8_t)count_1s<CNT32_MAX15>(b);

  for (int i = 0; i < 64; i++)
      if (!CpuIs64Bit) // Matt Taylor's folding trick for 32 bit systems
      {
          Bitboard b = 1ULL << i;
          b ^= b - 1;
          b ^= b >> 32;
          BSFTable[uint32_t(b * 0x783A9B23) >> 26] = i;
      }
      else
          BSFTable[((1ULL << i) * 0x218A392CD3D5DBFULL) >> 58] = i;

  int steps[][9] = { {}, { 7, 9 }, { 17, 15, 10, 6, -6, -10, -15, -17 },
                     {}, {}, {}, { 9, 7, -7, -9, 8, 1, -1, -8 } };

  for (Color c = WHITE; c <= BLACK; c++)
      for (Square s = SQ_A1; s <= SQ_H8; s++)
          for (PieceType pt = PAWN; pt <= KING; pt++)
              for (int k = 0; steps[pt][k]; k++)
              {
                  Square to = s + Square(c == WHITE ? steps[pt][k] : -steps[pt][k]);

                  if (square_is_ok(to) && square_distance(s, to) < 3)
                      set_bit(&StepAttacksBB[make_piece(c, pt)][s], to);
              }

  Square RDelta[] = { DELTA_N,  DELTA_E,  DELTA_S,  DELTA_W  };
  Square BDelta[] = { DELTA_NE, DELTA_SE, DELTA_SW, DELTA_NW };

  init_sliding_attacks(BMult, BAttacks, BAttacksTable, BMask, BShift, BDelta);
  init_sliding_attacks(RMult, RAttacks, RAttacksTable, RMask, RShift, RDelta);

  for (Square s = SQ_A1; s <= SQ_H8; s++)
  {
      BishopPseudoAttacks[s] = bishop_attacks_bb(s, EmptyBoardBB);
      RookPseudoAttacks[s]   = rook_attacks_bb(s, EmptyBoardBB);
      QueenPseudoAttacks[s]  = queen_attacks_bb(s, EmptyBoardBB);
  }

  for (Square s1 = SQ_A1; s1 <= SQ_H8; s1++)
      for (Square s2 = SQ_A1; s2 <= SQ_H8; s2++)
          if (bit_is_set(QueenPseudoAttacks[s1], s2))
          {
              int f = file_distance(s1, s2);
              int r = rank_distance(s1, s2);

              Square d = (s2 - s1) / Max(f, r);

              for (Square s3 = s1 + d; s3 != s2; s3 += d)
                  set_bit(&BetweenBB[s1][s2], s3);
          }
}


namespace {

  Bitboard submask(Bitboard mask, int key) {

    Bitboard b, subMask = 0;
    int bitProbe = 1;

    // Extract an unique submask out of a mask according to the given key
    while (mask)
    {
        b = mask & -mask;
        mask ^= b;

        if (key & bitProbe)
            subMask |= b;

        bitProbe <<= 1;
    }
    return subMask;
  }

  Bitboard sliding_attacks(Square sq, Bitboard occupied, Square delta[], Bitboard excluded) {

    Bitboard attacks = 0;

    for (int i = 0; i < 4; i++)
    {
        Square s = sq + delta[i];

        while (    square_is_ok(s)
               &&  square_distance(s, s - delta[i]) == 1
               && !bit_is_set(excluded, s))
        {
            set_bit(&attacks, s);

            if (bit_is_set(occupied, s))
                break;

            s += delta[i];
        }
    }
    return attacks;
  }

  Bitboard pick_magic(Bitboard mask, RKISS& rk, int booster) {

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

  void init_sliding_attacks(Bitboard magic[], Bitboard* attack[], Bitboard attTable[],
                            Bitboard mask[], int shift[], Square delta[]) {

    const int  MagicBoosters[][8] = { { 3191, 2184, 1310, 3618, 2091, 1308, 2452, 3996 },
                                      { 1059, 3608,  605, 3234, 3326,   38, 2029, 3043 } };
    RKISS rk;
    Bitboard occupancy[4096], reference[4096], excluded;
    int key, maxKey, index, booster, offset = 0;

    for (Square s = SQ_A1; s <= SQ_H8; s++)
    {
        excluded = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));

        attack[s] = &attTable[offset];
        mask[s]   = sliding_attacks(s, EmptyBoardBB, delta, excluded);
        shift[s]  = (CpuIs64Bit ? 64 : 32) - count_1s<CNT32_MAX15>(mask[s]);

        maxKey = 1 << count_1s<CNT32_MAX15>(mask[s]);
        offset += maxKey;
        booster = MagicBoosters[CpuIs64Bit][square_rank(s)];

        // First compute occupancy and attacks for square 's'
        for (key = 0; key < maxKey; key++)
        {
            occupancy[key] = submask(mask[s], key);
            reference[key] = sliding_attacks(s, occupancy[key], delta, EmptyBoardBB);
        }

        // Then find a possible magic and the corresponding attacks
        do {
            magic[s] = pick_magic(mask[s], rk, booster);
            memset(attack[s], 0, maxKey * sizeof(Bitboard));

            for (key = 0; key < maxKey; key++)
            {
                index = CpuIs64Bit ? unsigned((occupancy[key] * magic[s]) >> shift[s])
                                   : unsigned(occupancy[key] * magic[s] ^ (occupancy[key] >> 32) * (magic[s] >> 32)) >> shift[s];

                if (!attack[s][index])
                    attack[s][index] = reference[key];

                else if (attack[s][index] != reference[key])
                    break;
            }
        } while (key != maxKey);
    }
  }
}
