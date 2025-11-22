/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

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
#include <initializer_list>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Features {

// Lookup array for indexing threats
IndexType offsets[PIECE_NB][SQUARE_NB];

struct HelperOffsets {
    int cumulativePieceOffset, cumulativeOffset;
};
std::array<HelperOffsets, PIECE_NB> helper_offsets;

// Information on a particular pair of pieces and whether they should be excluded
struct PiecePairData {
    // Layout: bits 8..31 are the index contribution of this piece pair, bits 0 and 1 are exclusion info
    uint32_t data;
    PiecePairData() {}
    PiecePairData(bool excluded_pair, bool semi_excluded_pair, IndexType feature_index_base) {
        data =
          excluded_pair << 1 | (semi_excluded_pair && !excluded_pair) | feature_index_base << 8;
    }
    // lsb: excluded if from < to; 2nd lsb: always excluded
    uint8_t   excluded_pair_info() const { return (uint8_t) data; }
    IndexType feature_index_base() const { return data >> 8; }
};

constexpr std::array<Piece, 12> AllPieces = {
  W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
};

// The final index is calculated from summing data found in these two LUTs, as well
// as offsets[attacker][from]
PiecePairData index_lut1[PIECE_NB][PIECE_NB];              // [attacker][attacked]
uint8_t       index_lut2[PIECE_NB][SQUARE_NB][SQUARE_NB];  // [attacker][from][to]

static void init_index_luts() {
    for (Piece attacker : AllPieces)
    {
        for (Piece attacked : AllPieces)
        {
            bool      enemy        = (attacker ^ attacked) == 8;
            PieceType attackerType = type_of(attacker);
            PieceType attackedType = type_of(attacked);

            int  map           = FullThreats::map[attackerType - 1][attackedType - 1];
            bool semi_excluded = attackerType == attackedType && (enemy || attackerType != PAWN);
            IndexType feature  = helper_offsets[attacker].cumulativeOffset
                              + (color_of(attacked) * (numValidTargets[attacker] / 2) + map)
                                  * helper_offsets[attacker].cumulativePieceOffset;

            bool excluded                  = map < 0;
            index_lut1[attacker][attacked] = PiecePairData(excluded, semi_excluded, feature);
        }
    }

    for (Piece attacker : AllPieces)
    {
        for (int from = 0; from < SQUARE_NB; ++from)
        {
            for (int to = 0; to < SQUARE_NB; ++to)
            {
                Bitboard attacks               = attacks_bb(attacker, Square(from));
                index_lut2[attacker][from][to] = popcount((square_bb(Square(to)) - 1) & attacks);
            }
        }
    }
}

void init_threat_offsets() {
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
                Bitboard attacks = attacks_bb(piece, from, 0ULL);
                cumulativePieceOffset += popcount(attacks);
            }

            else if (from >= SQ_A2 && from <= SQ_H7)
            {
                Bitboard attacks = (pieceIdx < 8) ? pawn_attacks_bb<WHITE>(square_bb(from))
                                                  : pawn_attacks_bb<BLACK>(square_bb(from));
                cumulativePieceOffset += popcount(attacks);
            }
        }

        helper_offsets[pieceIdx] = {cumulativePieceOffset, cumulativeOffset};

        cumulativeOffset += numValidTargets[pieceIdx] * cumulativePieceOffset;
    }

    init_index_luts();
}

// Index of a feature for a given king position and another piece on some square
inline sf_always_inline IndexType FullThreats::make_index(
  Color perspective, Piece attacker, Square from, Square to, Piece attacked, Square ksq) {
    const std::int8_t orientation   = OrientTBL[ksq] ^ (56 * perspective);
    unsigned          from_oriented = uint8_t(from) ^ orientation;
    unsigned          to_oriented   = uint8_t(to) ^ orientation;

    std::int8_t swap              = 8 * perspective;
    unsigned    attacker_oriented = attacker ^ swap;
    unsigned    attacked_oriented = attacked ^ swap;

    const auto piecePairData = index_lut1[attacker_oriented][attacked_oriented];

    const bool less_than = from_oriented < to_oriented;
    if ((piecePairData.excluded_pair_info() + less_than) & 2)
        return FullThreats::Dimensions;

    const IndexType index = piecePairData.feature_index_base()
                          + offsets[attacker_oriented][from_oriented]
                          + index_lut2[attacker_oriented][from_oriented][to_oriented];
    sf_assume(index < Dimensions);
    return index;
}

// Get a list of indices for active features in ascending order

void FullThreats::append_active_indices(Color perspective, const Position& pos, IndexList& active) {
    Square   ksq      = pos.square<KING>(perspective);
    Bitboard occupied = pos.pieces();

    for (Color color : {WHITE, BLACK})
    {
        for (PieceType pt = PAWN; pt <= KING; ++pt)
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

void FullThreats::append_changed_indices(Color            perspective,
                                         Square           ksq,
                                         const DiffType&  diff,
                                         IndexList&       removed,
                                         IndexList&       added,
                                         FusedUpdateData* fusedData,
                                         bool             first) {

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
                        fusedData->dp2removedOriginBoard |= square_bb(to);
                        continue;
                    }
                }
                else if (fusedData->dp2removedOriginBoard & square_bb(to))
                    continue;
            }

            if (to != SQ_NONE && to == fusedData->dp2removed)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedTargetBoard |= square_bb(from);
                        continue;
                    }
                }
                else if (fusedData->dp2removedTargetBoard & square_bb(from))
                    continue;
            }
        }

        auto&           insert = add ? added : removed;
        const IndexType index  = make_index(perspective, attacker, from, to, attacked, ksq);

        if (index < Dimensions)
            insert.push_back(index);
    }
}

bool FullThreats::requires_refresh(const DiffType& diff, Color perspective) {
    return perspective == diff.us && (int8_t(diff.ksq) & 0b100) != (int8_t(diff.prevKsq) & 0b100);
}

}  // namespace Stockfish::Eval::NNUE::Features
