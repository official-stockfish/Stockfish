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

// clipped_relu.h contains the definition of ClippedReLU layer.
//
// Following function(s) must be implemented in the architecture-specific
// files:
//
//  ClippedReLU::propagate

#ifndef NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#define NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED

#include <cstdint>
#include <iosfwd>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims>
class ClippedReLU {
   public:
    // Input/output type
    using InputType  = std::int32_t;
    using OutputType = std::uint8_t;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = InputDimensions;
    static_assert(InputDimensions > 0);

    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, DimensionPadding);

    using OutputBuffer = OutputType[PaddedOutputDimensions];

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value(std::uint32_t prevHash) {
        std::uint32_t hashValue = 0x538D24C7u;
        hashValue += prevHash;
        return hashValue;
    }

    // Read network parameters
    bool read_parameters(std::istream&) { return true; }

    // Write network parameters
    bool write_parameters(std::ostream&) const { return true; }

    // Forward propagation
    void propagate(const InputType* input, OutputType* output) const;
};

}  // namespace Stockfish::Eval::NNUE::Layers

#if defined(__i386__) || defined(__amd64__)

    #include "arch/i386/nnue/layers/clipped_relu.h"

#elif defined(__arm__) || defined(__aarch64__)

    #include "arch/arm/nnue/layers/clipped_relu.h"

#else

    #include "arch/generic/nnue/layers/clipped_relu.h"

#endif

#endif  // NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
