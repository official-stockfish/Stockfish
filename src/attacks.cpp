/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#include "attacks.h"

#include <array>

#include "misc.h"

namespace Stockfish::Attacks {

static constexpr Bitboard line_mask(Square sq, Direction d1, Direction d2) {
    Bitboard mask = 0;
    for (Direction d : {d1, d2})
    {
        Square s = sq;
        while (Bitboard dest = safe_destination(s, d))
        {
            mask |= dest;
            s += d;
        }
    }
    return mask;
}

// Sliding attacks within a rank, indexed by the slider's file and the
// 6 inner bits of the rank occupancy (edge squares never affect the
// attack set), yielding the 8-bit attack set on that rank
alignas(64) constexpr auto RankAttacks = []() {
    std::array<std::array<u8, 64>, FILE_NB> table{};
    for (int file = 0; file < 8; ++file)
        for (int occ6 = 0; occ6 < 64; ++occ6)
            table[file][occ6] = u8(sliding_attack(ROOK, Square(file), occ6 << 1));
    return table;
}();

static constexpr auto make_dual_magics() {
    std::array<DualMagic, SQUARE_NB> magics{};
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        DualMagic& m        = magics[s];
        m.maskFile          = line_mask(s, NORTH, SOUTH);
        m.maskDiag          = line_mask(s, NORTH_EAST, SOUTH_WEST);
        m.maskNone          = 0;
        m.maskAntidiag      = line_mask(s, NORTH_WEST, SOUTH_EAST);
        m.r                 = square_bb(s) * 2;
        m.rr                = square_bb(Square(63 - int(s))) * 2;
        m.rankAttacksLookup = RankAttacks[int(file_of(s))].data();
        m.shift             = 8 * int(rank_of(s));
    }
    return magics;
}

alignas(64) extern constexpr std::array<DualMagic, SQUARE_NB> DualMagics = make_dual_magics();

alignas(64) extern constexpr SquarePairBB LineBB = []() {
    SquarePairBB t{};
    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (PieceType pt : {BISHOP, ROOK})
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
                if (PseudoAttacks[pt][s1] & s2)
                    t[s1][s2] = (PseudoAttacks[pt][s1] & PseudoAttacks[pt][s2]) | s1 | s2;
    return t;
}();

alignas(64) extern constexpr SquarePairBB BetweenBB = []() {
    SquarePairBB t{};
    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            t[s1][s2] =
              (LineBB[s1][s2] & ((~Bitboard(0) << s1) ^ (~Bitboard(0) << s2)) & ~square_bb(s1))
              | s2;
    return t;
}();

alignas(64) extern constexpr SquarePairBB inverse_RayPassBB = []() {
    SquarePairBB t{};
    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            t[s2][s1] = LineBB[s1][s2] & (s2 > s1 ? ~Bitboard(0) << (s1 + 1) : square_bb(s1) - 1);
    return t;
}();

alignas(64) extern constexpr SquareBB DiagBB = []() {
    SquareBB t{};
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        t[s] = line_mask(s, NORTH_EAST, SOUTH_WEST) | s;
    return t;
}();

alignas(64) extern constexpr SquareBB AntiDiagBB = []() {
    SquareBB t{};
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        t[s] = line_mask(s, NORTH_WEST, SOUTH_EAST) | s;
    return t;
}();

}  // namespace Stockfish::Attacks
