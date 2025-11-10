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

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <type_traits>

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_feature_transformer.h"  // IWYU pragma: keep
#include "simd.h"

namespace Stockfish::Eval::NNUE {

using namespace SIMD;

namespace {

template<Color Perspective, IndexType TransformedFeatureDimensions>
void double_inc_update(const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState<PSQFeatureSet>&                        middle_state,
                       AccumulatorState<PSQFeatureSet>&                        target_state,
                       const AccumulatorState<PSQFeatureSet>&                  computed);

template<Color Perspective, IndexType TransformedFeatureDimensions>
bool double_inc_update(const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState<ThreatFeatureSet>&                     middle_state,
                       AccumulatorState<ThreatFeatureSet>&                     target_state,
                       const AccumulatorState<ThreatFeatureSet>&               computed,
                       const DirtyPiece&                                       dp2);

template<Color Perspective,
         bool  Forward,
         typename FeatureSet,
         IndexType TransformedFeatureDimensions>
void update_accumulator_incremental(
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  const Square                                            ksq,
  AccumulatorState<FeatureSet>&                           target_state,
  const AccumulatorState<FeatureSet>&                     computed);

template<Color Perspective, IndexType Dimensions>
void update_accumulator_refresh_cache(const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState<PSQFeatureSet>&      accumulatorState,
                                      AccumulatorCaches::Cache<Dimensions>& cache);

template<Color Perspective, IndexType Dimensions>
void update_threats_accumulator_full(const FeatureTransformer<Dimensions>& featureTransformer,
                                     const Position&                       pos,
                                     AccumulatorState<ThreatFeatureSet>&   accumulatorState);
}

template<typename T>
const AccumulatorState<T>& AccumulatorStack::latest() const noexcept {
    return accumulators<T>()[size - 1];
}

// Explicit template instantiations
template const AccumulatorState<PSQFeatureSet>&    AccumulatorStack::latest() const noexcept;
template const AccumulatorState<ThreatFeatureSet>& AccumulatorStack::latest() const noexcept;

template<typename T>
AccumulatorState<T>& AccumulatorStack::mut_latest() noexcept {
    return mut_accumulators<T>()[size - 1];
}

template<typename T>
const std::array<AccumulatorState<T>, AccumulatorStack::MaxSize>&
AccumulatorStack::accumulators() const noexcept {
    static_assert(std::is_same_v<T, PSQFeatureSet> || std::is_same_v<T, ThreatFeatureSet>,
                  "Invalid Feature Set Type");

    if constexpr (std::is_same_v<T, PSQFeatureSet>)
        return psq_accumulators;

    if constexpr (std::is_same_v<T, ThreatFeatureSet>)
        return threat_accumulators;
}

template<typename T>
std::array<AccumulatorState<T>, AccumulatorStack::MaxSize>&
AccumulatorStack::mut_accumulators() noexcept {
    static_assert(std::is_same_v<T, PSQFeatureSet> || std::is_same_v<T, ThreatFeatureSet>,
                  "Invalid Feature Set Type");

    if constexpr (std::is_same_v<T, PSQFeatureSet>)
        return psq_accumulators;

    if constexpr (std::is_same_v<T, ThreatFeatureSet>)
        return threat_accumulators;
}

void AccumulatorStack::reset() noexcept {
    psq_accumulators[0].reset({});
    threat_accumulators[0].reset({});
    size = 1;
}

void AccumulatorStack::push(const DirtyBoardData& dirtyBoardData) noexcept {
    assert(size < MaxSize);
    psq_accumulators[size].reset(dirtyBoardData.dp);
    threat_accumulators[size].reset(dirtyBoardData.dts);
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
    constexpr bool use_threats = (Dimensions == TransformedFeatureDimensionsBig);
    evaluate_side<WHITE, PSQFeatureSet>(pos, featureTransformer, cache);
    if (use_threats)
    {
        evaluate_side<WHITE, ThreatFeatureSet>(pos, featureTransformer, cache);
    }
    evaluate_side<BLACK, PSQFeatureSet>(pos, featureTransformer, cache);
    if (use_threats)
    {
        evaluate_side<BLACK, ThreatFeatureSet>(pos, featureTransformer, cache);
    }
}

template<Color Perspective, typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::evaluate_side(const Position&                       pos,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     AccumulatorCaches::Cache<Dimensions>& cache) noexcept {

    const auto last_usable_accum =
      find_last_usable_accumulator<Perspective, FeatureSet, Dimensions>();

    if ((accumulators<FeatureSet>()[last_usable_accum].template acc<Dimensions>())
          .computed[Perspective])
        forward_update_incremental<Perspective, FeatureSet>(pos, featureTransformer,
                                                            last_usable_accum);

    else
    {
        if constexpr (std::is_same_v<FeatureSet, PSQFeatureSet>)
            update_accumulator_refresh_cache<Perspective>(featureTransformer, pos,
                                                          mut_latest<PSQFeatureSet>(), cache);
        else
            update_threats_accumulator_full<Perspective>(featureTransformer, pos,
                                                         mut_latest<ThreatFeatureSet>());

        backward_update_incremental<Perspective, FeatureSet>(pos, featureTransformer,
                                                             last_usable_accum);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
template<Color Perspective, typename FeatureSet, IndexType Dimensions>
std::size_t AccumulatorStack::find_last_usable_accumulator() const noexcept {

    for (std::size_t curr_idx = size - 1; curr_idx > 0; curr_idx--)
    {
        if ((accumulators<FeatureSet>()[curr_idx].template acc<Dimensions>()).computed[Perspective])
            return curr_idx;

        if (FeatureSet::requires_refresh(accumulators<FeatureSet>()[curr_idx].diff, Perspective))
            return curr_idx;
    }

    return 0;
}

template<Color Perspective, typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::forward_update_incremental(
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  const std::size_t                     begin) noexcept {

    assert(begin < accumulators<FeatureSet>().size());
    assert((accumulators<FeatureSet>()[begin].template acc<Dimensions>()).computed[Perspective]);

    const Square ksq = pos.square<KING>(Perspective);

    for (std::size_t next = begin + 1; next < size; next++)
    {
        if (next + 1 < size)
        {
            DirtyPiece& dp1 = psq_accumulators[next].diff;
            DirtyPiece& dp2 = psq_accumulators[next + 1].diff;

            if (std::is_same_v<FeatureSet, ThreatFeatureSet> && dp2.remove_sq != SQ_NONE
                && ((threat_accumulators[next].diff.threateningSqs & square_bb(dp2.remove_sq))
                    || (threat_accumulators[next].diff.threatenedSqs & square_bb(dp2.remove_sq))))
            {
                double_inc_update<Perspective>(featureTransformer, ksq, threat_accumulators[next],
                                               threat_accumulators[next + 1],
                                               threat_accumulators[next - 1], dp2);
                next++;
                continue;
            }

            if (std::is_same_v<FeatureSet, PSQFeatureSet> && dp1.to != SQ_NONE
                && dp1.to == dp2.remove_sq)
            {
                const Square captureSq = dp1.to;
                dp1.to = dp2.remove_sq = SQ_NONE;
                double_inc_update<Perspective>(featureTransformer, ksq, psq_accumulators[next],
                                               psq_accumulators[next + 1],
                                               psq_accumulators[next - 1]);
                dp1.to = dp2.remove_sq = captureSq;

                next++;
                continue;
            }
        }

        update_accumulator_incremental<Perspective, true>(featureTransformer, ksq,
                                                          mut_accumulators<FeatureSet>()[next],
                                                          accumulators<FeatureSet>()[next - 1]);
    }

    assert((latest<PSQFeatureSet>().acc<Dimensions>()).computed[Perspective]);
}

template<Color Perspective, typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::backward_update_incremental(
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  const std::size_t                     end) noexcept {

    assert(end < accumulators<FeatureSet>().size());
    assert(end < size);
    assert((latest<FeatureSet>().template acc<Dimensions>()).computed[Perspective]);

    const Square ksq = pos.square<KING>(Perspective);

    for (std::int64_t next = std::int64_t(size) - 2; next >= std::int64_t(end); next--)
        update_accumulator_incremental<Perspective, false>(featureTransformer, ksq,
                                                           mut_accumulators<FeatureSet>()[next],
                                                           accumulators<FeatureSet>()[next + 1]);

    assert((accumulators<FeatureSet>()[end].template acc<Dimensions>()).computed[Perspective]);
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

template<typename FeatureSet, Color Perspective, IndexType Dimensions>
struct AccumulatorUpdateContext {
    const FeatureTransformer<Dimensions>& featureTransformer;
    const AccumulatorState<FeatureSet>&   from;
    AccumulatorState<FeatureSet>&         to;

    AccumulatorUpdateContext(const FeatureTransformer<Dimensions>& ft,
                             const AccumulatorState<FeatureSet>&   accF,
                             AccumulatorState<FeatureSet>&         accT) noexcept :
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
          (from.template acc<Dimensions>()).accumulation[Perspective],
          (to.template acc<Dimensions>()).accumulation[Perspective], to_weight_vector(indices)...);

        fused_row_reduce<Vec32Wrapper, PSQTBuckets, ops...>(
          (from.template acc<Dimensions>()).psqtAccumulation[Perspective],
          (to.template acc<Dimensions>()).psqtAccumulation[Perspective],
          to_psqt_weight_vector(indices)...);
    }

    void apply(typename FeatureSet::IndexList added, typename FeatureSet::IndexList removed) {
        const auto fromAcc = from.template acc<Dimensions>().accumulation[Perspective];
        const auto toAcc   = to.template acc<Dimensions>().accumulation[Perspective];

        const auto fromPsqtAcc = from.template acc<Dimensions>().psqtAccumulation[Perspective];
        const auto toPsqtAcc   = to.template acc<Dimensions>().psqtAccumulation[Perspective];

#ifdef VECTOR
        using Tiling = SIMDTiling<Dimensions, Dimensions, PSQTBuckets>;
        vec_t      acc[Tiling::NumRegs];
        psqt_vec_t psqt[Tiling::NumPsqtRegs];

        for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
        {
            auto* fromTile = reinterpret_cast<const vec_t*>(&fromAcc[j * Tiling::TileHeight]);
            auto* toTile   = reinterpret_cast<vec_t*>(&toAcc[j * Tiling::TileHeight]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = fromTile[k];

            for (IndexType i = 0; i < removed.size(); ++i)
            {
                IndexType       index  = removed[i];
                const IndexType offset = Dimensions * index + j * Tiling::TileHeight;
                auto*           column =
                  reinterpret_cast<const vec_i8_t*>(&featureTransformer.threatWeights[offset]);

#ifdef USE_NEON
                for (IndexType k = 0; k < Tiling::NumRegs; k += 2) {
                    acc[k] = vec_sub_16(acc[k], vmovl_s8(vget_low_s8(column[k / 2])));
                    acc[k + 1] = vec_sub_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
                }
#else
                for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                    acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));
#endif
            }

            for (IndexType i = 0; i < added.size(); ++i)
            {
                IndexType       index  = added[i];
                const IndexType offset = Dimensions * index + j * Tiling::TileHeight;
                auto*           column =
                  reinterpret_cast<const vec_i8_t*>(&featureTransformer.threatWeights[offset]);

#ifdef USE_NEON
                for (IndexType k = 0; k < Tiling::NumRegs; k += 2) {
                    acc[k] = vec_add_16(acc[k], vmovl_s8(vget_low_s8(column[k / 2])));
                    acc[k + 1] = vec_add_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
                }
#else
                for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                    acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
#endif
            }

            for (IndexType k = 0; k < Tiling::NumRegs; k++)
                vec_store(&toTile[k], acc[k]);
        }

        for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
        {
            auto* fromTilePsqt =
              reinterpret_cast<const psqt_vec_t*>(&fromPsqtAcc[j * Tiling::PsqtTileHeight]);
            auto* toTilePsqt =
              reinterpret_cast<psqt_vec_t*>(&toPsqtAcc[j * Tiling::PsqtTileHeight]);

            for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = fromTilePsqt[k];

            for (IndexType i = 0; i < removed.size(); ++i)
            {
                IndexType       index      = removed[i];
                const IndexType offset     = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
                auto*           columnPsqt = reinterpret_cast<const psqt_vec_t*>(
                  &featureTransformer.threatPsqtWeights[offset]);

                for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                    psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (IndexType i = 0; i < added.size(); ++i)
            {
                IndexType       index      = added[i];
                const IndexType offset     = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
                auto*           columnPsqt = reinterpret_cast<const psqt_vec_t*>(
                  &featureTransformer.threatPsqtWeights[offset]);

                for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                    psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
                vec_store_psqt(&toTilePsqt[k], psqt[k]);
        }

#else

        for (const auto index : removed)
        {
            const IndexType offset = Dimensions * index;

            for (IndexType j = 0; j < Dimensions; ++j)
                toAcc[j] = fromAcc[j] - featureTransformer.threatWeights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                toPsqtAcc[k] =
                  fromPsqtAcc[k] - featureTransformer.threatPsqtWeights[index * PSQTBuckets + k];
        }

        for (const auto index : added)
        {
            const IndexType offset = Dimensions * index;

            for (IndexType j = 0; j < Dimensions; ++j)
                toAcc[j] = fromAcc[j] + featureTransformer.threatWeights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                toPsqtAcc[k] =
                  fromPsqtAcc[k] + featureTransformer.threatPsqtWeights[index * PSQTBuckets + k];
        }

#endif
    }
};

template<Color Perspective, typename FeatureSet, IndexType Dimensions>
auto make_accumulator_update_context(const FeatureTransformer<Dimensions>& featureTransformer,
                                     const AccumulatorState<FeatureSet>&   accumulatorFrom,
                                     AccumulatorState<FeatureSet>&         accumulatorTo) noexcept {
    return AccumulatorUpdateContext<FeatureSet, Perspective, Dimensions>{
      featureTransformer, accumulatorFrom, accumulatorTo};
}

template<Color Perspective, IndexType TransformedFeatureDimensions>
void double_inc_update(const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState<PSQFeatureSet>&                        middle_state,
                       AccumulatorState<PSQFeatureSet>&                        target_state,
                       const AccumulatorState<PSQFeatureSet>&                  computed) {

    assert(computed.acc<TransformedFeatureDimensions>().computed[Perspective]);
    assert(!middle_state.acc<TransformedFeatureDimensions>().computed[Perspective]);
    assert(!target_state.acc<TransformedFeatureDimensions>().computed[Perspective]);

    PSQFeatureSet::IndexList removed, added;
    PSQFeatureSet::append_changed_indices<Perspective>(ksq, middle_state.diff, removed, added);
    // you can't capture a piece that was just involved in castling since the rook ends up
    // in a square that the king passed
    assert(added.size() < 2);
    PSQFeatureSet::append_changed_indices<Perspective>(ksq, target_state.diff, removed, added);

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

template<Color Perspective, IndexType TransformedFeatureDimensions>
bool double_inc_update(const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState<ThreatFeatureSet>&                     middle_state,
                       AccumulatorState<ThreatFeatureSet>&                     target_state,
                       const AccumulatorState<ThreatFeatureSet>&               computed,
                       const DirtyPiece&                                       dp2) {

    assert(computed.acc<TransformedFeatureDimensions>().computed[Perspective]);
    assert(!middle_state.acc<TransformedFeatureDimensions>().computed[Perspective]);
    assert(!target_state.acc<TransformedFeatureDimensions>().computed[Perspective]);

    ThreatFeatureSet::FusedUpdateData fusedData;

    fusedData.dp2removed = dp2.remove_sq;

    ThreatFeatureSet::IndexList removed, added;
    ThreatFeatureSet::append_changed_indices<Perspective>(ksq, middle_state.diff, removed, added,
                                                          &fusedData, true);
    ThreatFeatureSet::append_changed_indices<Perspective>(ksq, target_state.diff, removed, added,
                                                          &fusedData, false);

    auto updateContext =
      make_accumulator_update_context<Perspective>(featureTransformer, computed, target_state);

    updateContext.apply(added, removed);

    target_state.acc<TransformedFeatureDimensions>().computed[Perspective] = true;

    return true;
}

template<Color Perspective,
         bool  Forward,
         typename FeatureSet,
         IndexType TransformedFeatureDimensions>
void update_accumulator_incremental(
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  const Square                                            ksq,
  AccumulatorState<FeatureSet>&                           target_state,
  const AccumulatorState<FeatureSet>&                     computed) {

    assert((computed.template acc<TransformedFeatureDimensions>()).computed[Perspective]);
    assert(!(target_state.template acc<TransformedFeatureDimensions>()).computed[Perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    // In this case, the maximum size of both feature addition and removal
    // is 2, since we are incrementally updating one move at a time.
    typename FeatureSet::IndexList removed, added;
    if constexpr (Forward)
        FeatureSet::template append_changed_indices<Perspective>(ksq, target_state.diff, removed,
                                                                 added);
    else
        FeatureSet::template append_changed_indices<Perspective>(ksq, computed.diff, added,
                                                                 removed);

    auto updateContext =
      make_accumulator_update_context<Perspective>(featureTransformer, computed, target_state);

    if constexpr (std::is_same_v<FeatureSet, ThreatFeatureSet>)
        updateContext.apply(added, removed);
    else
    {
        assert(added.size() == 1 || added.size() == 2);
        assert(removed.size() == 1 || removed.size() == 2);
        assert((Forward && added.size() <= removed.size())
               || (!Forward && added.size() >= removed.size()));

        // Workaround compiler warning for uninitialized variables, replicated
        // on profile builds on windows with gcc 14.2.0.
        // TODO remove once unneeded
        sf_assume(added.size() == 1 || added.size() == 2);
        sf_assume(removed.size() == 1 || removed.size() == 2);

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
    }

    (target_state.template acc<TransformedFeatureDimensions>()).computed[Perspective] = true;
}

Bitboard get_changed_pieces(const Piece old[SQUARE_NB], const Piece new_[SQUARE_NB]) {
#if defined(USE_AVX512) || defined(USE_AVX2)
    static_assert(sizeof(Piece) == 1);
    Bitboard same_bb = 0;
    for (int i = 0; i < 64; i += 32)
    {
        const __m256i       old_v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(old + i));
        const __m256i       new_v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(new_ + i));
        const __m256i       cmp_equal  = _mm256_cmpeq_epi8(old_v, new_v);
        const std::uint32_t equal_mask = _mm256_movemask_epi8(cmp_equal);
        same_bb |= static_cast<Bitboard>(equal_mask) << i;
    }
    return ~same_bb;
#else
    Bitboard changed = 0;
    for (Square sq = SQUARE_ZERO; sq < SQUARE_NB; ++sq)
    {
        changed |= static_cast<Bitboard>(old[sq] != new_[sq]) << sq;
    }
    return changed;
#endif
}

template<Color Perspective, IndexType Dimensions>
void update_accumulator_refresh_cache(const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState<PSQFeatureSet>&      accumulatorState,
                                      AccumulatorCaches::Cache<Dimensions>& cache) {

    using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, Dimensions, PSQTBuckets>;

    const Square             ksq   = pos.square<KING>(Perspective);
    auto&                    entry = cache[ksq][Perspective];
    PSQFeatureSet::IndexList removed, added;

    const Bitboard changed_bb = get_changed_pieces(entry.pieces, pos.piece_array().data());
    Bitboard       removed_bb = changed_bb & entry.pieceBB;
    Bitboard       added_bb   = changed_bb & pos.pieces();

    while (removed_bb)
    {
        Square sq = pop_lsb(removed_bb);
        removed.push_back(PSQFeatureSet::make_index<Perspective>(sq, entry.pieces[sq], ksq));
    }
    while (added_bb)
    {
        Square sq = pop_lsb(added_bb);
        added.push_back(PSQFeatureSet::make_index<Perspective>(sq, pos.piece_on(sq), ksq));
    }

    entry.pieceBB = pos.pieces();
    std::copy_n(pos.piece_array().begin(), SQUARE_NB, entry.pieces);

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
}

template<Color Perspective, IndexType Dimensions>
void update_threats_accumulator_full(const FeatureTransformer<Dimensions>& featureTransformer,
                                     const Position&                       pos,
                                     AccumulatorState<ThreatFeatureSet>&   accumulatorState) {
    using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, Dimensions, PSQTBuckets>;

    ThreatFeatureSet::IndexList active;
    ThreatFeatureSet::append_active_indices<Perspective>(pos, active);

    auto& accumulator                 = accumulatorState.acc<Dimensions>();
    accumulator.computed[Perspective] = true;

#ifdef VECTOR
    vec_t      acc[Tiling::NumRegs];
    psqt_vec_t psqt[Tiling::NumPsqtRegs];

    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile =
          reinterpret_cast<vec_t*>(&accumulator.accumulation[Perspective][j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            acc[k] = vec_zero();

        IndexType i = 0;

        for (; i < active.size(); ++i)
        {
            IndexType       index  = active[i];
            const IndexType offset = Dimensions * index + j * Tiling::TileHeight;
            auto*           column =
              reinterpret_cast<const vec_i8_t*>(&featureTransformer.threatWeights[offset]);

#ifdef USE_NEON
                for (IndexType k = 0; k < Tiling::NumRegs; k += 2) {
                    acc[k] = vec_add_16(acc[k], vmovl_s8(vget_low_s8(column[k / 2])));
                    acc[k + 1] = vec_add_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
                }
#else
            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
#endif
        }

        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&accTile[k], acc[k]);
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
    {
        auto* accTilePsqt = reinterpret_cast<psqt_vec_t*>(
          &accumulator.psqtAccumulation[Perspective][j * Tiling::PsqtTileHeight]);

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            psqt[k] = vec_zero_psqt();

        for (IndexType i = 0; i < active.size(); ++i)
        {
            IndexType       index  = active[i];
            const IndexType offset = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
            auto*           columnPsqt =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.threatPsqtWeights[offset]);

            for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&accTilePsqt[k], psqt[k]);
    }

#else

    for (IndexType j = 0; j < Dimensions; ++j)
        accumulator.accumulation[Perspective][j] = 0;

    for (std::size_t k = 0; k < PSQTBuckets; ++k)
        accumulator.psqtAccumulation[Perspective][k] = 0;

    for (const auto index : active)
    {
        const IndexType offset = Dimensions * index;

        for (IndexType j = 0; j < Dimensions; ++j)
            accumulator.accumulation[Perspective][j] +=
              featureTransformer.threatWeights[offset + j];

        for (std::size_t k = 0; k < PSQTBuckets; ++k)
            accumulator.psqtAccumulation[Perspective][k] +=
              featureTransformer.threatPsqtWeights[index * PSQTBuckets + k];
    }

#endif
}

}

}
