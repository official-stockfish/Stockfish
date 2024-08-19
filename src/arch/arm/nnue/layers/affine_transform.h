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

#ifndef ARM_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define ARM_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

// Check ARM/AArch64 SIMD features.
// If none is defined, fall back to the generic implementation.
#ifndef __ARM_NEON

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
        static constexpr IndexType OutputLanes = 16 / sizeof(OutputType);
        static_assert(OutputDimensions % OutputLanes == 0);

        static constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(InputDimensions, 8) / 4;
        static constexpr IndexType NumRegs   = OutputDimensions / OutputLanes;

        int32x4_t acc[NumRegs];

        for (std::size_t k = 0; k < array_size(acc); ++k)
            acc[k] = reinterpret_cast<const int32x4_t*>(biases)[k];

        for (IndexType i = 0; i < NumChunks; ++i)
        {
            const int8x16_t in =
              vreinterpretq_s8_s32(vdupq_n_s32(reinterpret_cast<const std::int32_t*>(input)[i]));
            const auto col = reinterpret_cast<const int8x16_t*>(&weights[i * OutputDimensions * 4]);

            for (std::size_t k = 0; k < array_size(acc); ++k)
                vdotq_s32_v(acc[k], in, col[k]);
        }

        for (std::size_t k = 0; k < array_size(acc); ++k)
            reinterpret_cast<int32x4_t*>(output)[k] = acc[k];
    }
    else if constexpr (OutputDimensions == 1)
    {
        static constexpr IndexType InputLanes = 16 / sizeof(InputType);
        static_assert(PaddedInputDimensions % InputLanes == 0);

        static constexpr IndexType NumChunks = PaddedInputDimensions / InputLanes;

        int32x4_t sum = vdupq_n_s32(0);

        for (IndexType j = 0; j < NumChunks; ++j)
        {
            const int8x16_t in  = reinterpret_cast<const int8x16_t*>(input)[j];
            const int8x16_t row = reinterpret_cast<const int8x16_t*>(weights)[j];
            vdotq_s32_v(sum, in, row);
        }

#if __ARM_ARCH >= 8
        output[0] = vaddvq_s32(sum) + biases[0];
#else
        output[0] = vgetq_lane_s32(sum, 0) + vgetq_lane_s32(sum, 1) + vgetq_lane_s32(sum, 2)
                  + vgetq_lane_s32(sum, 3) + biases[0];
#endif
    }
}

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // !__ARM_NEON

#endif  // ARM_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
