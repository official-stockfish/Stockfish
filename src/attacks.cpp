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

namespace {

Bitboard LineBB[SQUARE_NB][SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard RayPassBB[SQUARE_NB][SQUARE_NB];

#ifdef USE_DUAL_HYPERBOLA_QUINT
alignas(64) DualMagic DualMagics[SQUARE_NB];
#else
alignas(64) Magic Magics[SQUARE_NB][2];
#endif

}

#ifdef USE_PEXT
using MagicMask = u16;
#else
using MagicMask = Bitboard;
#endif

[[maybe_unused]] static Bitboard line_mask(Square sq, Direction d1, Direction d2) {
    Bitboard mask = 0, dest;
    for (Direction d : {d1, d2})
    {
        Square s = sq;
        while ((dest = safe_destination(s, d)))
        {
            mask |= dest;
            s += d;
        }
    }
    return mask;
}

#ifdef USE_HYPERBOLA_QUINT
static void init_magics(Magic magics[][2]) {
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        Magic& rook = magics[s][ROOK - BISHOP];
        rook.mask1  = line_mask(s, NORTH, SOUTH);
        rook.mask2  = line_mask(s, EAST, WEST);

        Magic& bishop = magics[s][BISHOP - BISHOP];
        bishop.mask1  = line_mask(s, NORTH_EAST, SOUTH_WEST);
        bishop.mask2  = line_mask(s, NORTH_WEST, SOUTH_EAST);
    }
}

#elif defined(USE_DUAL_HYPERBOLA_QUINT)

// Sliding attacks within a rank, indexed by the slider's file and the
// 8-bit rank occupancy, yielding the 8-bit attack set on that rank
constexpr auto RankAttacks = []() {
    std::array<std::array<u8, 256>, FILE_NB> table{};
    for (int file = 0; file < 8; ++file)
        for (int occ = 0; occ < 256; ++occ)
            table[file][occ] = u8(sliding_attack(ROOK, Square(file), occ));
    return table;
}();

static void init_dual_magics(DualMagic magics[]) {
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
}

#else

namespace {
[[maybe_unused]] constexpr Bitboard constexpr_pext(Bitboard b, Bitboard m) {
    Bitboard result = 0, bit = 0;
    while (m)
    {
        Bitboard last = m & -m;
        result |= bool(b & last) << bit++;
        m ^= last;
    }
    return result;
}

    #ifdef USE_COMPTIME_ATTACKS
constexpr
    #endif
  void
  init_magics(PieceType             pt,
              MagicMask             table[],
              Magic                 magics[][2],
              [[maybe_unused]] bool tableAlreadyInit) {
    #if !defined(USE_COMPTIME_ATTACKS)
    tableAlreadyInit = false;
    #endif

    #ifndef USE_PEXT
    int seeds[][RANK_NB] = {{8977, 44560, 54343, 38998, 5731, 95205, 104912, 17020},
                            {728, 10316, 55013, 32803, 12281, 15100, 16645, 255}};

    Bitboard occupancy[4096];
    int      epoch[4096] = {}, cnt = 0;
    Bitboard reference[4096] = {};
    #endif
    int size = 0;

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));

        Magic&   m       = magics[s][pt - BISHOP];
        Bitboard attacks = sliding_attack(pt, s, 0);
        m.mask           = attacks & ~edges;
    #ifdef USE_PEXT
        m.pseudoAttacks = attacks;
    #else
        m.shift = (Is64Bit ? 64 : 32) - popcount(m.mask);
    #endif
        m.attacks = s == SQ_A1 ? table : magics[s - 1][pt - BISHOP].attacks + size;
        size      = 0;

        Bitboard                  b           = 0;
        [[maybe_unused]] Bitboard prevSliding = -1;
        do
        {
    #ifdef USE_PEXT
            if (!tableAlreadyInit)
            {
                Bitboard sliding = sliding_attack(pt, s, b);
                m.attacks[size] =
                  sliding != prevSliding ? constexpr_pext(sliding, attacks) : m.attacks[size - 1];
                prevSliding = sliding;
            }
    #else
            occupancy[size] = b;
            reference[size] = sliding_attack(pt, s, b);
    #endif

            size++;
            b = (b - m.mask) & m.mask;
        } while (b);

    #ifndef USE_PEXT
        PRNG rng(seeds[Is64Bit][rank_of(s)]);

        for (int i = 0; i < size;)
        {
            for (m.magic = 0; popcount((m.magic * m.mask) >> 56) < 6;)
                m.magic = rng.sparse_rand<Bitboard>();

            for (++cnt, i = 0; i < size; ++i)
            {
                unsigned idx = m.index(occupancy[i]);

                if (epoch[idx] < cnt)
                {
                    epoch[idx]     = cnt;
                    m.attacks[idx] = reference[i];
                }
                else if (m.attacks[idx] != reference[i])
                    break;
            }
        }
    #endif
    }
}

    #if defined(USE_COMPTIME_ATTACKS) && defined(USE_PEXT)
constexpr auto RookTable = []() {
    std::array<u16, 0x19000> result{};
    Magic                    magics[64][2] = {};
    init_magics(ROOK, result.data(), magics, false);
    return result;
}();
constexpr auto BishopTable = []() {
    std::array<u16, 0x1480> result{};
    Magic                   magics[64][2] = {};
    init_magics(BISHOP, result.data(), magics, false);
    return result;
}();
    #elif !defined(USE_DUAL_HYPERBOLA_QUINT) && !defined(USE_HYPERBOLA_QUINT)
std::array<MagicMask, 0x19000> RookTable;
std::array<MagicMask, 0x1480>  BishopTable;
    #endif
}

#endif

void init() {

#ifdef USE_HYPERBOLA_QUINT
    init_magics(Magics);
#elif defined(USE_DUAL_HYPERBOLA_QUINT)
    init_dual_magics(DualMagics);
#else
    init_magics(ROOK, const_cast<MagicMask*>(RookTable.data()), Magics, true);
    init_magics(BISHOP, const_cast<MagicMask*>(BishopTable.data()), Magics, true);
#endif

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
    {
        for (PieceType pt : {BISHOP, ROOK})
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                if (PseudoAttacks[pt][s1] & s2)
                {
                    LineBB[s1][s2] = (attacks_bb(pt, s1, 0) & attacks_bb(pt, s2, 0)) | s1 | s2;
                    BetweenBB[s1][s2] =
                      (attacks_bb(pt, s1, square_bb(s2)) & attacks_bb(pt, s2, square_bb(s1)));
                    RayPassBB[s1][s2] =
                      attacks_bb(pt, s1, 0) & (attacks_bb(pt, s2, square_bb(s1)) | s2);
                }
                BetweenBB[s1][s2] |= s2;
            }
    }
}

#ifdef USE_DUAL_HYPERBOLA_QUINT
const DualMagic& dual_magic(Square s) { return DualMagics[s]; }
#else
const Magic& magic(Square s, PieceType pt) {
    assert((pt == BISHOP || pt == ROOK) && is_ok(s));
    return Magics[s][pt - BISHOP];
}
#endif

Bitboard line_bb(Square s1, Square s2) {
    assert(is_ok(s1) && is_ok(s2));
    return LineBB[s1][s2];
}

Bitboard between_bb(Square s1, Square s2) {
    assert(is_ok(s1) && is_ok(s2));
    return BetweenBB[s1][s2];
}

Bitboard ray_pass_bb(Square s1, Square s2) {
    assert(is_ok(s1) && is_ok(s2));
    return RayPassBB[s1][s2];
}

}  // namespace Stockfish::Attacks
