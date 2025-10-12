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

#include "nnue_accumulator.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <type_traits>

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_feature_transformer.h"  // IWYU pragma: keep
#include "simd.h"

namespace Stockfish::Eval::NNUE {

using namespace SIMD;

namespace {

template<Color Perspective, IndexType TransformedFeatureDimensions>
void double_inc_update(const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState&                                       middle_state,
                       AccumulatorState&                                       target_state,
                       const AccumulatorState&                                 computed);

template<Color Perspective, bool Forward, IndexType TransformedFeatureDimensions>
void update_accumulator_incremental(
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  const Square                                            ksq,
  AccumulatorState&                                       target_state,
  const AccumulatorState&                                 computed);

template<Color Perspective, IndexType Dimensions>
void update_accumulator_refresh_cache(const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState&                     accumulatorState,
                                      AccumulatorCaches::Cache<Dimensions>& cache);

}

void AccumulatorState::reset(const DirtyPiece& dp) noexcept {
    dirtyPiece = dp;
    accumulatorBig.computed.fill(false);
    accumulatorSmall.computed.fill(false);
}

const AccumulatorState& AccumulatorStack::latest() const noexcept { return accumulators[size - 1]; }

AccumulatorState& AccumulatorStack::mut_latest() noexcept { return accumulators[size - 1]; }

void AccumulatorStack::reset() noexcept {
    accumulators[0].reset({});
    size = 1;
}

void AccumulatorStack::push(const DirtyPiece& dirtyPiece) noexcept {
    assert(size + 1 < accumulators.size());
    accumulators[size].reset(dirtyPiece);
    size++;
}

void AccumulatorStack::pop() noexcept {
    assert(size > 1);
    size--;
}

template<IndexType Dimensions>
void AccumulatorStack::evaluate(const Position&                       pos,
                                const FeatureTransformer<Dimensions>& featureTransformer,
                                AccumulatorCaches::Cache<Dimensions>& cache) noexcept {

    evaluate_side<WHITE>(pos, featureTransformer, cache);
    evaluate_side<BLACK>(pos, featureTransformer, cache);
}

template<Color Perspective, IndexType Dimensions>
void AccumulatorStack::evaluate_side(const Position&                       pos,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     AccumulatorCaches::Cache<Dimensions>& cache) noexcept {

    const auto last_usable_accum = find_last_usable_accumulator<Perspective, Dimensions>();

    if ((accumulators[last_usable_accum].template acc<Dimensions>()).computed[Perspective])
        forward_update_incremental<Perspective>(pos, featureTransformer, last_usable_accum);

    else
    {
        update_accumulator_refresh_cache<Perspective>(featureTransformer, pos, mut_latest(), cache);
        backward_update_incremental<Perspective>(pos, featureTransformer, last_usable_accum);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
template<Color Perspective, IndexType Dimensions>
std::size_t AccumulatorStack::find_last_usable_accumulator() const noexcept {

    for (std::size_t curr_idx = size - 1; curr_idx > 0; curr_idx--)
    {
        if ((accumulators[curr_idx].template acc<Dimensions>()).computed[Perspective])
            return curr_idx;

        if (FeatureSet::requires_refresh(accumulators[curr_idx].dirtyPiece, Perspective))
            return curr_idx;
    }

    return 0;
}

template<Color Perspective, IndexType Dimensions>
void AccumulatorStack::forward_update_incremental(
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  const std::size_t                     begin) noexcept {

    assert(begin < accumulators.size());
    assert((accumulators[begin].acc<Dimensions>()).computed[Perspective]);

    const Square ksq = pos.square<KING>(Perspective);

    for (std::size_t next = begin + 1; next < size; next++)
    {
        if (next + 1 < size)
        {
            DirtyPiece& dp1 = accumulators[next].dirtyPiece;
            DirtyPiece& dp2 = accumulators[next + 1].dirtyPiece;

            if (dp1.to != SQ_NONE && dp1.to == dp2.remove_sq)
            {
                const Square captureSq = dp1.to;
                dp1.to = dp2.remove_sq = SQ_NONE;
                double_inc_update<Perspective>(featureTransformer, ksq, accumulators[next],
                                               accumulators[next + 1], accumulators[next - 1]);
                dp1.to = dp2.remove_sq = captureSq;

                next++;
                continue;
            }
        }
        update_accumulator_incremental<Perspective, true>(
          featureTransformer, ksq, accumulators[next], accumulators[next - 1]);
    }

    assert((latest().acc<Dimensions>()).computed[Perspective]);
}

template<Color Perspective, IndexType Dimensions>
void AccumulatorStack::backward_update_incremental(
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  const std::size_t                     end) noexcept {

    assert(end < accumulators.size());
    assert(end < size);
    assert((latest().acc<Dimensions>()).computed[Perspective]);

    const Square ksq = pos.square<KING>(Perspective);

    for (std::int64_t next = std::int64_t(size) - 2; next >= std::int64_t(end); next--)
        update_accumulator_incremental<Perspective, false>(
          featureTransformer, ksq, accumulators[next], accumulators[next + 1]);

    assert((accumulators[end].acc<Dimensions>()).computed[Perspective]);
}

// Explicit template instantiations
template void AccumulatorStack::evaluate<TransformedFeatureDimensionsBig>(
  const Position&                                            pos,
  const FeatureTransformer<TransformedFeatureDimensionsBig>& featureTransformer,
  AccumulatorCaches::Cache<TransformedFeatureDimensionsBig>& cache) noexcept;
template void AccumulatorStack::evaluate<TransformedFeatureDimensionsSmall>(
  const Position&                                              pos,
  const FeatureTransformer<TransformedFeatureDimensionsSmall>& featureTransformer,
  AccumulatorCaches::Cache<TransformedFeatureDimensionsSmall>& cache) noexcept;


namespace {

template<typename VectorWrapper,
         IndexType Width,
         UpdateOperation... ops,
         typename ElementType,
         typename... Ts,
         std::enable_if_t<is_all_same_v<ElementType, Ts...>, bool> = true>
void fused_row_reduce(const ElementType* in, ElementType* out, const Ts* const... rows) {
    constexpr IndexType size = Width * sizeof(ElementType) / sizeof(typename VectorWrapper::type);

    auto* vecIn  = reinterpret_cast<const typename VectorWrapper::type*>(in);
    auto* vecOut = reinterpret_cast<typename VectorWrapper::type*>(out);

    for (IndexType i = 0; i < size; ++i)
        vecOut[i] = fused<VectorWrapper, ops...>(
          vecIn[i], reinterpret_cast<const typename VectorWrapper::type*>(rows)[i]...);
}

template<Color Perspective, IndexType Dimensions>
struct AccumulatorUpdateContext {
    const FeatureTransformer<Dimensions>& featureTransformer;
    const AccumulatorState&               from;
    AccumulatorState&                     to;

    AccumulatorUpdateContext(const FeatureTransformer<Dimensions>& ft,
                             const AccumulatorState&               accF,
                             AccumulatorState&                     accT) noexcept :
        featureTransformer{ft},
        from{accF},
        to{accT} {}

    template<UpdateOperation... ops,
             typename... Ts,
             std::enable_if_t<is_all_same_v<IndexType, Ts...>, bool> = true>
    void apply(const Ts... indices) {
        auto to_weight_vector = [&](const IndexType index) {
            return &featureTransformer.weights[index * Dimensions];
        };

        auto to_psqt_weight_vector = [&](const IndexType index) {
            return &featureTransformer.psqtWeights[index * PSQTBuckets];
        };

        fused_row_reduce<Vec16Wrapper, Dimensions, ops...>(
          (from.acc<Dimensions>()).accumulation[Perspective],
          (to.acc<Dimensions>()).accumulation[Perspective], to_weight_vector(indices)...);

        fused_row_reduce<Vec32Wrapper, PSQTBuckets, ops...>(
          (from.acc<Dimensions>()).psqtAccumulation[Perspective],
          (to.acc<Dimensions>()).psqtAccumulation[Perspective], to_psqt_weight_vector(indices)...);
    }
};

template<Color Perspective, IndexType Dimensions>
auto make_accumulator_update_context(const FeatureTransformer<Dimensions>& featureTransformer,
                                     const AccumulatorState&               accumulatorFrom,
                                     AccumulatorState&                     accumulatorTo) noexcept {
    return AccumulatorUpdateContext<Perspective, Dimensions>{featureTransformer, accumulatorFrom,
                                                             accumulatorTo};
}

template<Color Perspective, IndexType TransformedFeatureDimensions>
void double_inc_update(const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState&                                       middle_state,
                       AccumulatorState&                                       target_state,
                       const AccumulatorState&                                 computed) {

    assert(computed.acc<TransformedFeatureDimensions>().computed[Perspective]);
    assert(!middle_state.acc<TransformedFeatureDimensions>().computed[Perspective]);
    assert(!target_state.acc<TransformedFeatureDimensions>().computed[Perspective]);

    FeatureSet::IndexList removed, added;
    FeatureSet::append_changed_indices<Perspective>(ksq, middle_state.dirtyPiece, removed, added);
    // you can't capture a piece that was just involved in castling since the rook ends up
    // in a square that the king passed
    assert(added.size() < 2);
    FeatureSet::append_changed_indices<Perspective>(ksq, target_state.dirtyPiece, removed, added);

    assert(added.size() == 1);
    assert(removed.size() == 2 || removed.size() == 3);

    // Workaround compiler warning for uninitialized variables, replicated on
    // profile builds on windows with gcc 14.2.0.
    // TODO remove once unneeded
    sf_assume(added.size() == 1);
    sf_assume(removed.size() == 2 || removed.size() == 3);

    auto updateContext =
      make_accumulator_update_context<Perspective>(featureTransformer, computed, target_state);

    if (removed.size() == 2)
    {
        updateContext.template apply<Add, Sub, Sub>(added[0], removed[0], removed[1]);
    }
    else
    {
        updateContext.template apply<Add, Sub, Sub, Sub>(added[0], removed[0], removed[1],
                                                         removed[2]);
    }

    target_state.acc<TransformedFeatureDimensions>().computed[Perspective] = true;
}

template<Color Perspective, bool Forward, IndexType TransformedFeatureDimensions>
void update_accumulator_incremental(
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  const Square                                            ksq,
  AccumulatorState&                                       target_state,
  const AccumulatorState&                                 computed) {

    assert((computed.acc<TransformedFeatureDimensions>()).computed[Perspective]);
    assert(!(target_state.acc<TransformedFeatureDimensions>()).computed[Perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    // In this case, the maximum size of both feature addition and removal
    // is 2, since we are incrementally updating one move at a time.
    FeatureSet::IndexList removed, added;
    if constexpr (Forward)
        FeatureSet::append_changed_indices<Perspective>(ksq, target_state.dirtyPiece, removed,
                                                        added);
    else
        FeatureSet::append_changed_indices<Perspective>(ksq, computed.dirtyPiece, added, removed);

    assert(added.size() == 1 || added.size() == 2);
    assert(removed.size() == 1 || removed.size() == 2);
    assert((Forward && added.size() <= removed.size())
           || (!Forward && added.size() >= removed.size()));

    // Workaround compiler warning for uninitialized variables, replicated on
    // profile builds on windows with gcc 14.2.0.
    // TODO remove once unneeded
    sf_assume(added.size() == 1 || added.size() == 2);
    sf_assume(removed.size() == 1 || removed.size() == 2);

    auto updateContext =
      make_accumulator_update_context<Perspective>(featureTransformer, computed, target_state);

    if ((Forward && removed.size() == 1) || (!Forward && added.size() == 1))
    {
        assert(added.size() == 1 && removed.size() == 1);
        updateContext.template apply<Add, Sub>(added[0], removed[0]);
    }
    else if (Forward && added.size() == 1)
    {
        assert(removed.size() == 2);
        updateContext.template apply<Add, Sub, Sub>(added[0], removed[0], removed[1]);
    }
    else if (!Forward && removed.size() == 1)
    {
        assert(added.size() == 2);
        updateContext.template apply<Add, Add, Sub>(added[0], added[1], removed[0]);
    }
    else
    {
        assert(added.size() == 2 && removed.size() == 2);
        updateContext.template apply<Add, Add, Sub, Sub>(added[0], added[1], removed[0],
                                                         removed[1]);
    }

    (target_state.acc<TransformedFeatureDimensions>()).computed[Perspective] = true;
}

#ifdef USE_AVX512ICL
alignas(64) constexpr int16_t AllSquares[64] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
    60, 61, 62, 63
};

template <bool Perspective>
void list_changed_indices_avx512(const __m512i old_pieces, const __m512i new_pieces, Square ksq, ValueList<uint16_t, 64> &removed, ValueList<uint16_t, 64>& added) {
    // Index mapping depends on the king's square
    const __m512i orient_table = _mm512_set1_epi16(Features::HalfKAv2_hm::OrientTBL[Perspective][ksq]);
    const __m512i king_buckets = _mm512_set1_epi16(Features::HalfKAv2_hm::KingBuckets[Perspective][ksq]);
    __m512i lookup_table = _mm512_castsi256_si512(
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&Features::HalfKAv2_hm::PieceSquareIndex[Perspective])));
    lookup_table = _mm512_add_epi16(lookup_table, king_buckets);
    auto to_indices = [&] (__m512i squares, __m512i pieces) {
        // Implement the same algorithm as given in HalfKAv2_hm
        const __m512i lookup = _mm512_permutexvar_epi16(pieces, lookup_table);
        return _mm512_add_epi16(_mm512_xor_si512(squares, orient_table), lookup);
    };

    // Get either the lower or higher half of a 512-bit register
    auto unpack_half = [&] (__m512i pieces, int half) {
        assert(half == 0 || half == 1);
        return _mm512_cvtepu8_epi16(half == 0 ? _mm512_castsi512_si256(pieces) : _mm512_extracti64x4_epi64(pieces, 1));
    };

    // All pieces that were changed
    const __mmask64 changed = _mm512_cmpneq_epi8_mask(new_pieces, old_pieces);
    // Pieces, i.e. nonzero entries, that were added or removed
    const __mmask64 added_mask = _mm512_mask_test_epi8_mask(changed, new_pieces, new_pieces);
    const __mmask64 removed_mask = _mm512_mask_test_epi8_mask(changed, old_pieces, old_pieces);

    removed.set_size(popcount(removed_mask));
    added.set_size(popcount(added_mask));

    // Split into two halves (first 32 squares and last 32 squares)
    for (int half = 0; half < 2; half++) {
        const auto new_half = unpack_half(new_pieces, half);
        const auto old_half = unpack_half(old_pieces, half);
        const __m512i squares = _mm512_load_si512(AllSquares + half * 32);

        const auto new_indices = to_indices(squares, new_half);
        const auto old_indices = to_indices(squares, old_half);

        // For the second halves, get # of elements among first 32 only
        auto i = half * popcount(static_cast<std::uint32_t>(removed_mask));
        auto j = half * popcount(static_cast<std::uint32_t>(added_mask));

        // Don't use compressstoreu because it's slow on Zen 4
        auto removed_indices = _mm512_maskz_compress_epi16(removed_mask >> 32 * half, old_indices);
        _mm512_storeu_si512(&removed[i], removed_indices);
        auto added_indices = _mm512_maskz_compress_epi16(added_mask >> 32 * half, new_indices);
        _mm512_storeu_si512(&added[j], added_indices);
    }
}
#endif

template<Color Perspective, IndexType Dimensions>
void update_accumulator_refresh_cache(const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState&                     accumulatorState,
                                      AccumulatorCaches::Cache<Dimensions>& cache) {

    using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, Dimensions, PSQTBuckets>;

    const Square          ksq   = pos.square<KING>(Perspective);
    auto&                 entry = cache[ksq][Perspective];
    ValueList<uint16_t, 64> removed, added;

#ifdef USE_AVX512ICL
    auto board = pos.board_array();
    auto new_pieces = _mm512_loadu_si512(&board);
    auto old_pieces = _mm512_loadu_si512(entry.board);
    list_changed_indices_avx512<Perspective>(old_pieces, new_pieces, ksq, removed, added);
#else
    for (Color c : {WHITE, BLACK}) {
        for (PieceType pt = PAWN; pt <= KING; ++pt)
        {
            const Piece    piece    = make_piece(c, pt);
            const Bitboard oldBB    = entry.byColorBB[c] & entry.byTypeBB[pt];
            const Bitboard newBB    = pos.pieces(c, pt);
            Bitboard       toRemove = oldBB & ~newBB;
            Bitboard       toAdd    = newBB & ~oldBB;

            while (toRemove)
            {
                Square sq = pop_lsb(toRemove);
                removed.push_back(FeatureSet::make_index<Perspective>(sq, piece, ksq));
            }
            while (toAdd)
            {
                Square sq = pop_lsb(toAdd);
                added.push_back(FeatureSet::make_index<Perspective>(sq, piece, ksq));
            }
        }
    }
#endif

    auto& accumulator                 = accumulatorState.acc<Dimensions>();
    accumulator.computed[Perspective] = true;

#ifdef VECTOR
    vec_t      acc[Tiling::NumRegs];
    psqt_vec_t psqt[Tiling::NumPsqtRegs];

    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile =
          reinterpret_cast<vec_t*>(&accumulator.accumulation[Perspective][j * Tiling::TileHeight]);
        auto* entryTile = reinterpret_cast<vec_t*>(&entry.accumulation[j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            acc[k] = entryTile[k];

        IndexType i = 0;
        for (; i < std::min(removed.size(), added.size()); ++i)
        {
            IndexType       indexR  = removed[i];
            const IndexType offsetR = Dimensions * indexR + j * Tiling::TileHeight;
            auto* columnR = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR]);
            IndexType       indexA  = added[i];
            const IndexType offsetA = Dimensions * indexA + j * Tiling::TileHeight;
            auto* columnA = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = fused<Vec16Wrapper, Add, Sub>(acc[k], columnA[k], columnR[k]);
        }
        for (; i < removed.size(); ++i)
        {
            IndexType       index  = removed[i];
            const IndexType offset = Dimensions * index + j * Tiling::TileHeight;
            auto* column = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offset]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);
        }
        for (; i < added.size(); ++i)
        {
            IndexType       index  = added[i];
            const IndexType offset = Dimensions * index + j * Tiling::TileHeight;
            auto* column = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offset]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&entryTile[k], acc[k]);
        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&accTile[k], acc[k]);
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
    {
        auto* accTilePsqt = reinterpret_cast<psqt_vec_t*>(
          &accumulator.psqtAccumulation[Perspective][j * Tiling::PsqtTileHeight]);
        auto* entryTilePsqt =
          reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[j * Tiling::PsqtTileHeight]);

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            psqt[k] = entryTilePsqt[k];

        for (IndexType i = 0; i < removed.size(); ++i)
        {
            IndexType       index  = removed[i];
            const IndexType offset = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
            auto*           columnPsqt =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offset]);

            for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }
        for (IndexType i = 0; i < added.size(); ++i)
        {
            IndexType       index  = added[i];
            const IndexType offset = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
            auto*           columnPsqt =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offset]);

            for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&entryTilePsqt[k], psqt[k]);
        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&accTilePsqt[k], psqt[k]);
    }

#else

    for (const auto index : removed)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            entry.accumulation[j] -= featureTransformer.weights[offset + j];

        for (std::size_t k = 0; k < PSQTBuckets; ++k)
            entry.psqtAccumulation[k] -= featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }
    for (const auto index : added)
    {
        const IndexType offset = Dimensions * index;
        for (IndexType j = 0; j < Dimensions; ++j)
            entry.accumulation[j] += featureTransformer.weights[offset + j];

        for (std::size_t k = 0; k < PSQTBuckets; ++k)
            entry.psqtAccumulation[k] += featureTransformer.psqtWeights[index * PSQTBuckets + k];
    }

    // The accumulator of the refresh entry has been updated.
    // Now copy its content to the actual accumulator we were refreshing.

    std::memcpy(accumulator.accumulation[Perspective], entry.accumulation,
                sizeof(BiasType) * Dimensions);

    std::memcpy(accumulator.psqtAccumulation[Perspective], entry.psqtAccumulation,
                sizeof(int32_t) * PSQTBuckets);
#endif

#ifdef USE_AVX512ICL
    static_assert(sizeof(board) == sizeof(entry.board));
    std::copy_n(board.begin(), SQUARE_NB, entry.board);
#else
    for (Color c : {WHITE, BLACK})
        entry.byColorBB[c] = pos.pieces(c);

    for (PieceType pt = PAWN; pt <= KING; ++pt)
        entry.byTypeBB[pt] = pos.pieces(pt);
#endif
}

}

}
