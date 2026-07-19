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

#include "pp_3wide.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "full_threats.h"
#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Features {

constexpr IndexType make_pawn_id(Color color, Square square) {
    assert(square >= SQ_A2 && square <= SQ_H7);
    return 48 * int(color) + square - SQ_A2;
}

#ifdef USE_AVX512ICL
static inline __m256i pp_idx_epi16(__m256i a, __m256i b) {
    const __m256i hi   = _mm256_max_epu16(a, b);
    const __m256i lo   = _mm256_min_epu16(a, b);
    const __m256i prod = _mm256_mullo_epi16(hi, _mm256_sub_epi16(hi, _mm256_set1_epi16(1)));
    return _mm256_add_epi16(_mm256_add_epi16(_mm256_srli_epi16(prod, 1), lo),
                            _mm256_set1_epi16(i16(PP_3Wide::IndexBase)));
}
#endif

inline sf_always_inline IndexType PP_3Wide::make_index(
  Color perspective, Color color, Square from, Square to, Color pairedColor, Square ksq) {
    const i8 orientation   = FullThreats::OrientTBL[ksq] ^ (56 * perspective);
    unsigned from_oriented = u8(from) ^ orientation;
    unsigned to_oriented   = u8(to) ^ orientation;

    Color color_oriented       = Color(color ^ perspective);
    Color pairedColor_oriented = Color(pairedColor ^ perspective);

    assert(from_oriented >= SQ_A2 && from_oriented <= SQ_H7);
    assert(to_oriented >= SQ_A2 && to_oriented <= SQ_H7);

    const IndexType idA = make_pawn_id(color_oriented, Square(from_oriented));
    const IndexType idB = make_pawn_id(pairedColor_oriented, Square(to_oriented));
    const IndexType hi  = std::max(idA, idB);
    const IndexType lo  = std::min(idA, idB);

    return hi * (hi - 1) / 2 + lo + IndexBase;
}

void PP_3Wide::append_active_indices(Color perspective, const Position& pos, IndexList& active) {
    const Square   ksq   = pos.square<KING>(perspective);
    const Bitboard white = pos.pieces(WHITE, PAWN);
    const Bitboard black = pos.pieces(BLACK, PAWN);

    Bitboard bb = white;
    while (bb)
    {
        Square         from = pop_lsb(bb);
        const Bitboard band = pawn_pair_bb(from);
        for (Bitboard ww = band & bb; ww;)
            active.push_back(make_index(perspective, WHITE, from, pop_lsb(ww), WHITE, ksq));
        for (Bitboard wb = band & black; wb;)
            active.push_back(make_index(perspective, WHITE, from, pop_lsb(wb), BLACK, ksq));
    }

    bb = black;
    while (bb)
    {
        Square         from = pop_lsb(bb);
        const Bitboard band = pawn_pair_bb(from);
        for (Bitboard bbk = band & bb; bbk;)
            active.push_back(make_index(perspective, BLACK, from, pop_lsb(bbk), BLACK, ksq));
    }
}

void PP_3Wide::append_changed_indices(Color                                    perspective,
                                      Square                                   ksq,
                                      const DiffType&                          diff,
                                      IndexList&                               removed,
                                      IndexList&                               added,
                                      [[maybe_unused]] const ThreatWeightType* prefetchBase,
                                      [[maybe_unused]] IndexType               prefetchStride) {

    const Bitboard whiteBefore = diff.before[WHITE];
    const Bitboard blackBefore = diff.before[BLACK];
    const Bitboard whiteAfter  = diff.after[WHITE];
    const Bitboard blackAfter  = diff.after[BLACK];

    if (whiteBefore == whiteAfter && blackBefore == blackAfter)
        return;

#ifdef USE_AVX512ICL
    const u8      orientation = u8(FullThreats::OrientTBL[ksq]) ^ u8(56 * perspective);
    const __m512i iota        = AllSquares;
    const __m512i adjusted =
      _mm512_sub_epi8(_mm512_xor_si512(iota, _mm512_set1_epi8(orientation)), _mm512_set1_epi8(8));

    auto generate = [&](Bitboard updatedW, Bitboard updatedB, Bitboard pawnsW, Bitboard pawnsB,
                        IndexList& out) {
        const Bitboard friendly = perspective == WHITE ? pawnsW : pawnsB;
        const Bitboard enemy    = perspective == WHITE ? pawnsB : pawnsW;
        const __m512i  ids      = _mm512_mask_blend_epi8(
          friendly, _mm512_add_epi8(adjusted, _mm512_set1_epi8(48)), adjusted);

        const Bitboard unchanged = (pawnsW | pawnsB) & ~(updatedW | updatedB);
        for (Bitboard u = updatedW | updatedB; u;)
        {
            const Square   a        = pop_lsb(u);
            const Bitboard partners = pawn_pair_bb(a) & (unchanged | u);
            const int      n        = popcount(partners);
            if (!n)
                continue;

            const u16     colorOff = (enemy & a) ? 48 : 0;
            const u16     aId      = u16(((u8(a) ^ orientation) - 8) + colorOff);
            const __m256i pids     = _mm256_cvtepu8_epi16(
              _mm512_castsi512_si128(_mm512_maskz_compress_epi8(partners, ids)));
            const __m256i feats = pp_idx_epi16(_mm256_set1_epi16(aId), pids);

            u16* w = out.make_space(n);
            _mm256_storeu_epi16(w, feats);
        }
    };
#else
    auto generate = [&](Bitboard updatedW, Bitboard updatedB, Bitboard pawnsW, Bitboard pawnsB,
                        IndexList& out) {
        auto push = [&](IndexType index) {
            if (prefetchBase)
                prefetch<PrefetchRw::READ, PrefetchLoc::LOW>(reinterpret_cast<const void*>(
                  reinterpret_cast<uintptr_t>(prefetchBase) + index * prefetchStride));
            out.push_back(index);
        };
        const Bitboard unchanged = (pawnsW | pawnsB) & ~(updatedW | updatedB);
        for (Bitboard u = updatedW | updatedB; u;)
        {
            const Square   a    = pop_lsb(u);
            const Bitboard mask = pawn_pair_bb(a) & (unchanged | u);
            const Color    aCol = (pawnsB & a) ? BLACK : WHITE;
            for (Bitboard pb = pawnsB & mask; pb;)
                push(make_index(perspective, aCol, a, pop_lsb(pb), BLACK, ksq));
            for (Bitboard pw = pawnsW & mask; pw;)
                push(make_index(perspective, aCol, a, pop_lsb(pw), WHITE, ksq));
        }
    };
#endif

    generate(whiteAfter & ~whiteBefore, blackAfter & ~blackBefore, whiteAfter, blackAfter, added);
    generate(whiteBefore & ~whiteAfter, blackBefore & ~blackAfter, whiteBefore, blackBefore,
             removed);
}

}  // namespace Stockfish::Eval::NNUE::Features
