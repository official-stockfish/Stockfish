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
#include <initializer_list>
#include <memory>

#include "../bitboard.h"
#include "../position.h"
#include "../types.h"
#include "network.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_feature_transformer.h"

namespace Stockfish::Eval::NNUE {

#if defined(__GNUC__) && !defined(__clang__)
    #define sf_assume(cond) \
        do \
        { \
            if (!(cond)) \
                __builtin_unreachable(); \
        } while (0)
#else
    // do nothing for other compilers
    #define sf_assume(cond)
#endif

namespace {

template<Color                                     Perspective,
         IncUpdateDirection                        Direction = FORWARD,
         IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> AccumulatorState::*accPtr>
void update_accumulator_incremental(
  const FeatureTransformer<TransformedFeatureDimensions, accPtr>& featureTransformer,
  const Square                                                    ksq,
  AccumulatorState&                                               target_state,
  const AccumulatorState&                                         computed);

template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
void update_accumulator_refresh_cache(
  const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
  const Position&                               pos,
  AccumulatorState&                             accumulatorState,
  AccumulatorCaches::Cache<Dimensions>&         cache);

}

void AccumulatorState::reset(const DirtyPiece& dp) noexcept {
    dirtyPiece = dp;
    accumulatorBig.computed.fill(false);
    accumulatorSmall.computed.fill(false);
}

const AccumulatorState& AccumulatorStack::latest() const noexcept {
    return m_accumulators[m_current_idx - 1];
}

AccumulatorState& AccumulatorStack::mut_latest() noexcept {
    return m_accumulators[m_current_idx - 1];
}

void AccumulatorStack::reset(const Position&    rootPos,
                             const Networks&    networks,
                             AccumulatorCaches& caches) noexcept {
    m_current_idx = 1;

    update_accumulator_refresh_cache<WHITE, TransformedFeatureDimensionsBig,
                                     &AccumulatorState::accumulatorBig>(
      *networks.big.featureTransformer, rootPos, m_accumulators[0], caches.big);
    update_accumulator_refresh_cache<BLACK, TransformedFeatureDimensionsBig,
                                     &AccumulatorState::accumulatorBig>(
      *networks.big.featureTransformer, rootPos, m_accumulators[0], caches.big);

    update_accumulator_refresh_cache<WHITE, TransformedFeatureDimensionsSmall,
                                     &AccumulatorState::accumulatorSmall>(
      *networks.small.featureTransformer, rootPos, m_accumulators[0], caches.small);
    update_accumulator_refresh_cache<BLACK, TransformedFeatureDimensionsSmall,
                                     &AccumulatorState::accumulatorSmall>(
      *networks.small.featureTransformer, rootPos, m_accumulators[0], caches.small);
}

void AccumulatorStack::push(const DirtyPiece& dirtyPiece) noexcept {
    assert(m_current_idx + 1 < m_accumulators.size());
    m_accumulators[m_current_idx].reset(dirtyPiece);
    m_current_idx++;
}

void AccumulatorStack::pop() noexcept {
    assert(m_current_idx > 1);
    m_current_idx--;
}

template<IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
void AccumulatorStack::evaluate(const Position&                               pos,
                                const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                                AccumulatorCaches::Cache<Dimensions>&         cache) noexcept {

    evaluate_side<WHITE>(pos, featureTransformer, cache);
    evaluate_side<BLACK>(pos, featureTransformer, cache);
}

template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
void AccumulatorStack::evaluate_side(
  const Position&                               pos,
  const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
  AccumulatorCaches::Cache<Dimensions>&         cache) noexcept {

    const auto last_usable_accum = find_last_usable_accumulator<Perspective, Dimensions, accPtr>();

    if ((m_accumulators[last_usable_accum].*accPtr).computed[Perspective])
        forward_update_incremental<Perspective>(pos, featureTransformer, last_usable_accum);

    else
    {
        update_accumulator_refresh_cache<Perspective>(featureTransformer, pos, mut_latest(), cache);
        backward_update_incremental<Perspective>(pos, featureTransformer, last_usable_accum);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
std::size_t AccumulatorStack::find_last_usable_accumulator() const noexcept {

    for (std::size_t curr_idx = m_current_idx - 1; curr_idx > 0; curr_idx--)
    {
        if ((m_accumulators[curr_idx].*accPtr).computed[Perspective])
            return curr_idx;

        if (FeatureSet::requires_refresh(m_accumulators[curr_idx].dirtyPiece, Perspective))
            return curr_idx;
    }

    return 0;
}

template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
void AccumulatorStack::forward_update_incremental(
  const Position&                               pos,
  const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
  const std::size_t                             begin) noexcept {

    assert(begin < m_accumulators.size());
    assert((m_accumulators[begin].*accPtr).computed[Perspective]);

    const Square ksq = pos.square<KING>(Perspective);

    for (std::size_t next = begin + 1; next < m_current_idx; next++)
        update_accumulator_incremental<Perspective>(featureTransformer, ksq, m_accumulators[next],
                                                    m_accumulators[next - 1]);

    assert((latest().*accPtr).computed[Perspective]);
}

template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
void AccumulatorStack::backward_update_incremental(
  const Position&                               pos,
  const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
  const std::size_t                             end) noexcept {

    assert(end < m_accumulators.size());
    assert(end < m_current_idx);
    assert((latest().*accPtr).computed[Perspective]);

    const Square ksq = pos.square<KING>(Perspective);

    for (std::size_t next = m_current_idx - 2; next >= end; next--)
        update_accumulator_incremental<Perspective, BACKWARDS>(
          featureTransformer, ksq, m_accumulators[next], m_accumulators[next + 1]);

    assert((m_accumulators[end].*accPtr).computed[Perspective]);
}

// Explicit template instantiations
template void
AccumulatorStack::evaluate<TransformedFeatureDimensionsBig, &AccumulatorState::accumulatorBig>(
  const Position& pos,
  const FeatureTransformer<TransformedFeatureDimensionsBig, &AccumulatorState::accumulatorBig>&
                                                             featureTransformer,
  AccumulatorCaches::Cache<TransformedFeatureDimensionsBig>& cache) noexcept;
template void
AccumulatorStack::evaluate<TransformedFeatureDimensionsSmall, &AccumulatorState::accumulatorSmall>(
  const Position& pos,
  const FeatureTransformer<TransformedFeatureDimensionsSmall, &AccumulatorState::accumulatorSmall>&
                                                               featureTransformer,
  AccumulatorCaches::Cache<TransformedFeatureDimensionsSmall>& cache) noexcept;


namespace {

template<Color                                     Perspective,
         IncUpdateDirection                        Direction,
         IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> AccumulatorState::*accPtr>
void update_accumulator_incremental(
  const FeatureTransformer<TransformedFeatureDimensions, accPtr>& featureTransformer,
  const Square                                                    ksq,
  AccumulatorState&                                               target_state,
  const AccumulatorState&                                         computed) {
    [[maybe_unused]] constexpr bool Forward   = Direction == FORWARD;
    [[maybe_unused]] constexpr bool Backwards = Direction == BACKWARDS;

    assert(Forward != Backwards);

    assert((computed.*accPtr).computed[Perspective]);
    assert(!(target_state.*accPtr).computed[Perspective]);

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

    if (removed.size() == 0 && added.size() == 0)
    {
        std::memcpy((target_state.*accPtr).accumulation[Perspective],
                    (computed.*accPtr).accumulation[Perspective],
                    TransformedFeatureDimensions * sizeof(BiasType));
        std::memcpy((target_state.*accPtr).psqtAccumulation[Perspective],
                    (computed.*accPtr).psqtAccumulation[Perspective],
                    PSQTBuckets * sizeof(PSQTWeightType));
    }
    else
    {
        assert(added.size() == 1 || added.size() == 2);
        assert(removed.size() == 1 || removed.size() == 2);

        if (Forward)
            assert(added.size() <= removed.size());
        else
            assert(removed.size() <= added.size());

        // Workaround compiler warning for uninitialized variables, replicated on
        // profile builds on windows with gcc 14.2.0.
        // TODO remove once unneeded
        sf_assume(added.size() == 1 || added.size() == 2);
        sf_assume(removed.size() == 1 || removed.size() == 2);

#ifdef VECTOR
        auto* accIn =
          reinterpret_cast<const vec_t*>(&(computed.*accPtr).accumulation[Perspective][0]);
        auto* accOut =
          reinterpret_cast<vec_t*>(&(target_state.*accPtr).accumulation[Perspective][0]);

        const IndexType offsetA0 = TransformedFeatureDimensions * added[0];
        auto* columnA0 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA0]);
        const IndexType offsetR0 = TransformedFeatureDimensions * removed[0];
        auto* columnR0 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR0]);

        if ((Forward && removed.size() == 1) || (Backwards && added.size() == 1))
        {
            assert(added.size() == 1 && removed.size() == 1);
            for (IndexType i = 0;
                 i < TransformedFeatureDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                accOut[i] = vec_add_16(vec_sub_16(accIn[i], columnR0[i]), columnA0[i]);
        }
        else if (Forward && added.size() == 1)
        {
            assert(removed.size() == 2);
            const IndexType offsetR1 = TransformedFeatureDimensions * removed[1];
            auto* columnR1 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR1]);

            for (IndexType i = 0;
                 i < TransformedFeatureDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                accOut[i] = vec_sub_16(vec_add_16(accIn[i], columnA0[i]),
                                       vec_add_16(columnR0[i], columnR1[i]));
        }
        else if (Backwards && removed.size() == 1)
        {
            assert(added.size() == 2);
            const IndexType offsetA1 = TransformedFeatureDimensions * added[1];
            auto* columnA1 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA1]);

            for (IndexType i = 0;
                 i < TransformedFeatureDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                accOut[i] = vec_add_16(vec_add_16(accIn[i], columnA0[i]),
                                       vec_sub_16(columnA1[i], columnR0[i]));
        }
        else
        {
            assert(added.size() == 2 && removed.size() == 2);
            const IndexType offsetA1 = TransformedFeatureDimensions * added[1];
            auto* columnA1 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA1]);
            const IndexType offsetR1 = TransformedFeatureDimensions * removed[1];
            auto* columnR1 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR1]);

            for (IndexType i = 0;
                 i < TransformedFeatureDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                accOut[i] = vec_add_16(accIn[i], vec_sub_16(vec_add_16(columnA0[i], columnA1[i]),
                                                            vec_add_16(columnR0[i], columnR1[i])));
        }

        auto* accPsqtIn =
          reinterpret_cast<const psqt_vec_t*>(&(computed.*accPtr).psqtAccumulation[Perspective][0]);
        auto* accPsqtOut =
          reinterpret_cast<psqt_vec_t*>(&(target_state.*accPtr).psqtAccumulation[Perspective][0]);

        const IndexType offsetPsqtA0 = PSQTBuckets * added[0];
        auto*           columnPsqtA0 =
          reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offsetPsqtA0]);
        const IndexType offsetPsqtR0 = PSQTBuckets * removed[0];
        auto*           columnPsqtR0 =
          reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offsetPsqtR0]);

        if ((Forward && removed.size() == 1)
            || (Backwards && added.size() == 1))  // added.size() == removed.size() == 1
        {
            for (std::size_t i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t);
                 ++i)
                accPsqtOut[i] =
                  vec_add_psqt_32(vec_sub_psqt_32(accPsqtIn[i], columnPsqtR0[i]), columnPsqtA0[i]);
        }
        else if (Forward && added.size() == 1)
        {
            const IndexType offsetPsqtR1 = PSQTBuckets * removed[1];
            auto*           columnPsqtR1 =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offsetPsqtR1]);

            for (std::size_t i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t);
                 ++i)
                accPsqtOut[i] = vec_sub_psqt_32(vec_add_psqt_32(accPsqtIn[i], columnPsqtA0[i]),
                                                vec_add_psqt_32(columnPsqtR0[i], columnPsqtR1[i]));
        }
        else if (Backwards && removed.size() == 1)
        {
            const IndexType offsetPsqtA1 = PSQTBuckets * added[1];
            auto*           columnPsqtA1 =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offsetPsqtA1]);

            for (std::size_t i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t);
                 ++i)
                accPsqtOut[i] = vec_add_psqt_32(vec_add_psqt_32(accPsqtIn[i], columnPsqtA0[i]),
                                                vec_sub_psqt_32(columnPsqtA1[i], columnPsqtR0[i]));
        }
        else
        {
            const IndexType offsetPsqtA1 = PSQTBuckets * added[1];
            auto*           columnPsqtA1 =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offsetPsqtA1]);
            const IndexType offsetPsqtR1 = PSQTBuckets * removed[1];
            auto*           columnPsqtR1 =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offsetPsqtR1]);

            for (std::size_t i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t);
                 ++i)
                accPsqtOut[i] = vec_add_psqt_32(
                  accPsqtIn[i], vec_sub_psqt_32(vec_add_psqt_32(columnPsqtA0[i], columnPsqtA1[i]),
                                                vec_add_psqt_32(columnPsqtR0[i], columnPsqtR1[i])));
        }
#else
        std::memcpy((target_state.*accPtr).accumulation[Perspective],
                    (computed.*accPtr).accumulation[Perspective],
                    TransformedFeatureDimensions * sizeof(BiasType));
        std::memcpy((target_state.*accPtr).psqtAccumulation[Perspective],
                    (computed.*accPtr).psqtAccumulation[Perspective],
                    PSQTBuckets * sizeof(PSQTWeightType));

        // Difference calculation for the deactivated features
        for (const auto index : removed)
        {
            const IndexType offset = TransformedFeatureDimensions * index;
            for (IndexType i = 0; i < TransformedFeatureDimensions; ++i)
                (target_state.*accPtr).accumulation[Perspective][i] -=
                  featureTransformer.weights[offset + i];

            for (std::size_t i = 0; i < PSQTBuckets; ++i)
                (target_state.*accPtr).psqtAccumulation[Perspective][i] -=
                  featureTransformer.psqtWeights[index * PSQTBuckets + i];
        }

        // Difference calculation for the activated features
        for (const auto index : added)
        {
            const IndexType offset = TransformedFeatureDimensions * index;
            for (IndexType i = 0; i < TransformedFeatureDimensions; ++i)
                (target_state.*accPtr).accumulation[Perspective][i] +=
                  featureTransformer.weights[offset + i];

            for (std::size_t i = 0; i < PSQTBuckets; ++i)
                (target_state.*accPtr).psqtAccumulation[Perspective][i] +=
                  featureTransformer.psqtWeights[index * PSQTBuckets + i];
        }
#endif
    }

    (target_state.*accPtr).computed[Perspective] = true;
}

template<Color Perspective, IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
void update_accumulator_refresh_cache(
  const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
  const Position&                               pos,
  AccumulatorState&                             accumulatorState,
  AccumulatorCaches::Cache<Dimensions>&         cache) {
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

    auto& accumulator                 = accumulatorState.*accPtr;
    accumulator.computed[Perspective] = true;

#ifdef VECTOR
    const bool combineLast3 =
      std::abs((int) removed.size() - (int) added.size()) == 1 && removed.size() + added.size() > 2;
    vec_t      acc[Tiling::NumRegs];
    psqt_vec_t psqt[Tiling::NumPsqtRegs];

    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile =
          reinterpret_cast<vec_t*>(&accumulator.accumulation[Perspective][j * Tiling::TileHeight]);
        auto* entryTile = reinterpret_cast<vec_t*>(&entry.accumulation[j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::NumRegs; ++k)
            acc[k] = entryTile[k];

        std::size_t i = 0;
        for (; i < std::min(removed.size(), added.size()) - combineLast3; ++i)
        {
            IndexType       indexR  = removed[i];
            const IndexType offsetR = Dimensions * indexR + j * Tiling::TileHeight;
            auto* columnR = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR]);
            IndexType       indexA  = added[i];
            const IndexType offsetA = Dimensions * indexA + j * Tiling::TileHeight;
            auto* columnA = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA]);

            for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                acc[k] = vec_add_16(acc[k], vec_sub_16(columnA[k], columnR[k]));
        }
        if (combineLast3)
        {
            IndexType       indexR  = removed[i];
            const IndexType offsetR = Dimensions * indexR + j * Tiling::TileHeight;
            auto* columnR = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR]);
            IndexType       indexA  = added[i];
            const IndexType offsetA = Dimensions * indexA + j * Tiling::TileHeight;
            auto* columnA = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA]);

            if (removed.size() > added.size())
            {
                IndexType       indexR2  = removed[i + 1];
                const IndexType offsetR2 = Dimensions * indexR2 + j * Tiling::TileHeight;
                auto*           columnR2 =
                  reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR2]);

                for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                    acc[k] = vec_sub_16(vec_add_16(acc[k], columnA[k]),
                                        vec_add_16(columnR[k], columnR2[k]));
            }
            else
            {
                IndexType       indexA2  = added[i + 1];
                const IndexType offsetA2 = Dimensions * indexA2 + j * Tiling::TileHeight;
                auto*           columnA2 =
                  reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA2]);

                for (IndexType k = 0; k < Tiling::NumRegs; ++k)
                    acc[k] = vec_add_16(vec_sub_16(acc[k], columnR[k]),
                                        vec_add_16(columnA[k], columnA2[k]));
            }
        }
        else
        {
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

        for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
            psqt[k] = entryTilePsqt[k];

        for (std::size_t i = 0; i < removed.size(); ++i)
        {
            IndexType       index  = removed[i];
            const IndexType offset = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
            auto*           columnPsqt =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offset]);

            for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }
        for (std::size_t i = 0; i < added.size(); ++i)
        {
            IndexType       index  = added[i];
            const IndexType offset = PSQTBuckets * index + j * Tiling::PsqtTileHeight;
            auto*           columnPsqt =
              reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offset]);

            for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
            vec_store_psqt(&entryTilePsqt[k], psqt[k]);
        for (std::size_t k = 0; k < Tiling::NumPsqtRegs; ++k)
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
