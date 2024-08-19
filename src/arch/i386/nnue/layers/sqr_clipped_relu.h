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

#ifndef I386_NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
#define I386_NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED

#ifndef NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

// Check x86/AMD64 SIMD extensions.
// If none is defined, fall back to the generic implementation.
#ifndef __SSE2__

#include "arch/generic/nnue/layers/sqr_clipped_relu.h"

#else

#include "../../arch.h"

#include <cstdint>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims>
void SqrClippedReLU<InDims>::propagate(const InputType* input, OutputType* output) const {
    static constexpr IndexType NumChunks = PaddedOutputDimensions / 16;

    const auto in  = reinterpret_cast<const __m128i*>(input);
    const auto out = reinterpret_cast<__m128i*>(output);

    for (IndexType i = 0; i < NumChunks; ++i)
    {
        __m128i words0 = _mm_packs_epi32(in[i * 4 + 0], in[i * 4 + 1]);
        __m128i words1 = _mm_packs_epi32(in[i * 4 + 2], in[i * 4 + 3]);

        // We shift by WeightScaleBits * 2 = 12 and divide by 128 which is
        // an additional shift-right of 7, meaning 19 in total. Mulhi
        // strips the lower 16 bits so we need to shift by 3 more.
        words0 = _mm_srli_epi16(_mm_mulhi_epi16(words0, words0), 3);
        words1 = _mm_srli_epi16(_mm_mulhi_epi16(words1, words1), 3);

        out[i] = _mm_packs_epi16(words0, words1);
    }
}

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // !__SSE2__

#endif  // I386_NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
