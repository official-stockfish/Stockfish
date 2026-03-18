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
void HalfKAv2_hm::write_indices(const std::array<Piece, SQUARE_NB>& oldPieces,
                                const std::array<Piece, SQUARE_NB>& newPieces,
                                Bitboard                            removedBB,
                                Bitboard                            addedBB,
                                Color                               perspective,
                                Square                              ksq,
                                IndexList&                          removed,
                                IndexList&                          added) {

    auto* write_removed = removed.make_space(popcount(removedBB));
    auto* write_added   = added.make_space(popcount(addedBB));

    const __m512i vecOldPieces = _mm512_loadu_si512(oldPieces.data());
    const __m512i vecNewPieces = _mm512_loadu_si512(newPieces.data());

    static constexpr uint16_t psiTable[COLOR_NB][32] = {
      {PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE,
       PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE,
       PS_NONE, PS_NONE,   PS_NONE,     PS_NONE,     PS_NONE,   PS_NONE,    PS_NONE, PS_NONE,
       PS_NONE, PS_NONE,   PS_NONE,     PS_NONE,     PS_NONE,   PS_NONE,    PS_NONE, PS_NONE},

      {PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE,
       PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE,
       PS_NONE, PS_NONE,   PS_NONE,     PS_NONE,     PS_NONE,   PS_NONE,    PS_NONE, PS_NONE,
       PS_NONE, PS_NONE,   PS_NONE,     PS_NONE,     PS_NONE,   PS_NONE,    PS_NONE, PS_NONE}};
    const __m512i psi = _mm512_loadu_si512(psiTable[perspective]);

    const __m512i allSquares = _mm512_set_epi8(
      63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41,
      40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18,
      17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);

    const uint16_t flip   = 56 * perspective;
    const __m512i  orient = _mm512_set1_epi16((uint16_t) OrientTBL[ksq] ^ flip);
    const __m512i  bucket = _mm512_set1_epi16((uint16_t) KingBuckets[int(ksq) ^ flip]);

    __m512i removed_squares       = _mm512_maskz_compress_epi8(removedBB, allSquares);
    __m512i removed_pieces        = _mm512_permutexvar_epi8(removed_squares, vecOldPieces);
    removed_squares               = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(removed_squares));
    removed_pieces                = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(removed_pieces));
    const __m512i removed_psi     = _mm512_permutexvar_epi16(removed_pieces, psi);
    __m512i       removed_indices = _mm512_xor_si512(removed_squares, orient);
    removed_indices               = _mm512_add_epi16(removed_indices, removed_psi);
    removed_indices               = _mm512_add_epi16(removed_indices, bucket);

    __m512i added_squares       = _mm512_maskz_compress_epi8(addedBB, allSquares);
    __m512i added_pieces        = _mm512_permutexvar_epi8(added_squares, vecNewPieces);
    added_squares               = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(added_squares));
    added_pieces                = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(added_pieces));
    const __m512i added_psi     = _mm512_permutexvar_epi16(added_pieces, psi);
    __m512i       added_indices = _mm512_xor_si512(added_squares, orient);
    added_indices               = _mm512_add_epi16(added_indices, added_psi);
    added_indices               = _mm512_add_epi16(added_indices, bucket);

    const __m512i removed_indices0 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(removed_indices));
    const __m512i removed_indices1 =
      _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(removed_indices, 1));
    _mm512_storeu_si512(write_removed, removed_indices0);
    _mm512_storeu_si512(write_removed + 16, removed_indices1);

    const __m512i added_indices0 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(added_indices));
    const __m512i added_indices1 =
      _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(added_indices, 1));
    _mm512_storeu_si512(write_added, added_indices0);
    _mm512_storeu_si512(write_added + 16, added_indices1);
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
