/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#ifndef GENERIC_NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define GENERIC_NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

#include <algorithm>
#include <cstring>

#include "position.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE {

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
template<bool Write>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::permute_weights() {}

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
template<Color Perspective, bool CurrentOnly>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::
  apply_accumulator_updates_incremental(StateInfo*             computed,
                                        StateInfo*             next,
                                        FeatureSet::IndexList& removed,
                                        FeatureSet::IndexList& added) const {

    std::memcpy((next->*accPtr).accumulation[Perspective],
                (computed->*accPtr).accumulation[Perspective], HalfDimensions * sizeof(BiasType));
    std::memcpy((next->*accPtr).psqtAccumulation[Perspective],
                (computed->*accPtr).psqtAccumulation[Perspective],
                PSQTBuckets * sizeof(PSQTWeightType));

    // Difference calculation for the deactivated features
    for (const auto index : removed)
    {
        const IndexType wOffset  = HalfDimensions * index;
        const IndexType pwOffset = PSQTBuckets * index;

        for (IndexType i = 0; i < HalfDimensions; ++i)
            (next->*accPtr).accumulation[Perspective][i] -= weights[wOffset + i];

        for (IndexType i = 0; i < PSQTBuckets; ++i)
            (next->*accPtr).psqtAccumulation[Perspective][i] -= psqtWeights[pwOffset + i];
    }

    // Difference calculation for the activated features
    for (const auto index : added)
    {
        const IndexType wOffset  = HalfDimensions * index;
        const IndexType pwOffset = PSQTBuckets * index;

        for (IndexType i = 0; i < HalfDimensions; ++i)
            (next->*accPtr).accumulation[Perspective][i] += weights[wOffset + i];

        for (IndexType i = 0; i < PSQTBuckets; ++i)
            (next->*accPtr).psqtAccumulation[Perspective][i] += psqtWeights[pwOffset + i];
    }
}

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
template<Color Perspective>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::
  apply_accumulator_updates_refresh_cache(
    Accumulator<TransformedFeatureDimensions>&                accumulator,
    typename AccumulatorCaches::Cache<HalfDimensions>::Entry& entry,
    FeatureSet::IndexList                                     removed,
    FeatureSet::IndexList                                     added) const {
    for (const auto index : removed)
    {
        const IndexType wOffset  = HalfDimensions * index;
        const IndexType pwOffset = PSQTBuckets * index;

        for (IndexType j = 0; j < HalfDimensions; ++j)
            entry.accumulation[j] -= weights[wOffset + j];

        for (IndexType k = 0; k < PSQTBuckets; ++k)
            entry.psqtAccumulation[k] -= psqtWeights[pwOffset + k];
    }
    for (const auto index : added)
    {
        const IndexType wOffset  = HalfDimensions * index;
        const IndexType pwOffset = PSQTBuckets * index;

        for (IndexType j = 0; j < HalfDimensions; ++j)
            entry.accumulation[j] += weights[wOffset + j];

        for (IndexType k = 0; k < PSQTBuckets; ++k)
            entry.psqtAccumulation[k] += psqtWeights[pwOffset + k];
    }

    // The accumulator of the refresh entry has been updated.
    // Now copy its content to the actual accumulator we were refreshing.
    std::memcpy(accumulator.accumulation[Perspective], entry.accumulation,
                sizeof(BiasType) * HalfDimensions);
    std::memcpy(accumulator.psqtAccumulation[Perspective], entry.psqtAccumulation,
                sizeof(PSQTWeightType) * PSQTBuckets);
}

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::convert_accumulators(
  const Position& pos, OutputType* output) const {
    const int   perspectives[2] = {pos.side_to_move(), ~pos.side_to_move()};
    const auto& accumulation    = (pos.state()->*accPtr).accumulation;

    for (IndexType p = 0; p < 2; ++p)
    {
        const IndexType offset = (HalfDimensions / 2) * p;

        for (IndexType j = 0; j < HalfDimensions / 2; ++j)
        {
            BiasType sum0      = accumulation[perspectives[p]][j];
            BiasType sum1      = accumulation[perspectives[p]][j + HalfDimensions / 2];
            sum0               = std::clamp<BiasType>(sum0, 0, 127 * 2);
            sum1               = std::clamp<BiasType>(sum1, 0, 127 * 2);
            output[offset + j] = static_cast<OutputType>(unsigned(sum0 * sum1) / 512);
        }
    }
}

}  // namespace Stockfish::Eval::NNUE

#endif  // GENERIC_NNUE_FEATURE_TRANSFORMER_H_INCLUDED
