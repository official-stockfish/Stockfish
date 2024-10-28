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

// affine_transform.h contains the definition of AffineTransform layer.
//
// Following function(s) must be implemented in the architecture-specific
// files:
//
//  AffineTransform::propagate
//  AffineTransform::get_weight_index
//
// Following class(es) must be defined in the architecture-specific files:
//
//  AffineTransformSparseInput<InDims, OutDims>

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#include <cstdint>
#include <iostream>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims, IndexType OutDims>
class AffineTransform {
   public:
    // Input/output type
    using InputType  = std::uint8_t;
    using OutputType = std::int32_t;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = OutDims;
    static_assert(InputDimensions > 0 && OutputDimensions > 0);

    static constexpr IndexType PaddedInputDimensions =
      ceil_to_multiple<IndexType>(InputDimensions, DimensionPadding);
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, DimensionPadding);

    using OutputBuffer = OutputType[PaddedOutputDimensions];

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value(std::uint32_t prevHash) {
        std::uint32_t hashValue = 0xCC03DAE4u;
        hashValue += OutputDimensions;
        hashValue ^= prevHash >> 1;
        hashValue ^= prevHash << 31;
        return hashValue;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
        read_little_endian<BiasType>(stream, biases, OutputDimensions);
        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            weights[get_weight_index(i)] = read_little_endian<WeightType>(stream);

        return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
        write_little_endian<BiasType>(stream, biases, OutputDimensions);

        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            write_little_endian<WeightType>(stream, weights[get_weight_index(i)]);

        return !stream.fail();
    }

    // Forward propagation
    void propagate(const InputType* input, OutputType* output) const;

   protected:
    using BiasType   = OutputType;
    using WeightType = std::int8_t;

    static constexpr IndexType get_weight_index(IndexType i);

    alignas(CacheLineSize) BiasType biases[OutputDimensions];
    alignas(CacheLineSize) WeightType weights[OutputDimensions * PaddedInputDimensions];
};

}  // namespace Stockfish::Eval::NNUE::Layers

// This macro is used to inherit types and constexpr values from
// AffineTransform class in case implementation details define specialized
// AffineTransformSparseInput class.
#define __DEFINE_BASE_PROPERTIES \
    using Base = AffineTransform<InDims, OutDims>; \
    using Base::biases, Base::weights; \
\
   public: \
    using typename Base::InputType, typename Base::OutputType, typename Base::OutputBuffer; \
    using typename Base::BiasType, typename Base::WeightType; \
    using Base::InputDimensions, Base::OutputDimensions, Base::PaddedInputDimensions, \
      Base::PaddedOutputDimensions;

#if defined(__i386__) || defined(__amd64__)

    #include "arch/i386/nnue/layers/affine_transform.h"
    #include "arch/i386/nnue/layers/affine_transform_sparse_input.h"

#elif defined(__arm__) || defined(__aarch64__)

    #include "arch/arm/nnue/layers/affine_transform.h"
    #include "arch/arm/nnue/layers/affine_transform_sparse_input.h"

#else

    #include "arch/generic/nnue/layers/affine_transform.h"

#endif

#undef __DEFINE_BASE_PROPERTIES

#endif  // NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
