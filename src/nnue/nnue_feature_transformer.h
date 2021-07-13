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

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include "nnue_common.h"
#include "nnue_architecture.h"

#include "../position.h"

#include <iostream>
#include <cstdint>

namespace Stockfish::Eval::NNUE {

  static_assert(PSQTBuckets % 8 == 0,
    "Per feature PSQT values cannot be processed at granularity lower than 8 at a time.");
  static_assert(TransformedFeatureDimensions % 32 == 0, "");

  // Input feature converter
  class FeatureTransformer_Base {
   protected:
    using BiasType       = std::int16_t;
    using WeightType     = std::int16_t;
    using PSQTWeightType = std::int32_t;

    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions = FeatureSet::Dimensions;
    static constexpr IndexType OutputDimensions = HalfDimensions * 2;

    // Size of forward propagation buffer
    static constexpr std::size_t BufferSize =
        OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() {
      return FeatureSet::HashValue ^ OutputDimensions;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {

      read_little_endian<BiasType      >(stream, biases     , HalfDimensions                  );
      read_little_endian<WeightType    >(stream, weights    , HalfDimensions * InputDimensions);
      read_little_endian<PSQTWeightType>(stream, psqtWeights, PSQTBuckets    * InputDimensions);

      return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {

      write_little_endian<BiasType      >(stream, biases     , HalfDimensions                  );
      write_little_endian<WeightType    >(stream, weights    , HalfDimensions * InputDimensions);
      write_little_endian<PSQTWeightType>(stream, psqtWeights, PSQTBuckets    * InputDimensions);

      return !stream.fail();
    }

   protected:
    alignas(CacheLineSize) BiasType biases[HalfDimensions];
    alignas(CacheLineSize) WeightType weights[HalfDimensions * InputDimensions];
    alignas(CacheLineSize) PSQTWeightType psqtWeights[InputDimensions * PSQTBuckets];

    std::pair<StateInfo*, StateInfo*> try_search_for_computed(const Position& pos, Color perspective) const
    {
      StateInfo *st = pos.state(), *next = nullptr;
      int gain = FeatureSet::refresh_cost(pos);
      while (st->previous && !st->accumulator.computed[perspective])
      {
        // This governs when a full feature refresh is needed and how many
        // updates are better than just one full refresh.
        if (   FeatureSet::requires_refresh(st, perspective)
            || (gain -= FeatureSet::update_cost(st) + 1) < 0)
          break;
        next = st;
        st = st->previous;
      }
      return { st, next };
    }
  };

}  // namespace Stockfish::Eval::NNUE

#include "nnue_feature_transformer_vec.h"

#if defined (FEATURE_TRANSFORMER_NO_VEC)

#   include "nnue_feature_transformer_scalar.h"

namespace Stockfish::Eval::NNUE {
    using FeatureTransformer = FeatureTransformer_Scalar;
}

#else

namespace Stockfish::Eval::NNUE {
    using FeatureTransformer = FeatureTransformer_Vec;
}

#endif

#endif // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
