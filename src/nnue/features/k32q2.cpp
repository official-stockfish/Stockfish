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

//Definition of input features K32Q2 of NNUE evaluation function

#include "k32q2.h"

#include <array>

#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"

#if defined(USE_AVX512ICL)
    #include "../../bitboard.h"
#endif

namespace Stockfish::Eval::NNUE::Features {

#if defined(USE_AVX512ICL)
void K32Q2::write_indices(const std::array<Piece, SQUARE_NB>& oldPieces,
                          const std::array<Piece, SQUARE_NB>& newPieces,
                          Bitboard                            removedBB,
                          Bitboard                            addedBB,
                          Color                               perspective,
                          Square                              ksq,
                          bool                                opponent_has_queen,
                          IndexList&                          removed,
                          IndexList&                          added) {

    auto* write_removed = removed.make_space(popcount(removedBB));
    auto* write_added   = added.make_space(popcount(addedBB));

    const __m512i vecOldPieces = _mm512_loadu_si512(oldPieces.data());
    const __m512i vecNewPieces = _mm512_loadu_si512(newPieces.data());

    // PieceSquareIndex, KingBuckets, and queen offset are multiples of 64, while s and orient
    // use only the low six bits. Therefore no carry crosses bit 6, and
    // (s ^ orient) + psi[pc] + bucket + queen == s ^ (psi[pc] + bucket + queen + orient),
    // allowing the orientation to be folded into the per-piece lookup offset.
    const u16     flip         = 56 * perspective;
    const u16     orient       = u16(OrientTBL[ksq]) ^ flip;
    const u16     queen_offset = opponent_has_queen ? PS_NB : 0;
    const __m512i psi =
      _mm512_castsi256_si512(_mm256_loadu_si256((const __m256i*) PieceSquareIndex[perspective]));
    const __m512i psi_plus_offset =
      _mm512_add_epi16(psi, _mm512_set1_epi16(u16(KingBuckets[int(ksq) ^ flip] + queen_offset + orient)));

    __m512i removed_squares = _mm512_maskz_compress_epi8(removedBB, AllSquares);
    __m512i added_squares   = _mm512_maskz_compress_epi8(addedBB, AllSquares);
    __m512i removed_pieces  = _mm512_maskz_compress_epi8(removedBB, vecOldPieces);
    __m512i added_pieces    = _mm512_maskz_compress_epi8(addedBB, vecNewPieces);

    removed_squares = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(removed_squares));
    added_squares   = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(added_squares));
    removed_pieces  = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(removed_pieces));
    added_pieces    = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(added_pieces));

    const __m512i removed_indices =
      _mm512_xor_si512(removed_squares, _mm512_permutexvar_epi16(removed_pieces, psi_plus_offset));
    const __m512i added_indices =
      _mm512_xor_si512(added_squares, _mm512_permutexvar_epi16(added_pieces, psi_plus_offset));

    _mm512_storeu_si512(write_removed, removed_indices);
    _mm512_storeu_si512(write_added, added_indices);
}
#endif

// Index of a feature for a given king position and another piece on some square

IndexType K32Q2::make_index(Color perspective, Square s, Piece pc, Square ksq, bool opponent_has_queen) {
    alignas(64) static constexpr auto offsets = [] {
        std::array<std::array<u16, PIECE_NB>, 2 * COLOR_NB * SQUARE_NB> table{};
        for (int q = 0; q < 2; ++q)
            for (int c = 0; c < COLOR_NB; ++c)
                for (int sq = 0; sq < SQUARE_NB; ++sq)
                {
                    const u16 flip   = 56 * c;
                    const u16 orient = u16(OrientTBL[sq]) ^ flip;
                    const u16 queen_offset = q ? PS_NB : 0;
                    for (int pieceIndex = 0; pieceIndex < PIECE_NB; ++pieceIndex)
                        table[(q * COLOR_NB + c) * SQUARE_NB + sq][pieceIndex] =
                          PieceSquareIndex[c][pieceIndex] + KingBuckets[sq ^ flip] + queen_offset + orient;
                }
        return table;
    }();

    return IndexType(s) ^ offsets[(int(opponent_has_queen) * COLOR_NB + perspective) * SQUARE_NB + ksq][pc];
}

// Get a list of indices for recently changed features

void K32Q2::append_changed_indices(
  Color perspective, Square ksq, const DiffType& diff, bool opponent_has_queen, IndexList& removed, IndexList& added) {
    removed.push_back(make_index(perspective, diff.from, diff.pc, ksq, opponent_has_queen));
    if (diff.to != SQ_NONE)
        added.push_back(make_index(perspective, diff.to, diff.pc, ksq, opponent_has_queen));

    if (diff.remove_sq != SQ_NONE)
        removed.push_back(make_index(perspective, diff.remove_sq, diff.remove_pc, ksq, opponent_has_queen));

    if (diff.add_sq != SQ_NONE)
        added.push_back(make_index(perspective, diff.add_sq, diff.add_pc, ksq, opponent_has_queen));
}

bool K32Q2::requires_refresh(const DiffType& diff, Color perspective) {
    if (diff.pc == make_piece(perspective, KING))
        return true;

    Color opponent = ~perspective;
    if (   (diff.remove_sq != SQ_NONE && diff.remove_pc == make_piece(opponent, QUEEN))
        || (diff.add_sq != SQ_NONE && diff.add_pc == make_piece(opponent, QUEEN)))
        return true;

    return false;
}

}  // namespace Stockfish::Eval::NNUE::Features
