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

#include "nnue_accumulator.h"

#include <cassert>
#include <cstdint>
#include <new>
#include <type_traits>

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "features/half_ka_v2_hm.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_feature_transformer.h"  // IWYU pragma: keep
#include "simd.h"

namespace Stockfish::Eval::NNUE {

using namespace SIMD;

namespace {

template<IndexType TransformedFeatureDimensions>
void double_inc_update(Color                                                   perspective,
                       const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState<PSQFeatureSet>&                        middle_state,
                       AccumulatorState<PSQFeatureSet>&                        target_state,
                       const AccumulatorState<PSQFeatureSet>&                  computed);

template<IndexType TransformedFeatureDimensions>
void double_inc_update(Color                                                   perspective,
                       const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState<ThreatFeatureSet>&                     middle_state,
                       AccumulatorState<ThreatFeatureSet>&                     target_state,
                       const AccumulatorState<ThreatFeatureSet>&               computed,
                       const DirtyPiece&                                       dp2);

template<bool Forward, typename FeatureSet, IndexType TransformedFeatureDimensions>
void update_accumulator_incremental(
  Color                                                   perspective,
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  const Square                                            ksq,
  AccumulatorState<FeatureSet>&                           target_state,
  const AccumulatorState<FeatureSet>&                     computed);

template<IndexType Dimensions>
void update_accumulator_refresh_cache(Color                                 perspective,
                                      const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState<PSQFeatureSet>&      accumulatorState,
                                      AccumulatorCaches::Cache<Dimensions>& cache);

template<IndexType Dimensions>
void update_threats_accumulator_full(Color                                 perspective,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
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

std::pair<DirtyPiece&, DirtyThreats&> AccumulatorStack::push() noexcept {
    assert(size < MaxSize);
    auto& dp  = psq_accumulators[size].reset();
    auto& dts = threat_accumulators[size].reset();
    new (&dts) DirtyThreats;
    size++;
    return {dp, dts};
}

void AccumulatorStack::pop() noexcept {
    assert(size > 1);
    size--;
}

template<IndexType Dimensions>
void AccumulatorStack::evaluate(const Position&                       pos,
                                const FeatureTransformer<Dimensions>& featureTransformer,
                                AccumulatorCaches::Cache<Dimensions>& cache) noexcept {
    constexpr bool UseThreats = (Dimensions == TransformedFeatureDimensionsBig);

    evaluate_side<PSQFeatureSet>(WHITE, pos, featureTransformer, cache);

    if (UseThreats)
        evaluate_side<ThreatFeatureSet>(WHITE, pos, featureTransformer, cache);

    evaluate_side<PSQFeatureSet>(BLACK, pos, featureTransformer, cache);

    if (UseThreats)
        evaluate_side<ThreatFeatureSet>(BLACK, pos, featureTransformer, cache);
}

template<typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::evaluate_side(Color                                 perspective,
                                     const Position&                       pos,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     AccumulatorCaches::Cache<Dimensions>& cache) noexcept {

    const auto last_usable_accum =
      find_last_usable_accumulator<FeatureSet, Dimensions>(perspective);

    if ((accumulators<FeatureSet>()[last_usable_accum].template acc<Dimensions>())
          .computed[perspective])
        forward_update_incremental<FeatureSet>(perspective, pos, featureTransformer,
                                               last_usable_accum);

    else
    {
        if constexpr (std::is_same_v<FeatureSet, PSQFeatureSet>)
            update_accumulator_refresh_cache(perspective, featureTransformer, pos,
                                             mut_latest<PSQFeatureSet>(), cache);
        else
            update_threats_accumulator_full(perspective, featureTransformer, pos,
                                            mut_latest<ThreatFeatureSet>());

        backward_update_incremental<FeatureSet>(perspective, pos, featureTransformer,
                                                last_usable_accum);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
template<typename FeatureSet, IndexType Dimensions>
std::size_t AccumulatorStack::find_last_usable_accumulator(Color perspective) const noexcept {

    for (std::size_t curr_idx = size - 1; curr_idx > 0; curr_idx--)
    {
        if ((accumulators<FeatureSet>()[curr_idx].template acc<Dimensions>()).computed[perspective])
            return curr_idx;

        if (FeatureSet::requires_refresh(accumulators<FeatureSet>()[curr_idx].diff, perspective))
            return curr_idx;
    }

    return 0;
}

template<typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::forward_update_incremental(
  Color                                 perspective,
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  const std::size_t                     begin) noexcept {

    assert(begin < accumulators<FeatureSet>().size());
    assert((accumulators<FeatureSet>()[begin].template acc<Dimensions>()).computed[perspective]);

    const Square ksq = pos.square<KING>(perspective);

    for (std::size_t next = begin + 1; next < size; next++)
    {
        if (next + 1 < size)
        {
            DirtyPiece& dp1 = mut_accumulators<PSQFeatureSet>()[next].diff;
            DirtyPiece& dp2 = mut_accumulators<PSQFeatureSet>()[next + 1].diff;

            auto& accumulators = mut_accumulators<FeatureSet>();

            if constexpr (std::is_same_v<FeatureSet, ThreatFeatureSet>)
            {
                if (dp2.remove_sq != SQ_NONE
                    && (accumulators[next].diff.threateningSqs & square_bb(dp2.remove_sq)))
                {
                    double_inc_update(perspective, featureTransformer, ksq, accumulators[next],
                                      accumulators[next + 1], accumulators[next - 1], dp2);
                    next++;
                    continue;
                }
            }

            if constexpr (std::is_same_v<FeatureSet, PSQFeatureSet>)
            {
                if (dp1.to != SQ_NONE && dp1.to == dp2.remove_sq)
                {
                    const Square captureSq = dp1.to;
                    dp1.to = dp2.remove_sq = SQ_NONE;
                    double_inc_update(perspective, featureTransformer, ksq, accumulators[next],
                                      accumulators[next + 1], accumulators[next - 1]);
                    dp1.to = dp2.remove_sq = captureSq;
                    next++;
                    continue;
                }
            }
        }

        update_accumulator_incremental<true>(perspective, featureTransformer, ksq,
                                             mut_accumulators<FeatureSet>()[next],
                                             accumulators<FeatureSet>()[next - 1]);
    }

    assert((latest<PSQFeatureSet>().acc<Dimensions>()).computed[perspective]);
}

template<typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::backward_update_incremental(
  Color perspective,

  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  const std::size_t                     end) noexcept {

    assert(end < accumulators<FeatureSet>().size());
    assert(end < size);
    assert((latest<FeatureSet>().template acc<Dimensions>()).computed[perspective]);

    const Square ksq = pos.square<KING>(perspective);

    for (std::int64_t next = std::int64_t(size) - 2; next >= std::int64_t(end); next--)
        update_accumulator_incremental<false>(perspective, featureTransformer, ksq,
                                              mut_accumulators<FeatureSet>()[next],
                                              accumulators<FeatureSet>()[next + 1]);

    assert((accumulators<FeatureSet>()[end].template acc<Dimensions>()).computed[perspective]);
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

template<typename FeatureSet, IndexType Dimensions>
struct AccumulatorUpdateContext {
    Color                                 perspective;
    const FeatureTransformer<Dimensions>& featureTransformer;
    const AccumulatorState<FeatureSet>&   from;
    AccumulatorState<FeatureSet>&         to;

    AccumulatorUpdateContext(Color                                 persp,
                             const FeatureTransformer<Dimensions>& ft,
                             const AccumulatorState<FeatureSet>&   accF,
                             AccumulatorState<FeatureSet>&         accT) noexcept :
        perspective{persp},
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
          (from.template acc<Dimensions>()).accumulation[perspective].data(),
          (to.template acc<Dimensions>()).accumulation[perspective].data(),
          to_weight_vector(indices)...);

        fused_row_reduce<Vec32Wrapper, PSQTBuckets, ops...>(
          (from.template acc<Dimensions>()).psqtAccumulation[perspective].data(),
          (to.template acc<Dimensions>()).psqtAccumulation[perspective].data(),
          to_psqt_weight_vector(indices)...);
    }

    void apply(const typename FeatureSet::IndexList& added,
               const typename FeatureSet::IndexList& removed) {
        const auto& fromAcc = from.template acc<Dimensions>().accumulation[perspective];
        auto&       toAcc   = to.template acc<Dimensions>().accumulation[perspective];

        const auto& fromPsqtAcc = from.template acc<Dimensions>().psqtAccumulation[perspective];
        auto&       toPsqtAcc   = to.template acc<Dimensions>().psqtAccumulation[perspective];

#ifdef VECTOR
        using Tiling = SIMDTiling<Dimensions, Dimensions, PSQTBuckets>;
        vec_t      acc[Tiling::NumRegs];
        psqt_vec_t psqt[Tiling::NumPsqtRegs];

        const auto* threatWeights = &featureTransformer.threatWeights[0];

        for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
        {
            auto* fromTile = reinterpret_cast<const vec_t*>(&fromAcc[j * Tiling::TileHeight]);
            auto* toTile   = reinterpret_cast<vec_t*>(&toAcc[j * Tiling::TileHeight]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = fromTile[k];

            for (int i = 0; i < removed.ssize(); ++i)
            {
                size_t       index  = removed[i];
                const size_t offset = Dimensions * index;
                auto*        column = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset]);

    #ifdef USE_NEON
                for (IndexType k = 0; k < Tiling::NumRegs; k += 2)
                {
                    acc[k]     = vec_sub_16(acc[k], vmovl_s8(vget_low_s8(column[k / 2])));
                    acc[k + 1] = vec_sub_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
                }
    #else
                for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                    acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));
    #endif
            }

            for (int i = 0; i < added.ssize(); ++i)
            {
                size_t       index  = added[i];
                const size_t offset = Dimensions * index;
                auto*        column = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset]);

    #ifdef USE_NEON
                for (IndexType k = 0; k < Tiling::NumRegs; k += 2)
                {
                    acc[k]     = vec_add_16(acc[k], vmovl_s8(vget_low_s8(column[k / 2])));
                    acc[k + 1] = vec_add_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
                }
    #else
                for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                    acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
    #endif
            }

            for (IndexType k = 0; k < Tiling::NumRegs; k++)
                vec_store(&toTile[k], acc[k]);

            threatWeights += Tiling::TileHeight;
        }

        for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
        {
            auto* fromTilePsqt =
              reinterpret_cast<const psqt_vec_t*>(&fromPsqtAcc[j * Tiling::PsqtTileHeight]);
            auto* toTilePsqt =
              reinterpret_cast<psqt_vec_t*>(&toPsqtAcc[j * Tiling::PsqtTileHeight]);

            for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = fromTilePsqt[k];

            for (int i = 0; i < removed.ssize(); ++i)
            {
                size_t       index      = removed[i];
                const size_t offset     = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
                auto*        columnPsqt = reinterpret_cast<const psqt_vec_t*>(
                  &featureTransformer.threatPsqtWeights[offset]);

                for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                    psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (int i = 0; i < added.ssize(); ++i)
            {
                size_t       index      = added[i];
                const size_t offset     = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
                auto*        columnPsqt = reinterpret_cast<const psqt_vec_t*>(
                  &featureTransformer.threatPsqtWeights[offset]);

                for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                    psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
                vec_store_psqt(&toTilePsqt[k], psqt[k]);
        }

#else

        toAcc     = fromAcc;
        toPsqtAcc = fromPsqtAcc;

        for (const auto index : removed)
        {
            const IndexType offset = Dimensions * index;

            for (IndexType j = 0; j < Dimensions; ++j)
                toAcc[j] -= featureTransformer.threatWeights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                toPsqtAcc[k] -= featureTransformer.threatPsqtWeights[index * PSQTBuckets + k];
        }

        for (const auto index : added)
        {
            const IndexType offset = Dimensions * index;

            for (IndexType j = 0; j < Dimensions; ++j)
                toAcc[j] += featureTransformer.threatWeights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                toPsqtAcc[k] += featureTransformer.threatPsqtWeights[index * PSQTBuckets + k];
        }

#endif
    }
};

template<typename FeatureSet, IndexType Dimensions>
auto make_accumulator_update_context(Color                                 perspective,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     const AccumulatorState<FeatureSet>&   accumulatorFrom,
                                     AccumulatorState<FeatureSet>&         accumulatorTo) noexcept {
    return AccumulatorUpdateContext<FeatureSet, Dimensions>{perspective, featureTransformer,
                                                            accumulatorFrom, accumulatorTo};
}

template<IndexType TransformedFeatureDimensions>
void double_inc_update(Color                                                   perspective,
                       const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState<PSQFeatureSet>&                        middle_state,
                       AccumulatorState<PSQFeatureSet>&                        target_state,
                       const AccumulatorState<PSQFeatureSet>&                  computed) {

    assert(computed.acc<TransformedFeatureDimensions>().computed[perspective]);
    assert(!middle_state.acc<TransformedFeatureDimensions>().computed[perspective]);
    assert(!target_state.acc<TransformedFeatureDimensions>().computed[perspective]);

    PSQFeatureSet::IndexList removed, added;
    PSQFeatureSet::append_changed_indices(perspective, ksq, middle_state.diff, removed, added);
    // you can't capture a piece that was just involved in castling since the rook ends up
    // in a square that the king passed
    assert(added.size() < 2);
    PSQFeatureSet::append_changed_indices(perspective, ksq, target_state.diff, removed, added);

    assert(added.size() == 1);
    assert(removed.size() == 2 || removed.size() == 3);

    // Workaround compiler warning for uninitialized variables, replicated on
    // profile builds on windows with gcc 14.2.0.
    // TODO remove once unneeded
    sf_assume(added.size() == 1);
    sf_assume(removed.size() == 2 || removed.size() == 3);

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computed, target_state);

    if (removed.size() == 2)
    {
        updateContext.template apply<Add, Sub, Sub>(added[0], removed[0], removed[1]);
    }
    else
    {
        updateContext.template apply<Add, Sub, Sub, Sub>(added[0], removed[0], removed[1],
                                                         removed[2]);
    }

    target_state.acc<TransformedFeatureDimensions>().computed[perspective] = true;
}

template<IndexType TransformedFeatureDimensions>
void double_inc_update(Color                                                   perspective,
                       const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
                       const Square                                            ksq,
                       AccumulatorState<ThreatFeatureSet>&                     middle_state,
                       AccumulatorState<ThreatFeatureSet>&                     target_state,
                       const AccumulatorState<ThreatFeatureSet>&               computed,
                       const DirtyPiece&                                       dp2) {

    assert(computed.acc<TransformedFeatureDimensions>().computed[perspective]);
    assert(!middle_state.acc<TransformedFeatureDimensions>().computed[perspective]);
    assert(!target_state.acc<TransformedFeatureDimensions>().computed[perspective]);

    ThreatFeatureSet::FusedUpdateData fusedData;

    fusedData.dp2removed = dp2.remove_sq;

    ThreatFeatureSet::IndexList removed, added;
    ThreatFeatureSet::append_changed_indices(perspective, ksq, middle_state.diff, removed, added,
                                             &fusedData, true);
    ThreatFeatureSet::append_changed_indices(perspective, ksq, target_state.diff, removed, added,
                                             &fusedData, false);

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computed, target_state);

    updateContext.apply(added, removed);

    target_state.acc<TransformedFeatureDimensions>().computed[perspective] = true;
}

template<bool Forward, typename FeatureSet, IndexType TransformedFeatureDimensions>
void update_accumulator_incremental(
  Color                                                   perspective,
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  const Square                                            ksq,
  AccumulatorState<FeatureSet>&                           target_state,
  const AccumulatorState<FeatureSet>&                     computed) {

    assert((computed.template acc<TransformedFeatureDimensions>()).computed[perspective]);
    assert(!(target_state.template acc<TransformedFeatureDimensions>()).computed[perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    // In this case, the maximum size of both feature addition and removal
    // is 2, since we are incrementally updating one move at a time.
    typename FeatureSet::IndexList removed, added;
    if constexpr (Forward)
        FeatureSet::append_changed_indices(perspective, ksq, target_state.diff, removed, added);
    else
        FeatureSet::append_changed_indices(perspective, ksq, computed.diff, added, removed);

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computed, target_state);

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

    (target_state.template acc<TransformedFeatureDimensions>()).computed[perspective] = true;
}

Bitboard get_changed_pieces(const std::array<Piece, SQUARE_NB>& oldPieces,
                            const std::array<Piece, SQUARE_NB>& newPieces) {
#if defined(USE_AVX512) || defined(USE_AVX2)
    static_assert(sizeof(Piece) == 1);
    Bitboard sameBB = 0;

    for (int i = 0; i < 64; i += 32)
    {
        const __m256i old_v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&oldPieces[i]));
        const __m256i new_v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&newPieces[i]));
        const __m256i cmpEqual        = _mm256_cmpeq_epi8(old_v, new_v);
        const std::uint32_t equalMask = _mm256_movemask_epi8(cmpEqual);
        sameBB |= static_cast<Bitboard>(equalMask) << i;
    }
    return ~sameBB;
#elif defined(USE_NEON)
    uint8x16x4_t old_v = vld4q_u8(reinterpret_cast<const uint8_t*>(oldPieces.data()));
    uint8x16x4_t new_v = vld4q_u8(reinterpret_cast<const uint8_t*>(newPieces.data()));
    auto         cmp   = [=](const int i) { return vceqq_u8(old_v.val[i], new_v.val[i]); };

    uint8x16_t cmp0_1 = vsriq_n_u8(cmp(1), cmp(0), 1);
    uint8x16_t cmp2_3 = vsriq_n_u8(cmp(3), cmp(2), 1);
    uint8x16_t merged = vsriq_n_u8(cmp2_3, cmp0_1, 2);
    merged            = vsriq_n_u8(merged, merged, 4);
    uint8x8_t sameBB  = vshrn_n_u16(vreinterpretq_u16_u8(merged), 4);

    return ~vget_lane_u64(vreinterpret_u64_u8(sameBB), 0);
#else
    Bitboard changed = 0;

    for (Square sq = SQUARE_ZERO; sq < SQUARE_NB; ++sq)
        changed |= static_cast<Bitboard>(oldPieces[sq] != newPieces[sq]) << sq;

    return changed;
#endif
}

template<IndexType Dimensions>
void update_accumulator_refresh_cache(Color                                 perspective,
                                      const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState<PSQFeatureSet>&      accumulatorState,
                                      AccumulatorCaches::Cache<Dimensions>& cache) {

    using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, Dimensions, PSQTBuckets>;

    const Square             ksq   = pos.square<KING>(perspective);
    auto&                    entry = cache[ksq][perspective];
    PSQFeatureSet::IndexList removed, added;

    const Bitboard changedBB = get_changed_pieces(entry.pieces, pos.piece_array());
    Bitboard       removedBB = changedBB & entry.pieceBB;
    Bitboard       addedBB   = changedBB & pos.pieces();

    while (removedBB)
    {
        Square sq = pop_lsb(removedBB);
        removed.push_back(PSQFeatureSet::make_index(perspective, sq, entry.pieces[sq], ksq));
    }
    while (addedBB)
    {
        Square sq = pop_lsb(addedBB);
        added.push_back(PSQFeatureSet::make_index(perspective, sq, pos.piece_on(sq), ksq));
    }

    entry.pieceBB = pos.pieces();
    entry.pieces  = pos.piece_array();

    auto& accumulator                 = accumulatorState.acc<Dimensions>();
    accumulator.computed[perspective] = true;

#ifdef VECTOR
    vec_t      acc[Tiling::NumRegs];
    psqt_vec_t psqt[Tiling::NumPsqtRegs];

    const auto* weights = &featureTransformer.weights[0];

    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile =
          reinterpret_cast<vec_t*>(&accumulator.accumulation[perspective][j * Tiling::TileHeight]);
        auto* entryTile = reinterpret_cast<vec_t*>(&entry.accumulation[j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            acc[k] = entryTile[k];

        int i = 0;
        for (; i < std::min(removed.ssize(), added.ssize()); ++i)
        {
            size_t       indexR  = removed[i];
            const size_t offsetR = Dimensions * indexR;
            auto*        columnR = reinterpret_cast<const vec_t*>(&weights[offsetR]);
            size_t       indexA  = added[i];
            const size_t offsetA = Dimensions * indexA;
            auto*        columnA = reinterpret_cast<const vec_t*>(&weights[offsetA]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = fused<Vec16Wrapper, Add, Sub>(acc[k], columnA[k], columnR[k]);
        }
        for (; i < removed.ssize(); ++i)
        {
            size_t       index  = removed[i];
            const size_t offset = Dimensions * index;
            auto*        column = reinterpret_cast<const vec_t*>(&weights[offset]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);
        }
        for (; i < added.ssize(); ++i)
        {
            size_t       index  = added[i];
            const size_t offset = Dimensions * index;
            auto*        column = reinterpret_cast<const vec_t*>(&weights[offset]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&entryTile[k], acc[k]);
        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&accTile[k], acc[k]);

        weights += Tiling::TileHeight;
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
    {
        auto* accTilePsqt = reinterpret_cast<psqt_vec_t*>(
          &accumulator.psqtAccumulation[perspective][j * Tiling::PsqtTileHeight]);
        auto* entryTilePsqt =
          reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[j * Tiling::PsqtTileHeight]);

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            psqt[k] = entryTilePsqt[k];

        for (int i = 0; i < removed.ssize(); ++i)
        {
            size_t       index  = removed[i];
            const size_t offset = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
            auto*        columnPsqt =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offset]);

            for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }
        for (int i = 0; i < added.ssize(); ++i)
        {
            size_t       index  = added[i];
            const size_t offset = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
            auto*        columnPsqt =
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
    accumulator.accumulation[perspective]     = entry.accumulation;
    accumulator.psqtAccumulation[perspective] = entry.psqtAccumulation;
#endif
}

template<IndexType Dimensions>
void update_threats_accumulator_full(Color                                 perspective,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     const Position&                       pos,
                                     AccumulatorState<ThreatFeatureSet>&   accumulatorState) {
    using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, Dimensions, PSQTBuckets>;

    ThreatFeatureSet::IndexList active;
    ThreatFeatureSet::append_active_indices(perspective, pos, active);

    auto& accumulator                 = accumulatorState.acc<Dimensions>();
    accumulator.computed[perspective] = true;

#ifdef VECTOR
    vec_t      acc[Tiling::NumRegs];
    psqt_vec_t psqt[Tiling::NumPsqtRegs];

    const auto* threatWeights = &featureTransformer.threatWeights[0];

    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile =
          reinterpret_cast<vec_t*>(&accumulator.accumulation[perspective][j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            acc[k] = vec_zero();

        int i = 0;

        for (; i < active.ssize(); ++i)
        {
            size_t       index  = active[i];
            const size_t offset = Dimensions * index;
            auto*        column = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset]);

    #ifdef USE_NEON
            for (IndexType k = 0; k < Tiling::NumRegs; k += 2)
            {
                acc[k]     = vec_add_16(acc[k], vmovl_s8(vget_low_s8(column[k / 2])));
                acc[k + 1] = vec_add_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
            }
    #else
            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
    #endif
        }

        for (IndexType k = 0; k < Tiling::NumRegs; k++)
            vec_store(&accTile[k], acc[k]);

        threatWeights += Tiling::TileHeight;
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PsqtTileHeight; ++j)
    {
        auto* accTilePsqt = reinterpret_cast<psqt_vec_t*>(
          &accumulator.psqtAccumulation[perspective][j * Tiling::PsqtTileHeight]);

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            psqt[k] = vec_zero_psqt();

        for (int i = 0; i < active.ssize(); ++i)
        {
            size_t       index  = active[i];
            const size_t offset = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
            auto*        columnPsqt =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.threatPsqtWeights[offset]);

            for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&accTilePsqt[k], psqt[k]);
    }

#else

    for (IndexType j = 0; j < Dimensions; ++j)
        accumulator.accumulation[perspective][j] = 0;

    for (std::size_t k = 0; k < PSQTBuckets; ++k)
        accumulator.psqtAccumulation[perspective][k] = 0;

    for (const auto index : active)
    {
        const IndexType offset = Dimensions * index;

        for (IndexType j = 0; j < Dimensions; ++j)
            accumulator.accumulation[perspective][j] +=
              featureTransformer.threatWeights[offset + j];

        for (std::size_t k = 0; k < PSQTBuckets; ++k)
            accumulator.psqtAccumulation[perspective][k] +=
              featureTransformer.threatPsqtWeights[index * PSQTBuckets + k];
    }

#endif
}

}

}
