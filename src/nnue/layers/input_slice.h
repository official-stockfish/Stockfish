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

// NNUE evaluation function layer InputSlice definition

#ifndef NNUE_LAYERS_INPUT_SLICE_H_INCLUDED
#define NNUE_LAYERS_INPUT_SLICE_H_INCLUDED

#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

// Input layer
template <IndexType OutDims, IndexType Offset = 0>
class InputSlice {
 public:
  // Need to maintain alignment
  static_assert(Offset % MaxSimdWidth == 0, "");

  // Output type
  using OutputType = TransformedFeatureType;

  // Output dimensionality
  static constexpr IndexType OutputDimensions = OutDims;

  // Size of forward propagation buffer used from the input layer to this layer
  static constexpr std::size_t BufferSize = 0;

  // Hash value embedded in the evaluation file
  static constexpr std::uint32_t get_hash_value() {
    std::uint32_t hashValue = 0xEC42E90Du;
    hashValue ^= OutputDimensions ^ (Offset << 10);
    return hashValue;
  }

  // Read network parameters
  bool read_parameters(std::istream& /*stream*/) {
    return true;
  }

  // Read network parameters
  bool write_parameters(std::ostream& /*stream*/) const {
    return true;
  }

  // Forward propagation
  const OutputType* propagate(
      const TransformedFeatureType* transformedFeatures,
      char* /*buffer*/) const {
    return transformedFeatures + Offset;
  }

 private:
};

}  // namespace Stockfish::Eval::NNUE::Layers

#endif // #ifndef NNUE_LAYERS_INPUT_SLICE_H_INCLUDED
