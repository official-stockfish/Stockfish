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

#include <algorithm>

#include "bitboard.h"
#include "bitcount.h"
#include "misc.h"

int SquareDistance[SQUARE_NB][SQUARE_NB];

Bitboard  RookMasks  [SQUARE_NB];
Bitboard  RookMagics [SQUARE_NB];
Bitboard* RookAttacks[SQUARE_NB];
unsigned  RookShifts [SQUARE_NB];

Bitboard  BishopMasks  [SQUARE_NB];
Bitboard  BishopMagics [SQUARE_NB];
Bitboard* BishopAttacks[SQUARE_NB];
unsigned  BishopShifts [SQUARE_NB];

Bitboard SquareBB[SQUARE_NB];
Bitboard FileBB[FILE_NB];
Bitboard RankBB[RANK_NB];
Bitboard AdjacentFilesBB[FILE_NB];
Bitboard InFrontBB[COLOR_NB][RANK_NB];
Bitboard StepAttacksBB[PIECE_NB][SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];
Bitboard DistanceRingBB[SQUARE_NB][8];
Bitboard ForwardBB[COLOR_NB][SQUARE_NB];
Bitboard PassedPawnMask[COLOR_NB][SQUARE_NB];
Bitboard PawnAttackSpan[COLOR_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

namespace {

  // De Bruijn sequences. See chessprogramming.wikispaces.com/BitScan
  const uint64_t DeBruijn64 = 0x3F79D71B4CB0A89ULL;
  const uint32_t DeBruijn32 = 0x783A9B23;

  int MSBTable[256];            // To implement software msb()
  Square BSFTable[SQUARE_NB];   // To implement software bitscan
  Bitboard RookTable[0x19000];  // To store rook attacks
  Bitboard BishopTable[0x1480]; // To store bishop attacks

  typedef unsigned (Fn)(Square, Bitboard);

  void init_magics(Bitboard table[], Bitboard* attacks[], Bitboard magics[],
                   Bitboard masks[], unsigned shifts[], Square deltas[], Fn index);

  // bsf_index() returns the index into BSFTable[] to look up the bitscan. Uses
  // Matt Taylor's folding for 32 bit case, extended to 64 bit by Kim Walisch.

  unsigned bsf_index(Bitboard b) {
    b ^= b - 1;
    return Is64Bit ? (b * DeBruijn64) >> 58
                   : ((unsigned(b) ^ unsigned(b >> 32)) * DeBruijn32) >> 26;
  }
}

#ifndef USE_BSFQ

/// Software fall-back of lsb() and msb() for CPU lacking hardware support

Square lsb(Bitboard b) {
  return BSFTable[bsf_index(b)];
}

Square msb(Bitboard b) {

  unsigned b32;
  int result = 0;

  if (b > 0xFFFFFFFF)
  {
      b >>= 32;
      result = 32;
  }

  b32 = unsigned(b);

  if (b32 > 0xFFFF)
  {
      b32 >>= 16;
      result += 16;
  }

  if (b32 > 0xFF)
  {
      b32 >>= 8;
      result += 8;
  }

  return Square(result + MSBTable[b32]);
}

#endif // ifndef USE_BSFQ


/// Bitboards::pretty() returns an ASCII representation of a bitboard suitable
/// to be printed to standard output. Useful for debugging.

const std::string Bitboards::pretty(Bitboard b) {

  std::string s = "+---+---+---+---+---+---+---+---+\n";

  for (Rank r = RANK_8; r >= RANK_1; --r)
  {
      for (File f = FILE_A; f <= FILE_H; ++f)
          s += b & make_square(f, r) ? "| X " : "|   ";

      s += "|\n+---+---+---+---+---+---+---+---+\n";
  }

  return s;
}


/// Bitboards::init() initializes various bitboard tables. It is called at
/// startup and relies on global objects to be already zero-initialized.

void Bitboards::init() {

  for (Square s = SQ_A1; s <= SQ_H8; ++s)
  {
      SquareBB[s] = 1ULL << s;
      BSFTable[bsf_index(SquareBB[s])] = s;
  }

  for (Bitboard b = 2; b < 256; ++b)
      MSBTable[b] = MSBTable[b - 1] + !more_than_one(b);

  for (File f = FILE_A; f <= FILE_H; ++f)
      FileBB[f] = f > FILE_A ? FileBB[f - 1] << 1 : FileABB;

  for (Rank r = RANK_1; r <= RANK_8; ++r)
      RankBB[r] = r > RANK_1 ? RankBB[r - 1] << 8 : Rank1BB;

  for (File f = FILE_A; f <= FILE_H; ++f)
      AdjacentFilesBB[f] = (f > FILE_A ? FileBB[f - 1] : 0) | (f < FILE_H ? FileBB[f + 1] : 0);

  for (Rank r = RANK_1; r < RANK_8; ++r)
      InFrontBB[WHITE][r] = ~(InFrontBB[BLACK][r + 1] = InFrontBB[BLACK][r] | RankBB[r]);

  for (Color c = WHITE; c <= BLACK; ++c)
      for (Square s = SQ_A1; s <= SQ_H8; ++s)
      {
          ForwardBB[c][s]      = InFrontBB[c][rank_of(s)] & FileBB[file_of(s)];
          PawnAttackSpan[c][s] = InFrontBB[c][rank_of(s)] & AdjacentFilesBB[file_of(s)];
          PassedPawnMask[c][s] = ForwardBB[c][s] | PawnAttackSpan[c][s];
      }

  for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
      for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
          if (s1 != s2)
          {
              SquareDistance[s1][s2] = std::max(distance<File>(s1, s2), distance<Rank>(s1, s2));
              DistanceRingBB[s1][SquareDistance[s1][s2] - 1] |= s2;
          }

  int steps[][9] = { {}, { 7, 9 }, { 17, 15, 10, 6, -6, -10, -15, -17 },
                     {}, {}, {}, { 9, 7, -7, -9, 8, 1, -1, -8 } };

  for (Color c = WHITE; c <= BLACK; ++c)
      for (PieceType pt = PAWN; pt <= KING; ++pt)
          for (Square s = SQ_A1; s <= SQ_H8; ++s)
              for (int i = 0; steps[pt][i]; ++i)
              {
                  Square to = s + Square(c == WHITE ? steps[pt][i] : -steps[pt][i]);

                  if (is_ok(to) && distance(s, to) < 3)
                      StepAttacksBB[make_piece(c, pt)][s] |= to;
              }

  Square RookDeltas[] = { DELTA_N,  DELTA_E,  DELTA_S,  DELTA_W  };
  Square BishopDeltas[] = { DELTA_NE, DELTA_SE, DELTA_SW, DELTA_NW };

  init_magics(RookTable, RookAttacks, RookMagics, RookMasks, RookShifts, RookDeltas, magic_index<ROOK>);
  init_magics(BishopTable, BishopAttacks, BishopMagics, BishopMasks, BishopShifts, BishopDeltas, magic_index<BISHOP>);

  for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
  {
      PseudoAttacks[QUEEN][s1]  = PseudoAttacks[BISHOP][s1] = attacks_bb<BISHOP>(s1, 0);
      PseudoAttacks[QUEEN][s1] |= PseudoAttacks[  ROOK][s1] = attacks_bb<  ROOK>(s1, 0);

      for (Piece pc = W_BISHOP; pc <= W_ROOK; ++pc)
          for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
          {
              if (!(PseudoAttacks[pc][s1] & s2))
                  continue;

              LineBB[s1][s2] = (attacks_bb(pc, s1, 0) & attacks_bb(pc, s2, 0)) | s1 | s2;
              BetweenBB[s1][s2] = attacks_bb(pc, s1, SquareBB[s2]) & attacks_bb(pc, s2, SquareBB[s1]);
          }
  }
}


namespace {

  Bitboard sliding_attack(Square deltas[], Square sq, Bitboard occupied) {

    Bitboard attack = 0;

    for (int i = 0; i < 4; ++i)
        for (Square s = sq + deltas[i];
             is_ok(s) && distance(s, s - deltas[i]) == 1;
             s += deltas[i])
        {
            attack |= s;

            if (occupied & s)
                break;
        }

    return attack;
  }


  // init_magics() computes all rook and bishop attacks at startup. Magic
  // bitboards are used to look up attacks of sliding pieces. As a reference see
  // chessprogramming.wikispaces.com/Magic+Bitboards. In particular, here we
  // use the so called "fancy" approach.

  void init_magics(Bitboard table[], Bitboard* attacks[], Bitboard magics[],
                   Bitboard masks[], unsigned shifts[], Square deltas[], Fn index) {

    int seeds[][RANK_NB] = { { 8977, 44560, 54343, 38998,  5731, 95205, 104912, 17020 },
                             {  728, 10316, 55013, 32803, 12281, 15100,  16645,   255 } };

    Bitboard occupancy[4096], reference[4096], edges, b;
    int age[4096] = {0}, current = 0, i, size;

    // attacks[s] is a pointer to the beginning of the attacks table for square 's'
    attacks[SQ_A1] = table;

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
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
            reference[size] = sliding_attack(deltas, s, b);

            if (HasPext)
                attacks[s][pext(b, masks[s])] = reference[size];

            size++;
            b = (b - masks[s]) & masks[s];
        } while (b);

        // Set the offset for the table of the next square. We have individual
        // table sizes for each square with "Fancy Magic Bitboards".
        if (s < SQ_H8)
            attacks[s + 1] = attacks[s] + size;

        if (HasPext)
            continue;

        PRNG rng(seeds[Is64Bit][rank_of(s)]);

        // Find a magic for square 's' picking up an (almost) random number
        // until we find the one that passes the verification test.
        do {
            do
                magics[s] = rng.sparse_rand<Bitboard>();
            while (popcount<Max15>((magics[s] * masks[s]) >> 56) < 6);

            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[s] database.
            // Note that we build up the database for square 's' as a side
            // effect of verifying the magic.
            for (++current, i = 0; i < size; ++i)
            {
                unsigned idx = index(s, occupancy[i]);

                if (age[idx] < current)
                {
                    age[idx] = current;
                    attacks[s][idx] = reference[i];
                }
                else if (attacks[s][idx] != reference[i])
                    break;
            }
        } while (i < size);
    }
  }
}
