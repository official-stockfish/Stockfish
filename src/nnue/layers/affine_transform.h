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

// Definition of layer AffineTransform of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#include <iostream>
#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

  // Affine transformation layer
  template <typename PreviousLayer, IndexType OutDims>
  class AffineTransform {
   public:
    // Input/output type
    using InputType = typename PreviousLayer::OutputType;
    using OutputType = std::int32_t;
    static_assert(std::is_same<InputType, std::uint8_t>::value, "");

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions =
        PreviousLayer::OutputDimensions;
    static constexpr IndexType OutputDimensions = OutDims;
    static constexpr IndexType PaddedInputDimensions =
        ceil_to_multiple<IndexType>(InputDimensions, 32);

    // Size of forward propagation buffer used in this layer
    static constexpr std::size_t SelfBufferSize =
        ceil_to_multiple(OutputDimensions * sizeof(OutputType), CacheLineSize);

    // Size of the forward propagation buffer used from the input layer to this layer
    static constexpr std::size_t BufferSize =
        PreviousLayer::BufferSize + SelfBufferSize;

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() {
      std::uint32_t hashValue = 0xCC03DAE4u;
      hashValue += OutputDimensions;
      hashValue ^= PreviousLayer::get_hash_value() >> 1;
      hashValue ^= PreviousLayer::get_hash_value() << 31;
      return hashValue;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
      if (!previousLayer.read_parameters(stream)) return false;
      for (std::size_t i = 0; i < OutputDimensions; ++i)
        biases[i] = read_little_endian<BiasType>(stream);
      for (std::size_t i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
        weights[i] = read_little_endian<WeightType>(stream);
      return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
      if (!previousLayer.write_parameters(stream)) return false;
      for (std::size_t i = 0; i < OutputDimensions; ++i)
          write_little_endian<BiasType>(stream, biases[i]);
      for (std::size_t i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
          write_little_endian<WeightType>(stream, weights[i]);
      return !stream.fail();
    }

    // Forward propagation
    const OutputType* propagate(
        const TransformedFeatureType* transformedFeatures, char* buffer) const {
      const auto input = previousLayer.propagate(
          transformedFeatures, buffer + SelfBufferSize);

      auto output = reinterpret_cast<OutputType*>(buffer);

      for (IndexType i = 0; i < OutputDimensions; ++i) {
        const IndexType offset = i * PaddedInputDimensions;
        OutputType sum = biases[i];
        for (IndexType j = 0; j < InputDimensions; ++j) {
          sum += weights[offset + j] * input[j];
        }
        output[i] = sum;
      }
      return output;
    }

   private:
    using BiasType = OutputType;
    using WeightType = std::int8_t;

    PreviousLayer previousLayer;

    alignas(CacheLineSize) BiasType biases[OutputDimensions];
    alignas(CacheLineSize) WeightType weights[OutputDimensions * PaddedInputDimensions];
  };

}  // namespace Stockfish::Eval::NNUE::Layers

#endif // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
