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

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SCALAR_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_SCALAR_H_INCLUDED

#if !defined (NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED)
#error "This file can only be included through affine_transform.h"
#endif

#include <iostream>

namespace Stockfish::Eval::NNUE::Layers {

  // Affine transformation layer
  template <typename PreviousLayer, IndexType OutDims>
  class AffineTransform_Scalar : public AffineTransform_Base<PreviousLayer, OutDims> {
   public:
    using BaseType = AffineTransform_Base<PreviousLayer, OutDims>;

    using InputType = typename BaseType::InputType;
    using OutputType = typename BaseType::OutputType;

    static constexpr auto InputDimensions = BaseType::InputDimensions;
    static constexpr auto OutputDimensions = BaseType::OutputDimensions;
    static constexpr auto PaddedInputDimensions = BaseType::PaddedInputDimensions;
    static constexpr auto SelfBufferSize = BaseType::SelfBufferSize;
    static constexpr auto BufferSize = BaseType::BufferSize;

    // Read network parameters
    bool read_parameters(std::istream& stream) {
      if (!BaseType::previousLayer.read_parameters(stream)) return false;
      for (std::size_t i = 0; i < OutputDimensions; ++i)
        biases[i] = read_little_endian<BiasType>(stream);
      for (std::size_t i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
        weights[i] = read_little_endian<WeightType>(stream);
      return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
      if (!BaseType::previousLayer.write_parameters(stream)) return false;
      for (std::size_t i = 0; i < OutputDimensions; ++i)
          write_little_endian<BiasType>(stream, biases[i]);
      for (std::size_t i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
          write_little_endian<WeightType>(stream, weights[i]);
      return !stream.fail();
    }

    // Forward propagation
    const OutputType* propagate(
      const TransformedFeatureType* transformedFeatures,
      char* buffer) const {

      const auto input = BaseType::previousLayer.propagate(
        transformedFeatures, buffer + SelfBufferSize);

      const auto output = reinterpret_cast<OutputType*>(buffer);

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
    using BiasType = typename BaseType::BiasType;
    using WeightType = typename BaseType::WeightType;

    using BaseType::biases;
    using BaseType::weights;
  };

}  // namespace Stockfish::Eval::NNUE::Layers

#endif // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SCALAR_H_INCLUDED
