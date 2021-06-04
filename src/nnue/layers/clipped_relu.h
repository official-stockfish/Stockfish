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

// Definition of layer ClippedReLU of NNUE evaluation function

#ifndef NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#define NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED

#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

  // Clipped ReLU
  template <typename PreviousLayer>
  class ClippedReLU {
   public:
    // Input/output type
    using InputType = typename PreviousLayer::OutputType;
    using OutputType = std::int8_t;
    static_assert(std::is_same<InputType, std::int32_t>::value, "");

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions =
        PreviousLayer::OutputDimensions;
    static constexpr IndexType OutputDimensions = InputDimensions;

    // Size of forward propagation buffer used in this layer
    static constexpr std::size_t SelfBufferSize =
        ceil_to_multiple(OutputDimensions * sizeof(OutputType), CacheLineSize);

    // Size of the forward propagation buffer used from the input layer to this layer
    static constexpr std::size_t BufferSize =
        PreviousLayer::BufferSize + SelfBufferSize;

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() {
      std::uint32_t hashValue = 0x538D24C7u;
      hashValue += PreviousLayer::get_hash_value();
      return hashValue;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
      return previousLayer.read_parameters(stream);
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
      return previousLayer.write_parameters(stream);
    }

    // Forward propagation
    const OutputType* propagate(const TransformedFeatureType* features, char* buffer) const {
    
      const auto input  = previousLayer.propagate(features, buffer + SelfBufferSize);
      const auto output = reinterpret_cast<OutputType*>(buffer);

      // We implement a clipped ReLu of the input, keeping the output in the 0..127 range
      for (IndexType i = 0; i < InputDimensions; ++i)
      {
          int x = (input[i] >> WeightScaleBits);
          output[i] = std::clamp(x, 0, 127);
      }

      return output;
    }

   private:
    PreviousLayer previousLayer;
  };

}  // namespace Stockfish::Eval::NNUE::Layers

#endif // NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
