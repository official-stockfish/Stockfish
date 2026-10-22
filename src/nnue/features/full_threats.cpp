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

//Definition of input features FullThreats of NNUE evaluation function

#include "full_threats.h"

#include <array>
#include <cstdint>
#include <initializer_list>

#include "../../attacks.h"
#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Features {

struct alignas(64) ThreatLUTs {
    // [attacker][attacked][from < to]
    std::array<std::array<std::array<u32, 2>, PIECE_NB>, PIECE_NB> lut1{};
    // [pt][from][to]
    std::array<std::array<std::array<u16, SQUARE_NB>, SQUARE_NB>, PIECE_TYPE_NB> lut2{};
};

constexpr std::array<Piece, 12> AllPieces = {
  W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
};

constexpr auto init_threat_luts() {
    ThreatLUTs                     tables{};
    std::array<u16, PIECE_TYPE_NB> offsets{};

    for (PieceType pt : {PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING})
    {
        u16 offset = 0;
        for (Square from = SQ_A1; from <= SQ_H8; ++from)
        {
            for (Square to = SQ_A1; to <= SQ_H8; ++to)
            {
                Bitboard attacks = (pt != PAWN) ? Attacks::PseudoAttacks[pt][from]
                                 : (to < from)  ? Attacks::PseudoAttacks[BLACK][from]
                                                : Attacks::PseudoAttacks[WHITE][from];
                tables.lut2[pt][from][to] =
                  offset + constexpr_popcount(((1ULL << to) - 1) & attacks);
            }

            if (pt != PAWN)
                offset += constexpr_popcount(Attacks::PseudoAttacks[pt][from]);
            else if (from >= SQ_A2 && from <= SQ_H7)
                offset += constexpr_popcount(Attacks::PseudoAttacks[WHITE][from]);
        }

        offsets[pt] = offset;
    }

    IndexType cumulativeOffset = 0;
    for (Piece attacker : AllPieces)
    {
        const PieceType attackerType          = type_of(attacker);
        const IndexType cumulativePieceOffset = offsets[attackerType];
        const IndexType validTargets          = numValidTargets[attacker];

        for (Piece attacked : AllPieces)
        {
            bool      enemy        = (attacker ^ attacked) == 8;
            PieceType attackedType = type_of(attacked);

            int  map           = FullThreats::map[attackerType - 1][attackedType - 1];
            bool semi_excluded = attackerType == attackedType && (enemy || attackerType != PAWN);
            IndexType feature =
              cumulativeOffset
              + (color_of(attacked) * (validTargets / 2) + map) * cumulativePieceOffset;

            bool excluded                      = map < 0;
            tables.lut1[attacker][attacked][0] = excluded ? FullThreats::Dimensions : feature;
            tables.lut1[attacker][attacked][1] =
              excluded || semi_excluded ? FullThreats::Dimensions : feature;
        }

        cumulativeOffset += validTargets * cumulativePieceOffset;
    }

    return tables;
}

constexpr ThreatLUTs threat_luts = init_threat_luts();

// Index of a feature for a given king position and another piece on some square
sf_always_inline IndexType FullThreats::make_index(
  Color perspective, Piece attacker, Square from, Square to, Piece attacked, Square ksq) {
    const i8 orientation   = OrientTBL[ksq] ^ (56 * perspective);
    unsigned from_oriented = u8(from) ^ orientation;
    unsigned to_oriented   = u8(to) ^ orientation;

    i8       swap              = 8 * perspective;
    unsigned attacker_oriented = attacker ^ swap;
    unsigned attacked_oriented = attacked ^ swap;

    return threat_luts.lut1[attacker_oriented][attacked_oriented][from_oriented < to_oriented]
         + threat_luts.lut2[type_of(attacker)][from_oriented][to_oriented];
}

// Get a list of indices for active features in ascending order

void FullThreats::append_active_indices(Color perspective, const Position& pos, IndexList& active) {
    const Square   ksq                = pos.square<KING>(perspective);
    const Bitboard occupied           = pos.pieces();
    const Bitboard pawnTargets        = pos.pieces(KNIGHT, ROOK);
    const Bitboard minorSliderTargets = pos.pieces(PAWN, KNIGHT, BISHOP, ROOK);
    const Bitboard queenTargets       = pos.pieces(PAWN, KNIGHT, BISHOP, ROOK, QUEEN);

    auto process_pawn_attacks = [&](Color c, Direction attkDir) {
        const Bitboard cPawns  = pos.pieces(c, PAWN);
        Bitboard       attacks = shift(cPawns, attkDir) & pawnTargets;
        while (attacks)
        {
            Square    to       = pop_lsb(attacks);
            Square    from     = to - attkDir;
            Piece     attacked = pos.piece_on(to);
            Piece     attacker = make_piece(c, PAWN);
            IndexType index    = make_index(perspective, attacker, from, to, attacked, ksq);
            active.push_back_if_lt(index, Dimensions);
        }
    };

    process_pawn_attacks(WHITE, NORTH_EAST);
    process_pawn_attacks(WHITE, NORTH_WEST);

    process_pawn_attacks(BLACK, SOUTH_WEST);
    process_pawn_attacks(BLACK, SOUTH_EAST);

    for (Color c : {WHITE, BLACK})
    {
        for (PieceType pt = KNIGHT; pt < KING; ++pt)
        {
            Piece    attacker = make_piece(c, pt);
            Bitboard bb       = pos.pieces(c, pt);
            Bitboard targets  = pt == KNIGHT || pt == QUEEN ? queenTargets : minorSliderTargets;
            while (bb)
            {
                Square   from    = pop_lsb(bb);
                Bitboard attacks = Attacks::attacks_bb(pt, from, occupied) & targets;
                while (attacks)
                {
                    Square    to       = pop_lsb(attacks);
                    Piece     attacked = pos.piece_on(to);
                    IndexType index    = make_index(perspective, attacker, from, to, attacked, ksq);
                    active.push_back_if_lt(index, Dimensions);
                }
            }
        }
    }
}

// Get a list of indices for recently changed features

void FullThreats::append_changed_indices(Color                   perspective,
                                         Square                  ksq,
                                         const DiffType&         diff,
                                         IndexList&              removed,
                                         IndexList&              added,
                                         const ThreatWeightType* prefetchBase,
                                         IndexType               prefetchStride) {
    const unsigned orient    = u8(OrientTBL[ksq]) ^ (56 * perspective);
    const unsigned pair_swap = 0x88 * perspective;
    const auto*    lut1_flat = &threat_luts.lut1[0][0];

    for (const auto& dirty : diff.list)
    {
        const u32      raw   = dirty.raw();
        const unsigned from  = u8(raw) ^ orient;
        const unsigned to    = u8(raw >> 8) ^ orient;
        const unsigned pair  = u8(raw >> 16);
        const auto&    plut2 = threat_luts.lut2[(pair >> 4) & 7];

        const IndexType index = lut1_flat[pair ^ pair_swap][from < to] + plut2[from][to];

        if (prefetchBase)
            prefetch<PrefetchRw::READ, PrefetchLoc::LOW>(reinterpret_cast<const void*>(
              reinterpret_cast<uintptr_t>(prefetchBase) + uintptr_t(index) * prefetchStride));

        if (raw >> 31)
            added.push_back_if_lt(index, Dimensions);
        else
            removed.push_back_if_lt(index, Dimensions);
    }
}

void FullThreats::append_changed_indices_both(Square                  white_ksq,
                                              Square                  black_ksq,
                                              const DiffType&         diff,
                                              IndexList&              white_removed,
                                              IndexList&              white_added,
                                              IndexList&              black_removed,
                                              IndexList&              black_added,
                                              const ThreatWeightType* prefetchBase,
                                              IndexType               prefetchStride) {
    const unsigned w_orient  = u8(OrientTBL[white_ksq]);
    const unsigned b_orient  = u8(OrientTBL[black_ksq]) ^ 56;
    const auto*    lut1_flat = &threat_luts.lut1[0][0];

    for (const auto& dirty : diff.list)
    {
        const u32      raw   = dirty.raw();
        const unsigned from  = u8(raw);
        const unsigned to    = u8(raw >> 8);
        const unsigned pair  = u8(raw >> 16);
        const auto&    plut2 = threat_luts.lut2[(pair >> 4) & 7];

        const unsigned  w_from      = from ^ w_orient;
        const unsigned  w_to        = to ^ w_orient;
        const IndexType white_index = lut1_flat[pair][w_from < w_to] + plut2[w_from][w_to];

        const unsigned  b_from      = from ^ b_orient;
        const unsigned  b_to        = to ^ b_orient;
        const IndexType black_index = lut1_flat[pair ^ 0x88][b_from < b_to] + plut2[b_from][b_to];

        if (prefetchBase)
        {
            prefetch<PrefetchRw::READ, PrefetchLoc::LOW>(reinterpret_cast<const void*>(
              reinterpret_cast<uintptr_t>(prefetchBase) + uintptr_t(white_index) * prefetchStride));
            prefetch<PrefetchRw::READ, PrefetchLoc::LOW>(reinterpret_cast<const void*>(
              reinterpret_cast<uintptr_t>(prefetchBase) + uintptr_t(black_index) * prefetchStride));
        }

        if (raw >> 31)
        {
            white_added.push_back_if_lt(white_index, Dimensions);
            black_added.push_back_if_lt(black_index, Dimensions);
        }
        else
        {
            white_removed.push_back_if_lt(white_index, Dimensions);
            black_removed.push_back_if_lt(black_index, Dimensions);
        }
    }
}

}  // namespace Stockfish::Eval::NNUE::Features
