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

#ifndef NNUE_LAYERS_CLIPPED_RELU_VEC_H_INCLUDED
#define NNUE_LAYERS_CLIPPED_RELU_VEC_H_INCLUDED

#if !defined (NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED)
#error "This file can only be included through clipped_relu.h"
#endif

#if defined (USE_MMX) || defined (USE_SSE2) || defined (USE_NEON)

namespace Stockfish::Eval::NNUE::Layers {

  // Clipped ReLU
  template <typename PreviousLayer>
  class ClippedReLU_Vec : public ClippedReLU_Base<PreviousLayer> {
   public:
    using BaseType = ClippedReLU_Base<PreviousLayer>;

    using InputType = typename BaseType::InputType;
    using OutputType = typename BaseType::OutputType;

    static constexpr auto InputDimensions = BaseType::InputDimensions;
    static constexpr auto OutputDimensions = BaseType::OutputDimensions;
    static constexpr auto SelfBufferSize = BaseType::SelfBufferSize;
    static constexpr auto BufferSize = BaseType::BufferSize;

  // SIMD width (in bytes)
#if defined(USE_AVX2)
    static constexpr std::size_t SimdWidth = 32;
#elif defined(USE_SSE2)
    static constexpr std::size_t SimdWidth = 16;
#elif defined(USE_MMX)
    static constexpr std::size_t SimdWidth = 8;
#elif defined(USE_NEON)
    static constexpr std::size_t SimdWidth = 16;
#endif

    // Forward propagation
    const OutputType* propagate(
      const TransformedFeatureType* transformedFeatures,
      char* buffer) const {

      const auto input = BaseType::previousLayer.propagate(
        transformedFeatures, buffer + SelfBufferSize);
      const auto output = reinterpret_cast<OutputType*>(buffer);

#if defined(USE_AVX2)

      if constexpr (InputDimensions % SimdWidth == 0) {
        constexpr IndexType NumChunks = InputDimensions / SimdWidth;
        const __m256i Zero = _mm256_setzero_si256();
        const __m256i Offsets = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
        const auto in = reinterpret_cast<const __m256i*>(input);
        const auto out = reinterpret_cast<__m256i*>(output);
        for (IndexType i = 0; i < NumChunks; ++i) {
          const __m256i words0 = _mm256_srai_epi16(_mm256_packs_epi32(
            _mm256_load_si256(&in[i * 4 + 0]),
            _mm256_load_si256(&in[i * 4 + 1])), WeightScaleBits);
          const __m256i words1 = _mm256_srai_epi16(_mm256_packs_epi32(
            _mm256_load_si256(&in[i * 4 + 2]),
            _mm256_load_si256(&in[i * 4 + 3])), WeightScaleBits);
          _mm256_store_si256(&out[i], _mm256_permutevar8x32_epi32(_mm256_max_epi8(
            _mm256_packs_epi16(words0, words1), Zero), Offsets));
        }
      } else {
        constexpr IndexType NumChunks = InputDimensions / (SimdWidth / 2);
        const __m128i Zero = _mm_setzero_si128();
        const auto in = reinterpret_cast<const __m128i*>(input);
        const auto out = reinterpret_cast<__m128i*>(output);
        for (IndexType i = 0; i < NumChunks; ++i) {
          const __m128i words0 = _mm_srai_epi16(_mm_packs_epi32(
              _mm_load_si128(&in[i * 4 + 0]),
              _mm_load_si128(&in[i * 4 + 1])), WeightScaleBits);
          const __m128i words1 = _mm_srai_epi16(_mm_packs_epi32(
              _mm_load_si128(&in[i * 4 + 2]),
              _mm_load_si128(&in[i * 4 + 3])), WeightScaleBits);
          const __m128i packedbytes = _mm_packs_epi16(words0, words1);
          _mm_store_si128(&out[i], _mm_max_epi8(packedbytes, Zero));
        }
      }

      constexpr IndexType Start =
        InputDimensions % SimdWidth == 0
        ? InputDimensions / SimdWidth * SimdWidth
        : InputDimensions / (SimdWidth / 2) * (SimdWidth / 2);

#elif defined(USE_SSE2)

      constexpr IndexType NumChunks = InputDimensions / SimdWidth;

# ifdef USE_SSE41
      const __m128i Zero = _mm_setzero_si128();
# else
      const __m128i k0x80s = _mm_set1_epi8(-128);
# endif

      const auto in = reinterpret_cast<const __m128i*>(input);
      const auto out = reinterpret_cast<__m128i*>(output);
      for (IndexType i = 0; i < NumChunks; ++i) {
        const __m128i words0 = _mm_srai_epi16(_mm_packs_epi32(
          _mm_load_si128(&in[i * 4 + 0]),
          _mm_load_si128(&in[i * 4 + 1])), WeightScaleBits);
        const __m128i words1 = _mm_srai_epi16(_mm_packs_epi32(
          _mm_load_si128(&in[i * 4 + 2]),
          _mm_load_si128(&in[i * 4 + 3])), WeightScaleBits);
        const __m128i packedbytes = _mm_packs_epi16(words0, words1);
        _mm_store_si128(&out[i],

# ifdef USE_SSE41
          _mm_max_epi8(packedbytes, Zero)
# else
          _mm_subs_epi8(_mm_adds_epi8(packedbytes, k0x80s), k0x80s)
# endif

        );
      }

      constexpr IndexType Start = NumChunks * SimdWidth;

#elif defined(USE_MMX)

      constexpr IndexType NumChunks = InputDimensions / SimdWidth;
      const __m64 k0x80s = _mm_set1_pi8(-128);
      const auto in = reinterpret_cast<const __m64*>(input);
      const auto out = reinterpret_cast<__m64*>(output);
      for (IndexType i = 0; i < NumChunks; ++i) {
        const __m64 words0 = _mm_srai_pi16(
            _mm_packs_pi32(in[i * 4 + 0], in[i * 4 + 1]),
            WeightScaleBits);
        const __m64 words1 = _mm_srai_pi16(
            _mm_packs_pi32(in[i * 4 + 2], in[i * 4 + 3]),
            WeightScaleBits);
        const __m64 packedbytes = _mm_packs_pi16(words0, words1);
        out[i] = _mm_subs_pi8(_mm_adds_pi8(packedbytes, k0x80s), k0x80s);
      }
      _mm_empty();
      constexpr IndexType Start = NumChunks * SimdWidth;

#elif defined(USE_NEON)

      constexpr IndexType NumChunks = InputDimensions / (SimdWidth / 2);
      const int8x8_t Zero = {0};
      const auto in = reinterpret_cast<const int32x4_t*>(input);
      const auto out = reinterpret_cast<int8x8_t*>(output);
      for (IndexType i = 0; i < NumChunks; ++i) {
        int16x8_t shifted;
        const auto pack = reinterpret_cast<int16x4_t*>(&shifted);
        pack[0] = vqshrn_n_s32(in[i * 2 + 0], WeightScaleBits);
        pack[1] = vqshrn_n_s32(in[i * 2 + 1], WeightScaleBits);
        out[i] = vmax_s8(vqmovn_s16(shifted), Zero);
      }
      constexpr IndexType Start = NumChunks * (SimdWidth / 2);
#else

# error "No vectorization possible but vectorization path entered."

#endif

      for (IndexType i = Start; i < InputDimensions; ++i) {
        int x = input[i] >> WeightScaleBits;
        if (x < 0)   x = 0;
        if (x > 127) x = 127;
        output[i] = static_cast<OutputType>(x);
      }

      return output;
    }
  };

}  // namespace Stockfish::Eval::NNUE::Layers

#else

#define CLIPPED_RELU_NO_VEC

#endif

#endif // NNUE_LAYERS_CLIPPED_RELU_VEC_H_INCLUDED
