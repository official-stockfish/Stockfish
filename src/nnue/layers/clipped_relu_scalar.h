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

#ifndef NNUE_LAYERS_CLIPPED_RELU_SCALAR_H_INCLUDED
#define NNUE_LAYERS_CLIPPED_RELU_SCALAR_H_INCLUDED

#if !defined (NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED)
#error "This file can only be included through clipped_relu.h"
#endif

namespace Stockfish::Eval::NNUE::Layers {

  // Clipped ReLU
  template <typename PreviousLayer>
  class ClippedReLU_Scalar : public ClippedReLU_Base<PreviousLayer> {
   public:
    using BaseType = ClippedReLU_Base<PreviousLayer>;

    using InputType = typename BaseType::InputType;
    using OutputType = typename BaseType::OutputType;

    static constexpr auto InputDimensions = BaseType::InputDimensions;
    static constexpr auto OutputDimensions = BaseType::OutputDimensions;
    static constexpr auto SelfBufferSize = BaseType::SelfBufferSize;
    static constexpr auto BufferSize = BaseType::BufferSize;

    // Forward propagation
    const OutputType* propagate(
      const TransformedFeatureType* transformedFeatures,
      char* buffer) const {

      const auto input = BaseType::previousLayer.propagate(
        transformedFeatures, buffer + SelfBufferSize);
      const auto output = reinterpret_cast<OutputType*>(buffer);

      for (IndexType i = 0; i < InputDimensions; ++i) {
        int x = input[i] >> WeightScaleBits;
        if (x < 0)   x = 0;
        if (x > 127) x = 127;
        output[i] = static_cast<OutputType>(x);
      }

      return output;
    }
  };

}  // namespace Stockfish::Eval::NNUE::Layers

#endif // NNUE_LAYERS_CLIPPED_RELU_SCALAR_H_INCLUDED
