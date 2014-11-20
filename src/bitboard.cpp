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

#include <algorithm>
#include <cstring> // For memset

#include "bitboard.h"
#include "bitcount.h"
#include "rkiss.h"

Bitboard RookMasks[SQUARE_NB];
Bitboard RookMagics[SQUARE_NB];
Bitboard* RookAttacks[SQUARE_NB];
unsigned RookShifts[SQUARE_NB];

Bitboard BishopMasks[SQUARE_NB];
Bitboard BishopMagics[SQUARE_NB];
Bitboard* BishopAttacks[SQUARE_NB];
unsigned BishopShifts[SQUARE_NB];

Bitboard SquareBB[SQUARE_NB];
Bitboard FileBB[FILE_NB];
Bitboard RankBB[RANK_NB];
Bitboard AdjacentFilesBB[FILE_NB];
Bitboard InFrontBB[COLOR_NB][RANK_NB];
Bitboard StepAttacksBB[PIECE_NB][SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];
Bitboard DistanceRingsBB[SQUARE_NB][8];
Bitboard ForwardBB[COLOR_NB][SQUARE_NB];
Bitboard PassedPawnMask[COLOR_NB][SQUARE_NB];
Bitboard PawnAttackSpan[COLOR_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

int SquareDistance[SQUARE_NB][SQUARE_NB];

namespace {

  // De Bruijn sequences. See chessprogramming.wikispaces.com/BitScan
  const uint64_t DeBruijn_64 = 0x3F79D71B4CB0A89ULL;
  const uint32_t DeBruijn_32 = 0x783A9B23;

  int MS1BTable[256];
  Square BSFTable[SQUARE_NB];
  Bitboard RookTable[0x19000]; // Storage space for rook attacks
  Bitboard BishopTable[0x1480];  // Storage space for bishop attacks

  typedef unsigned (Fn)(Square, Bitboard);

  void init_magics(Bitboard table[], Bitboard* attacks[], Bitboard magics[],
                   Bitboard masks[], unsigned shifts[], Square deltas[], Fn index);

  FORCE_INLINE unsigned bsf_index(Bitboard bitboard) {

    // Matt Taylor's folding for 32 bit systems, extended to 64 bits by Kim Walisch
    bitboard ^= (bitboard - 1);
    return Is64Bit ? (bitboard * DeBruijn_64) >> 58
                   : ((unsigned(bitboard) ^ unsigned(bitboard >> 32)) * DeBruijn_32) >> 26;
  }
}

/// lsb()/msb() finds the least/most significant bit in a non-zero bitboard.
/// pop_lsb() finds and clears the least significant bit in a non-zero bitboard.

#ifndef USE_BSFQ

Square lsb(Bitboard bitboard) { return BSFTable[bsf_index(bitboard)]; }

Square pop_lsb(Bitboard* bitboard) {

  Bitboard bitboard2 = *bitboard;
  *bitboard = bitboard2 & (bitboard2 - 1);
  return BSFTable[bsf_index(bitboard2)];
}

Square msb(Bitboard bitboard) {

  unsigned b32;
  int result = 0;

  if (bitboard > 0xFFFFFFFF)
  {
      bitboard >>= 32;
      result = 32;
  }

  b32 = unsigned(bitboard);

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

  return Square(result + MS1BTable[b32]);
}

#endif // ifndef USE_BSFQ


/// Bitboards::pretty() returns an ASCII representation of a bitboard to be
/// printed to standard output. This is sometimes useful for debugging.

const std::string Bitboards::pretty(Bitboard bitboard) {

  std::string s = "+---+---+---+---+---+---+---+---+\n";

  for (Rank rank = RANK_8; rank >= RANK_1; --rank)
  {
      for (File file = FILE_A; file <= FILE_H; ++file)
          s.append(bitboard & make_square(file, rank) ? "| X " : "|   ");

      s.append("|\n+---+---+---+---+---+---+---+---+\n");
  }

  return s;
}


/// Bitboards::init() initializes various bitboard tables. It is called at
/// startup and relies on global objects to be already zero-initialized.

void Bitboards::init() {

  for (Square square = SQ_A1; square <= SQ_H8; ++square)
  {
      SquareBB[square] = 1ULL << square;
      BSFTable[bsf_index(SquareBB[square])] = square;
  }

  for (Bitboard bitboard = 1; bitboard < 256; ++bitboard)
      MS1BTable[bitboard] = more_than_one(bitboard) ? MS1BTable[bitboard - 1] : lsb(bitboard);

  for (File file = FILE_A; file <= FILE_H; ++file)
      FileBB[file] = file > FILE_A ? FileBB[file - 1] << 1 : FileABB;

  for (Rank rank = RANK_1; rank <= RANK_8; ++rank)
      RankBB[rank] = rank > RANK_1 ? RankBB[rank - 1] << 8 : Rank1BB;

  for (File file = FILE_A; file <= FILE_H; ++file)
      AdjacentFilesBB[file] = (file > FILE_A ? FileBB[file - 1] : 0) | (file < FILE_H ? FileBB[file + 1] : 0);

  for (Rank rank = RANK_1; rank < RANK_8; ++rank)
      InFrontBB[WHITE][rank] = ~(InFrontBB[BLACK][rank + 1] = InFrontBB[BLACK][rank] | RankBB[rank]);

  for (Color color = WHITE; color <= BLACK; ++color)
      for (Square square = SQ_A1; square <= SQ_H8; ++square)
      {
          ForwardBB[color][square]      = InFrontBB[color][rank_of(square)] & FileBB[file_of(square)];
          PawnAttackSpan[color][square] = InFrontBB[color][rank_of(square)] & AdjacentFilesBB[file_of(square)];
          PassedPawnMask[color][square] = ForwardBB[color][square] | PawnAttackSpan[color][square];
      }

  for (Square square1 = SQ_A1; square1 <= SQ_H8; ++square1)
      for (Square square2 = SQ_A1; square2 <= SQ_H8; ++square2)
          if (square1 != square2)
          {
              SquareDistance[square1][square2] = std::max(distance<File>(square1, square2), distance<Rank>(square1, square2));
              DistanceRingsBB[square1][SquareDistance[square1][square2] - 1] |= square2;
          }

  int steps[][9] = { {}, { 7, 9 }, { 17, 15, 10, 6, -6, -10, -15, -17 },
                     {}, {}, {}, { 9, 7, -7, -9, 8, 1, -1, -8 } };

  for (Color color = WHITE; color <= BLACK; ++color)
      for (PieceType pieceType = PAWN; pieceType <= KING; ++pieceType)
          for (Square square = SQ_A1; square <= SQ_H8; ++square)
              for (int i = 0; steps[pieceType][i]; ++i)
              {
                  Square to = square + Square(color == WHITE ? steps[pieceType][i] : -steps[pieceType][i]);

                  if (is_ok(to) && distance(square, to) < 3)
                      StepAttacksBB[make_piece(color, pieceType)][square] |= to;
              }

  Square RookDeltas[] = { DELTA_N,  DELTA_E,  DELTA_S,  DELTA_W  };
  Square BishopDeltas[] = { DELTA_NE, DELTA_SE, DELTA_SW, DELTA_NW };

  init_magics(RookTable, RookAttacks, RookMagics, RookMasks, RookShifts, RookDeltas, magic_index<ROOK>);
  init_magics(BishopTable, BishopAttacks, BishopMagics, BishopMasks, BishopShifts, BishopDeltas, magic_index<BISHOP>);

  for (Square square1 = SQ_A1; square1 <= SQ_H8; ++square1)
  {
      PseudoAttacks[QUEEN][square1]  = PseudoAttacks[BISHOP][square1] = attacks_bb<BISHOP>(square1, 0);
      PseudoAttacks[QUEEN][square1] |= PseudoAttacks[  ROOK][square1] = attacks_bb<  ROOK>(square1, 0);

      for (Square square2 = SQ_A1; square2 <= SQ_H8; ++square2)
      {
          Piece piece = (PseudoAttacks[BISHOP][square1] & square2) ? W_BISHOP :
                        (PseudoAttacks[ROOK][square1]   & square2) ? W_ROOK   : NO_PIECE;

          if (piece == NO_PIECE)
              continue;

          LineBB[square1][square2] = (attacks_bb(piece, square1, 0) & attacks_bb(piece, square2, 0)) | square1 | square2;
          BetweenBB[square1][square2] = attacks_bb(piece, square1, SquareBB[square2]) & attacks_bb(piece, square2, SquareBB[square1]);
      }
  }
}


namespace {

  Bitboard sliding_attack(Square deltas[], Square s, Bitboard occupied) {

    Bitboard attack = 0;

    for (int i = 0; i < 4; ++i)
        for (Square square = s + deltas[i];
             is_ok(square) && distance(square, square - deltas[i]) == 1;
             square += deltas[i])
        {
            attack |= square;

            if (occupied & square)
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

    int MagicBoosters[][RANK_NB] = { {  969, 1976, 2850,  542, 2069, 2852, 1708,  164 },
                                     { 3101,  552, 3555,  926,  834,   26, 2131, 1117 } };
    RKISS rk;
    Bitboard occupancy[4096], reference[4096], edges, bitboard;
    int i, size, booster;

    // attacks[square] is a pointer to the beginning of the attacks table for square 'square'
    attacks[SQ_A1] = table;

    for (Square square = SQ_A1; square <= SQ_H8; ++square)
    {
        // Board edges are not considered in the relevant occupancies
        edges = ((Rank1BB | Rank8BB) & ~rank_bb(square)) | ((FileABB | FileHBB) & ~file_bb(square));

        // Given a square 'square', the mask is the bitboard of sliding attacks from
        // 'square' computed on an empty board. The index must be big enough to contain
        // all the attacks for each possible subset of the mask and so is 2 power
        // the number of 1s of the mask. Hence we deduce the size of the shift to
        // apply to the 64 or 32 bits word to get the index.
        masks[square]  = sliding_attack(deltas, square, 0) & ~edges;
        shifts[square] = (Is64Bit ? 64 : 32) - popcount<Max15>(masks[square]);

        // Use Carry-Rippler trick to enumerate all subsets of masks[square] and
        // store the corresponding sliding attack bitboard in reference[].
        bitboard = size = 0;
        do {
            occupancy[size] = bitboard;
            reference[size] = sliding_attack(deltas, square, bitboard);

            if (HasPext)
                attacks[square][_pext_u64(bitboard, masks[square])] = reference[size];

            size++;
            bitboard = (bitboard - masks[square]) & masks[square];
        } while (bitboard);

        // Set the offset for the table of the next square. We have individual
        // table sizes for each square with "Fancy Magic Bitboards".
        if (square < SQ_H8)
            attacks[square + 1] = attacks[square] + size;

        if (HasPext)
            continue;

        booster = MagicBoosters[Is64Bit][rank_of(square)];

        // Find a magic for square 'square' picking up an (almost) random number
        // until we find the one that passes the verification test.
        do {
            do
                magics[square] = rk.magic_rand<Bitboard>(booster);
            while (popcount<Max15>((magics[square] * masks[square]) >> 56) < 6);

            std::memset(attacks[square], 0, size * sizeof(Bitboard));

            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[square] database.
            // Note that we build up the database for square 'square' as a side
            // effect of verifying the magic.
            for (i = 0; i < size; ++i)
            {
                Bitboard& attack = attacks[square][index(square, occupancy[i])];

                if (attack && attack != reference[i])
                    break;

                assert(reference[i]);

                attack = reference[i];
            }
        } while (i < size);
    }
  }
}
