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

//Definition of input features HalfKAv2_hm of NNUE evaluation function

#include "half_ka_v2_hm.h"

#include "../../bitboard.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Features {

#if defined(USE_AVX512ICL)

// Provides a vectorized implementation of HalfKAv2_hm::make_index for one bitboard,
// with a constant ksq and perspective.
struct HalfKAv2_hm::BatchIndexer {
    static constexpr auto BucketAndOrientTable = []() {
        struct Entry {
            uint16_t orient, bucket;
        };

        std::array<std::array<Entry, COLOR_NB>, SQUARE_NB> table{};
        for (Color c : {WHITE, BLACK})
        {
            int flip = c * 56;
            for (int sq = 0; sq < SQUARE_NB; ++sq)
                table[sq][c] = {uint16_t(HalfKAv2_hm::OrientTBL[sq] ^ flip),
                                uint16_t(HalfKAv2_hm::KingBuckets[sq ^ flip])};
        }
        return table;
    }();

    // Respectively:
    // OrientTBL[ksq] ^ flip; KingBuckets[ksq ^ flip]; PieceSquareIndex[perspective]
    __m512i orient, bucket, psi;

    BatchIndexer(Square ksq, Color perspective) {
        psi = _mm512_castsi256_si512(_mm256_load_si256(
          reinterpret_cast<const __m256i*>(HalfKAv2_hm::PieceSquareIndex[perspective])));

        auto entry = BucketAndOrientTable[ksq][perspective];
        orient     = _mm512_set1_epi16(entry.orient);
        bucket     = _mm512_set1_epi16(entry.bucket);
    }

    void write_indices(Bitboard selectBB, const Piece* board, uint16_t* out) const {
        // Extract selected squares and pieces, and convert to u16
        __m512i squares = _mm512_maskz_compress_epi8(selectBB, AllSquares);
        __m512i pieces  = _mm512_permutexvar_epi8(squares, _mm512_loadu_si512(board));

        squares = _mm512_cvtepu8_epi16(_mm512_castsi512_si256(squares));
        pieces  = _mm512_cvtepu8_epi16(_mm512_castsi512_si256(pieces));

        // indices = PieceSquareIndex[perspective][pc]
        __m512i indices = _mm512_permutexvar_epi16(pieces, psi);
        // indices += pc_sq ^ (OrientTBL[ksq] ^ flip)
        // logical OR is equivalent to addition -- operands are bitwise disjoint
        indices = _mm512_or_si512(indices, _mm512_xor_si512(squares, orient));
        // indices += KingBuckets[int(ksq) ^ flip]
        indices = _mm512_add_epi16(indices, bucket);

        _mm512_storeu_si512(out, indices);
    }
};

void HalfKAv2_hm::write_indices(const std::array<Piece, SQUARE_NB>& oldPieces,
                                const std::array<Piece, SQUARE_NB>& newPieces,
                                Bitboard                            removedBB,
                                Bitboard                            addedBB,
                                Color                               perspective,
                                Square                              ksq,
                                CompactIndexList&                   removed,
                                CompactIndexList&                   added) {

    uint16_t* writeRemoved = removed.make_space(popcount(removedBB));
    uint16_t* writeAdded   = added.make_space(popcount(addedBB));

    BatchIndexer indexer(ksq, perspective);

    indexer.write_indices(removedBB, oldPieces.data(), writeRemoved);
    indexer.write_indices(addedBB, newPieces.data(), writeAdded);
}
#else

void HalfKAv2_hm::write_indices(const std::array<Piece, SQUARE_NB>& oldPieces,
                                const std::array<Piece, SQUARE_NB>& newPieces,
                                Bitboard                            removedBB,
                                Bitboard                            addedBB,
                                Color                               perspective,
                                Square                              ksq,
                                CompactIndexList&                   removed,
                                CompactIndexList&                   added) {
    while (removedBB)
    {
        Square sq = pop_lsb(removedBB);
        removed.push_back(make_index(perspective, sq, oldPieces[sq], ksq));
    }
    while (addedBB)
    {
        Square sq = pop_lsb(addedBB);
        added.push_back(make_index(perspective, sq, newPieces[sq], ksq));
    }
}

#endif

// Index of a feature for a given king position and another piece on some square

IndexType HalfKAv2_hm::make_index(Color perspective, Square s, Piece pc, Square ksq) {
    const IndexType flip = 56 * perspective;
    return (IndexType(s) ^ OrientTBL[ksq] ^ flip) + PieceSquareIndex[perspective][pc]
         + KingBuckets[int(ksq) ^ flip];
}

// Get a list of indices for active features

void HalfKAv2_hm::append_active_indices(Color perspective, const Position& pos, IndexList& active) {
    Square   ksq = pos.square<KING>(perspective);
    Bitboard bb  = pos.pieces();
    while (bb)
    {
        Square s = pop_lsb(bb);
        active.push_back(make_index(perspective, s, pos.piece_on(s), ksq));
    }
}

// Get a list of indices for recently changed features

void HalfKAv2_hm::append_changed_indices(
  Color perspective, Square ksq, const DiffType& diff, IndexList& removed, IndexList& added) {
    removed.push_back(make_index(perspective, diff.from, diff.pc, ksq));
    if (diff.to != SQ_NONE)
        added.push_back(make_index(perspective, diff.to, diff.pc, ksq));

    if (diff.remove_sq != SQ_NONE)
        removed.push_back(make_index(perspective, diff.remove_sq, diff.remove_pc, ksq));

    if (diff.add_sq != SQ_NONE)
        added.push_back(make_index(perspective, diff.add_sq, diff.add_pc, ksq));
}

bool HalfKAv2_hm::requires_refresh(const DiffType& diff, Color perspective) {
    return diff.pc == make_piece(perspective, KING);
}

}  // namespace Stockfish::Eval::NNUE::Features
