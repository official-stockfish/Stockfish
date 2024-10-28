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

#ifndef ARM_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#define ARM_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED

#ifndef NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

// Check ARM/AArch64 SIMD features.
// If none is defined, fall back to the generic implementation.
#ifndef __ARM_NEON

#include "arch/generic/nnue/layers/clipped_relu.h"

#else

#include "../../arch.h"

#include <cstdint>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims>
void ClippedReLU<InDims>::propagate(const InputType* input, OutputType* output) const {
    static constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(OutputDimensions, 16) / 8;

    const auto in  = reinterpret_cast<const int32x4_t*>(input);
    const auto out = reinterpret_cast<int8x8_t*>(output);

    for (IndexType i = 0; i < NumChunks; ++i)
    {
#if __ARM_ARCH >= 8
        int16x4_t words0 = vqshrn_n_s32(in[i * 2], WeightScaleBits);
        int16x8_t words  = vqshrn_high_n_s32(words0, in[i * 2 + 1], WeightScaleBits);
        out[i]           = vmax_s8(vqmovn_s16(words), vdup_n_s8(0));
#else
        union {
            int16x4x2_t tuple;
            int16x8_t   all;
        } words;

        words.tuple.val[0] = vqshrn_n_s32(in[i * 2 + 0], WeightScaleBits);
        words.tuple.val[1] = vqshrn_n_s32(in[i * 2 + 1], WeightScaleBits);
        out[i]             = vmax_s8(vqmovn_s16(words.all), vdup_n_s8(0));
#endif
    }
}

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // !__ARM_NEON

#endif  // ARM_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
