/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

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

// A class that converts the input features of the NNUE evaluation function

#ifndef NNUE_FEATURE_TRANSFORMER_SCALAR_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_SCALAR_H_INCLUDED

#if !defined (NNUE_FEATURE_TRANSFORMER_H_INCLUDED)
#error "This file can only be included through nnue_feature_transformer.h"
#endif

#include <cstring>

namespace Stockfish::Eval::NNUE {

  // Input feature converter
  class FeatureTransformer_Scalar : public FeatureTransformer_Base {
   public:
    using BaseType = FeatureTransformer_Base;

   private:

    using BiasType       = typename FeatureTransformer_Base::BiasType;
    using WeightType     = typename FeatureTransformer_Base::WeightType;
    using PSQTWeightType = typename FeatureTransformer_Base::PSQTWeightType;

    // Number of output dimensions for one side
    static constexpr auto HalfDimensions = FeatureTransformer_Base::HalfDimensions;

   public:
    // Output type
    using OutputType = typename FeatureTransformer_Base::OutputType;

    // Number of input/output dimensions
    static constexpr auto InputDimensions = FeatureTransformer_Base::InputDimensions;
    static constexpr auto OutputDimensions = FeatureTransformer_Base::OutputDimensions;

    // Size of forward propagation buffer
    static constexpr auto BufferSize = FeatureTransformer_Base::BufferSize;

    // Convert input features
    std::int32_t transform(const Position& pos, OutputType* output, int bucket) const {
      update_accumulator(pos, WHITE);
      update_accumulator(pos, BLACK);

      const Color perspectives[2] = {pos.side_to_move(), ~pos.side_to_move()};
      const auto& accumulation = pos.state()->accumulator.accumulation;
      const auto& psqtAccumulation = pos.state()->accumulator.psqtAccumulation;

      const auto psqt = (
          psqtAccumulation[perspectives[0]][bucket]
        - psqtAccumulation[perspectives[1]][bucket]
      ) / 2;

      for (IndexType p = 0; p < 2; ++p)
      {
        const IndexType offset = HalfDimensions * p;
        for (IndexType j = 0; j < HalfDimensions; ++j)
        {
          BiasType sum = accumulation[perspectives[p]][j];
          if (sum < 0)   sum = 0;
          if (sum > 127) sum = 127;
          output[offset + j] = static_cast<OutputType>(sum);
        }
      }

      return psqt;

   } // end of function transform()

   private:
    void update_accumulator(const Position& pos, const Color perspective) const {

      // The size must be enough to contain the largest possible update.
      // That might depend on the feature set and generally relies on the
      // feature set's update cost calculation to be correct and never
      // allow updates with more added/removed features than MaxActiveDimensions.
      using IndexList = ValueList<IndexType, FeatureSet::MaxActiveDimensions>;

      // Look for a usable accumulator of an earlier position. We keep track
      // of the estimated gain in terms of features to be added/subtracted.
      auto [st, next] = BaseType::try_search_for_computed(pos, perspective);

      if (st->accumulator.computed[perspective])
      {
        if (next == nullptr)
          return;

        // Update incrementally in two steps. First, we update the "next"
        // accumulator. Then, we update the current accumulator (pos.state()).

        // Gather all features to be updated.
        const Square ksq = pos.square<KING>(perspective);
        IndexList removed[2], added[2];
        FeatureSet::append_changed_indices(
          ksq, next, perspective, removed[0], added[0]);
        for (StateInfo *st2 = pos.state(); st2 != next; st2 = st2->previous)
          FeatureSet::append_changed_indices(
            ksq, st2, perspective, removed[1], added[1]);

        // Mark the accumulators as computed.
        next->accumulator.computed[perspective] = true;
        pos.state()->accumulator.computed[perspective] = true;
        // Now update the accumulators listed in states_to_update[], where the last element is a sentinel.
        StateInfo *states_to_update[3] =
          { next, next == pos.state() ? nullptr : pos.state(), nullptr };

        for (IndexType i = 0; states_to_update[i]; ++i)
        {
          std::memcpy(states_to_update[i]->accumulator.accumulation[perspective],
            st->accumulator.accumulation[perspective],
            HalfDimensions * sizeof(BiasType));

          for (std::size_t k = 0; k < PSQTBuckets; ++k)
            states_to_update[i]->accumulator.psqtAccumulation[perspective][k] = st->accumulator.psqtAccumulation[perspective][k];

          st = states_to_update[i];

          // Difference calculation for the deactivated features
          for (const auto index : removed[i])
          {
            const IndexType offset = HalfDimensions * index;

            for (IndexType j = 0; j < HalfDimensions; ++j)
              st->accumulator.accumulation[perspective][j] -= weights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
              st->accumulator.psqtAccumulation[perspective][k] -= psqtWeights[index * PSQTBuckets + k];
          }

          // Difference calculation for the activated features
          for (const auto index : added[i])
          {
            const IndexType offset = HalfDimensions * index;

            for (IndexType j = 0; j < HalfDimensions; ++j)
              st->accumulator.accumulation[perspective][j] += weights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
              st->accumulator.psqtAccumulation[perspective][k] += psqtWeights[index * PSQTBuckets + k];
          }
        }
      }
      else
      {
        // Refresh the accumulator
        auto& accumulator = pos.state()->accumulator;
        accumulator.computed[perspective] = true;
        IndexList active;
        FeatureSet::append_active_indices(pos, perspective, active);

        std::memcpy(accumulator.accumulation[perspective], biases,
            HalfDimensions * sizeof(BiasType));

        for (std::size_t k = 0; k < PSQTBuckets; ++k)
          accumulator.psqtAccumulation[perspective][k] = 0;

        for (const auto index : active)
        {
          const IndexType offset = HalfDimensions * index;

          for (IndexType j = 0; j < HalfDimensions; ++j)
            accumulator.accumulation[perspective][j] += weights[offset + j];

          for (std::size_t k = 0; k < PSQTBuckets; ++k)
            accumulator.psqtAccumulation[perspective][k] += psqtWeights[index * PSQTBuckets + k];
        }
      }
    }

    using BaseType::biases;
    using BaseType::weights;
    using BaseType::psqtWeights;
  };

}  // namespace Stockfish::Eval::NNUE

#endif // #ifndef NNUE_FEATURE_TRANSFORMER_SCALAR_H_INCLUDED
