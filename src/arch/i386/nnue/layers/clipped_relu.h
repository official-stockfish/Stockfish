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

#ifndef I386_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#define I386_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED

#ifndef NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

// Check x86/AMD64 SIMD extensions.
// If none is defined, fall back to the generic implementation.
#ifndef __SSE2__

#include "arch/generic/nnue/layers/clipped_relu.h"

#else

#include "../../arch.h"

#include <cstdint>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims>
void ClippedReLU<InDims>::propagate(const InputType* input, OutputType* output) const {
    static_assert(PaddedOutputDimensions % 32 == 0);

    // Do not use 256-bit registers on AVX as it does not have shift
    // instructions, instead fall back to SSE4.1.
#ifdef __AVX2__

#if defined(__AVX512F__) && defined(__AVX512BW__) && !defined(NO_AVX512)
    if constexpr (PaddedOutputDimensions >= 64)
    {
        static constexpr IndexType NumChunks = PaddedOutputDimensions / 64;

        static const __m512i permuteTable512 =
          _mm512_set_epi32(15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1, 12, 8, 4, 0);

        const auto in  = reinterpret_cast<const __m512i*>(input);
        const auto out = reinterpret_cast<__m512i*>(output);

        for (IndexType i = 0; i < NumChunks; ++i)
        {
            const __m512i words0 =
              _mm512_srli_epi16(_mm512_packus_epi32(in[i * 4 + 0], in[i * 4 + 1]), WeightScaleBits);
            const __m512i words1 =
              _mm512_srli_epi16(_mm512_packus_epi32(in[i * 4 + 2], in[i * 4 + 3]), WeightScaleBits);

            out[i] = _mm512_permutexvar_epi32(permuteTable512, _mm512_packs_epi16(words0, words1));
        }
    }
    constexpr IndexType Start = PaddedOutputDimensions / 64 * 64;
#else
    constexpr IndexType Start = 0;
#endif

    if constexpr (Start != PaddedOutputDimensions)
    {
        static constexpr IndexType NumChunks = PaddedOutputDimensions / 32;

        static const __m256i permuteTable256 = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);

        const auto in  = reinterpret_cast<const __m256i*>(input);
        const auto out = reinterpret_cast<__m256i*>(output);

        for (IndexType i = Start / 32; i < NumChunks; ++i)
        {
            const __m256i words0 =
              _mm256_srli_epi16(_mm256_packus_epi32(in[i * 4 + 0], in[i * 4 + 1]), WeightScaleBits);
            const __m256i words1 =
              _mm256_srli_epi16(_mm256_packus_epi32(in[i * 4 + 2], in[i * 4 + 3]), WeightScaleBits);

            out[i] =
              _mm256_permutevar8x32_epi32(_mm256_packs_epi16(words0, words1), permuteTable256);
        }
    }

#else  // __SSE2__

    static constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(OutputDimensions, 16) / 16;

    const auto in  = reinterpret_cast<const __m128i*>(input);
    const auto out = reinterpret_cast<__m128i*>(output);

    for (IndexType i = 0; i < NumChunks; ++i)
    {
#ifdef __SSE4_1__
        const __m128i words0 =
          _mm_srli_epi16(_mm_packus_epi32(in[i * 4 + 0], in[i * 4 + 1]), WeightScaleBits);
        const __m128i words1 =
          _mm_srli_epi16(_mm_packus_epi32(in[i * 4 + 2], in[i * 4 + 3]), WeightScaleBits);

        out[i] = _mm_packs_epi16(words0, words1);
#else
        static const __m128i s8min = _mm_set1_epi8(-0x80);

        const __m128i words0 =
          _mm_srai_epi16(_mm_packs_epi32(in[i * 4 + 0], in[i * 4 + 1]), WeightScaleBits);
        const __m128i words1 =
          _mm_srai_epi16(_mm_packs_epi32(in[i * 4 + 2], in[i * 4 + 3]), WeightScaleBits);
        const __m128i bytes = _mm_packs_epi16(words0, words1);

        out[i] = _mm_subs_epi8(_mm_adds_epi8(bytes, s8min), s8min);
#endif
    }

#endif
}

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // !__SSE2__

#endif  // I386_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
