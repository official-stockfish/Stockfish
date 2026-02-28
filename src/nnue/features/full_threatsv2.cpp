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

//Definition of input features FullThreatsv2 of NNUE evaluation function

#include "full_threatsv2.h"

#include <assert.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Features {

struct HelperOffsets {
    int cumulativePieceOffset, cumulativeOffset;
};

constexpr std::array<Piece, 12> AllPieces = {
  W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
};

template<PieceType PT>
constexpr auto make_piece_indices_type() {
    static_assert(PT != PieceType::PAWN);

    std::array<std::array<uint8_t, SQUARE_NB>, SQUARE_NB> out{};

    for (Square from = SQ_A1; from <= SQ_H8; ++from)
    {
        Bitboard attacks = PseudoAttacks[PT][from];

        for (Square to = SQ_A1; to <= SQ_H8; ++to)
        {
            out[from][to] = constexpr_popcount(((1ULL << to) - 1) & attacks);
        }
    }

    return out;
}

template<Piece P>
constexpr auto make_piece_indices_piece() {
    static_assert(type_of(P) == PieceType::PAWN);

    std::array<std::array<uint8_t, SQUARE_NB>, SQUARE_NB> out{};

    constexpr Color C = color_of(P);

    for (Square from = SQ_A1; from <= SQ_H8; ++from)
    {
        Bitboard attacks = PseudoAttacks[C][from];

        for (Square to = SQ_A1; to <= SQ_H8; ++to)
        {
            out[from][to] = constexpr_popcount(((1ULL << to) - 1) & attacks);
        }
    }

    return out;
}

constexpr auto index_lut2_array() {
    constexpr auto KNIGHT_ATTACKS = make_piece_indices_type<PieceType::KNIGHT>();
    constexpr auto BISHOP_ATTACKS = make_piece_indices_type<PieceType::BISHOP>();
    constexpr auto ROOK_ATTACKS   = make_piece_indices_type<PieceType::ROOK>();
    constexpr auto QUEEN_ATTACKS  = make_piece_indices_type<PieceType::QUEEN>();
    constexpr auto KING_ATTACKS   = make_piece_indices_type<PieceType::KING>();

    std::array<std::array<std::array<uint8_t, SQUARE_NB>, SQUARE_NB>, PIECE_NB> indices{};

    indices[W_PAWN] = make_piece_indices_piece<W_PAWN>();
    indices[B_PAWN] = make_piece_indices_piece<B_PAWN>();

    indices[W_KNIGHT] = KNIGHT_ATTACKS;
    indices[B_KNIGHT] = KNIGHT_ATTACKS;

    indices[W_BISHOP] = BISHOP_ATTACKS;
    indices[B_BISHOP] = BISHOP_ATTACKS;

    indices[W_ROOK] = ROOK_ATTACKS;
    indices[B_ROOK] = ROOK_ATTACKS;

    indices[W_QUEEN] = QUEEN_ATTACKS;
    indices[B_QUEEN] = QUEEN_ATTACKS;

    indices[W_KING] = KING_ATTACKS;
    indices[B_KING] = KING_ATTACKS;

    return indices;
}

constexpr auto init_threat_offsets() {
    std::array<HelperOffsets, PIECE_NB>                    indices{};
    std::array<std::array<IndexType, SQUARE_NB>, PIECE_NB> offsets{};

    int cumulativeOffset = 0;
    for (Piece piece : AllPieces)
    {
        int pieceIdx              = piece;
        int cumulativePieceOffset = 0;

        for (Square from = SQ_A1; from <= SQ_H8; ++from)
        {
            offsets[pieceIdx][from] = cumulativePieceOffset;

            if (type_of(piece) != PAWN)
            {
                Bitboard attacks = PseudoAttacks[type_of(piece)][from];
                cumulativePieceOffset += constexpr_popcount(attacks);
            }

            else if (from >= SQ_A2 && from <= SQ_H7)
            {
                Bitboard attacks = (pieceIdx < 8) ? pawn_attacks_bb<WHITE>(square_bb(from))
                                                  : pawn_attacks_bb<BLACK>(square_bb(from));
                cumulativePieceOffset += constexpr_popcount(attacks);
            }
        }

        indices[pieceIdx] = {cumulativePieceOffset, cumulativeOffset};

        cumulativeOffset += numValidTargets[pieceIdx] * cumulativePieceOffset;
    }

    return std::pair{indices, offsets};
}

constexpr auto threat_offsets = init_threat_offsets();
constexpr auto helper_offsets = threat_offsets.first;
// Lookup array for indexing threats
constexpr auto offsets = threat_offsets.second;

constexpr IndexType total_dimensions() {
    IndexType total = 0;

    for (Piece piece : AllPieces)
        total += numValidTargets[piece] * helper_offsets[piece].cumulativePieceOffset;

    return total;
}

static_assert(total_dimensions() == FullThreatsv2::Dimensions);

constexpr auto init_index_luts() {
    std::array<std::array<std::array<uint32_t, 2>, PIECE_NB>, PIECE_NB> indices{};

    for (Piece attacker : AllPieces)
    {
        std::array<int, PIECE_NB> targetBuckets{};
        for (auto& bucket : targetBuckets)
            bucket = -1;

        int nextTargetBucket = 0;

        for (Piece attacked : AllPieces)
        {
            PieceType attackerType = type_of(attacker);
            PieceType attackedType = type_of(attacked);

            int map = FullThreatsv2::map[attackerType - 1][attackedType - 1];
            bool excluded = map < 0 || (attacker == W_PAWN && attacked == B_PAWN);

            if (excluded)
            {
                indices[attacker][attacked][0] = FullThreatsv2::Dimensions;
                indices[attacker][attacked][1] = FullThreatsv2::Dimensions;
                continue;
            }

            Piece canonical = attacked;
            if (attackerType == attackedType && attackerType != PAWN)
                canonical = make_piece(color_of(attacker), attackerType);

            int& targetBucket = targetBuckets[canonical];
            if (targetBucket < 0)
                targetBucket = nextTargetBucket++;

            IndexType feature =
              helper_offsets[attacker].cumulativeOffset
              + targetBucket * helper_offsets[attacker].cumulativePieceOffset;

            bool semi_excluded              = attackerType == attackedType && attackerType != PAWN;
            indices[attacker][attacked][0] = feature;
            indices[attacker][attacked][1] = semi_excluded ? FullThreatsv2::Dimensions : feature;
        }

        assert(nextTargetBucket == numValidTargets[attacker]);
    }

    return indices;
}

// The final index is calculated from summing data found in these two LUTs, as well
// as offsets[attacker][from]

// [attacker][attacked][from < to]
constexpr auto index_lut1 = init_index_luts();
// [attacker][from][to]
constexpr auto index_lut2 = index_lut2_array();

// Index of a feature for a given king position and another piece on some square
inline sf_always_inline IndexType FullThreatsv2::make_index(
  Color perspective, Piece attacker, Square from, Square to, Piece attacked, Square ksq) {
    const std::int8_t orientation   = OrientTBL[ksq] ^ (56 * perspective);
    unsigned          from_oriented = uint8_t(from) ^ orientation;
    unsigned          to_oriented   = uint8_t(to) ^ orientation;
    bool              from_to_direc = from_oriented < to_oriented;

    // In the case when two pieces of the same type are attacking, one half of
    // the indices (in this case, the ones corresponding to from < to) are unused.
    // We can take advantage of this to merge the two threat planes by mapping one
    // to indices corresponding to from < to and the other to indices corresponding
    // to from > to. Here this is accomplished by swapping the oriented from, to
    // squares when the two pieces are opposites. This means that attacks of the
    // type X -> X use the usual from > to indices, and attacks of the type
    // X -> ~X now use the from < to indices. This cannot be applied to pawns,
    // since their attacks are not fully symmetrical.
    if ((attacker == ~attacked) && type_of(attacker) > PAWN)
        std::swap(from_oriented, to_oriented);

    std::int8_t swap              = 8 * perspective;
    unsigned    attacker_oriented = attacker ^ swap;
    unsigned    attacked_oriented = attacked ^ swap;

    return index_lut1[attacker_oriented][attacked_oriented][from_to_direc]
         + offsets[attacker_oriented][from_oriented]
         + index_lut2[attacker_oriented][from_oriented][to_oriented];
}

// Get a list of indices for active features in ascending order

void FullThreatsv2::append_active_indices(Color perspective, const Position& pos, IndexList& active) {
    Square   ksq      = pos.square<KING>(perspective);
    Bitboard occupied = pos.pieces();

    for (Color color : {WHITE, BLACK})
    {
        for (PieceType pt = PAWN; pt < KING; ++pt)
        {
            Color    c        = Color(perspective ^ color);
            Piece    attacker = make_piece(c, pt);
            Bitboard bb       = pos.pieces(c, pt);

            if (pt == PAWN)
            {
                auto right = (c == WHITE) ? NORTH_EAST : SOUTH_WEST;
                auto left  = (c == WHITE) ? NORTH_WEST : SOUTH_EAST;
                auto attacks_left =
                  ((c == WHITE) ? shift<NORTH_EAST>(bb) : shift<SOUTH_WEST>(bb)) & occupied;
                auto attacks_right =
                  ((c == WHITE) ? shift<NORTH_WEST>(bb) : shift<SOUTH_EAST>(bb)) & occupied;

                while (attacks_left)
                {
                    Square    to       = pop_lsb(attacks_left);
                    Square    from     = to - right;
                    Piece     attacked = pos.piece_on(to);
                    IndexType index    = make_index(perspective, attacker, from, to, attacked, ksq);

                    if (index < Dimensions)
                        active.push_back(index);
                }

                while (attacks_right)
                {
                    Square    to       = pop_lsb(attacks_right);
                    Square    from     = to - left;
                    Piece     attacked = pos.piece_on(to);
                    IndexType index    = make_index(perspective, attacker, from, to, attacked, ksq);

                    if (index < Dimensions)
                        active.push_back(index);
                }
            }
            else
            {
                while (bb)
                {
                    Square   from    = pop_lsb(bb);
                    Bitboard attacks = (attacks_bb(pt, from, occupied)) & occupied;

                    while (attacks)
                    {
                        Square    to       = pop_lsb(attacks);
                        Piece     attacked = pos.piece_on(to);
                        IndexType index =
                          make_index(perspective, attacker, from, to, attacked, ksq);

                        if (index < Dimensions)
                            active.push_back(index);
                    }
                }
            }
        }
    }
}

// Get a list of indices for recently changed features

void FullThreatsv2::append_changed_indices(Color                   perspective,
                                         Square                  ksq,
                                         const DiffType&         diff,
                                         IndexList&              removed,
                                         IndexList&              added,
                                         FusedUpdateData*        fusedData,
                                         bool                    first,
                                         const ThreatWeightType* prefetchBase,
                                         IndexType               prefetchStride) {

    for (const auto& dirty : diff.list)
    {
        auto attacker = dirty.pc();
        auto attacked = dirty.threatened_pc();
        auto from     = dirty.pc_sq();
        auto to       = dirty.threatened_sq();
        auto add      = dirty.add();

        if (fusedData)
        {
            if (from == fusedData->dp2removed)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedOriginBoard |= to;
                        continue;
                    }
                }
                else if (fusedData->dp2removedOriginBoard & to)
                    continue;
            }

            if (to != SQ_NONE && to == fusedData->dp2removed)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedTargetBoard |= from;
                        continue;
                    }
                }
                else if (fusedData->dp2removedTargetBoard & from)
                    continue;
            }
        }

        auto&           insert = add ? added : removed;
        const IndexType index  = make_index(perspective, attacker, from, to, attacked, ksq);

        if (index < Dimensions)
        {
            if (prefetchBase)
                prefetch<PrefetchRw::READ, PrefetchLoc::LOW>(
                  prefetchBase + static_cast<std::ptrdiff_t>(index) * prefetchStride);
            insert.push_back(index);
        }
    }
}

bool FullThreatsv2::requires_refresh(const DiffType& diff, Color perspective) {
    return perspective == diff.us && (int8_t(diff.ksq) & 0b100) != (int8_t(diff.prevKsq) & 0b100);
}

}  // namespace Stockfish::Eval::NNUE::Features
