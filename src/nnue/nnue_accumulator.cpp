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
#include <type_traits>

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_feature_transformer.h"

namespace Stockfish::Eval::NNUE {

#if defined(__GNUC__) && !defined(__clang__)
    #if __GNUC__ >= 13
        #define sf_assume(cond) __attribute__((assume(cond)))
    #else
        #define sf_assume(cond) \
            do \
            { \
                if (!(cond)) \
                    __builtin_unreachable(); \
            } while (0)
    #endif
#else
    // do nothing for other compilers
    #define sf_assume(cond)
#endif

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

template<Color Perspective, IndexType Dimensions>
void update_accumulator_refresh_cache(const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState&                     accumulatorState,
                                      AccumulatorCaches::Cache<Dimensions>& cache) {

    using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, Dimensions>;

    const Square          ksq   = pos.square<KING>(Perspective);
    auto&                 entry = cache[ksq][Perspective];
    FeatureSet::IndexList removed, added;

    for (Color c : {WHITE, BLACK})
    {
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

    for (Color c : {WHITE, BLACK})
        entry.byColorBB[c] = pos.pieces(c);

    for (PieceType pt = PAWN; pt <= KING; ++pt)
        entry.byTypeBB[pt] = pos.pieces(pt);
}

}

}
