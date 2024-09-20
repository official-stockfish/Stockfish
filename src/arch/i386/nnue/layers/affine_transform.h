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

#ifndef I386_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define I386_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

// Check x86/AMD64 SIMD extensions.
// If none is defined, fall back to the generic implementation.
#ifndef __SSE2__

#include "arch/generic/nnue/layers/affine_transform.h"

#else

#include "../../arch.h"

#include <algorithm>
#include <cstdint>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims, IndexType OutDims>
constexpr IndexType AffineTransform<InDims, OutDims>::get_weight_index(IndexType i) {
    return (i / 4) % (PaddedInputDimensions / 4) * OutputDimensions * 4
         + i / PaddedInputDimensions * 4 + i % 4;
}

template<IndexType InDims, IndexType OutDims>
void AffineTransform<InDims, OutDims>::propagate(const InputType* input, OutputType* output) const {
    if constexpr (OutputDimensions > 1)
    {
#if defined(__AVX512F__) && (defined(__AVX512BW__) || defined(__AVX512VNNI__)) \
  && !defined(NO_AVX512)
        using vec_t = __m512i;
#elif defined(__AVX2__)
        using vec_t = __m256i;
#else
        using vec_t = __m128i;
#endif

        static constexpr IndexType OutputLanes = sizeof(vec_t) / sizeof(OutputType);
        static_assert(OutputDimensions % OutputLanes == 0);

        static constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(InputDimensions, 8) / 4;
        static constexpr IndexType NumRegs   = OutputDimensions / OutputLanes;

        vec_t acc[NumRegs];

        for (std::size_t k = 0; k < array_size(acc); ++k)
            acc[k] = reinterpret_cast<const vec_t*>(biases)[k];

        for (IndexType i = 0; i < NumChunks; ++i)
        {
            const vec_t in =
              _mm_set1_epi32_v<vec_t>(reinterpret_cast<const std::uint32_t*>(input)[i]);
            const auto col = reinterpret_cast<const vec_t*>(&weights[i * OutputDimensions * 4]);

            for (std::size_t k = 0; k < array_size(acc); ++k)
                _mm_dpbusd_epi32_v(acc[k], in, col[k]);
        }

        for (std::size_t k = 0; k < array_size(acc); ++k)
            reinterpret_cast<vec_t*>(output)[k] = acc[k];
    }
    else if constexpr (OutputDimensions == 1)
    {
        // We cannot use AVX512 for the last layer because there are only
        // 32 inputs and the buffer is not padded to 64 elements.
#if defined(__AVX2__)
        using vec_t = __m256i;
#else
        using vec_t = __m128i;
#endif

        static constexpr IndexType InputLanes = sizeof(vec_t) / sizeof(InputType);
        static_assert(PaddedInputDimensions % InputLanes == 0);

        static constexpr IndexType NumChunks = PaddedInputDimensions / InputLanes;

        vec_t sum = _mm_setzero_v<vec_t>();

        for (IndexType j = 0; j < NumChunks; ++j)
        {
            const vec_t in  = reinterpret_cast<const vec_t*>(input)[j];
            const vec_t row = reinterpret_cast<const vec_t*>(weights)[j];
            _mm_dpbusd_epi32_v(sum, in, row);
        }

        output[0] = _mm_reduce_add_epi32_v(sum) + biases[0];
    }
}

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // !__SSE2__

#endif  // I386_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
