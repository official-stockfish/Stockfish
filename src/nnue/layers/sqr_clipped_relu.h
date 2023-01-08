/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

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

#ifndef NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
#define NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED

#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

  // Clipped ReLU
  template <IndexType InDims>
  class SqrClippedReLU {
   public:
    // Input/output type
    using InputType = std::int32_t;
    using OutputType = std::uint8_t;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions = InDims;
    static constexpr IndexType OutputDimensions = InputDimensions;
    static constexpr IndexType PaddedOutputDimensions =
        ceil_to_multiple<IndexType>(OutputDimensions, 32);

    using OutputBuffer = OutputType[PaddedOutputDimensions];

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value(std::uint32_t prevHash) {
      std::uint32_t hashValue = 0x538D24C7u;
      hashValue += prevHash;
      return hashValue;
    }

    // Read network parameters
    bool read_parameters(std::istream&) {
      return true;
    }

    // Write network parameters
    bool write_parameters(std::ostream&) const {
      return true;
    }

    // Forward propagation
    const OutputType* propagate(
        const InputType* input, OutputType* output) const {

  #if defined(USE_SSE2)
      constexpr IndexType NumChunks = InputDimensions / 16;

  #ifdef USE_SSE41
      const __m128i Zero = _mm_setzero_si128();
  #else
      const __m128i k0x80s = _mm_set1_epi8(-128);
  #endif

      static_assert(WeightScaleBits == 6);
      const auto in = reinterpret_cast<const __m128i*>(input);
      const auto out = reinterpret_cast<__m128i*>(output);
      for (IndexType i = 0; i < NumChunks; ++i) {
        __m128i words0 = _mm_packs_epi32(
            _mm_load_si128(&in[i * 4 + 0]),
            _mm_load_si128(&in[i * 4 + 1]));
        __m128i words1 = _mm_packs_epi32(
            _mm_load_si128(&in[i * 4 + 2]),
            _mm_load_si128(&in[i * 4 + 3]));

        // Not sure if
        words0 = _mm_srli_epi16(_mm_mulhi_epi16(words0, words0), 3);
        words1 = _mm_srli_epi16(_mm_mulhi_epi16(words1, words1), 3);

        const __m128i packedbytes = _mm_packs_epi16(words0, words1);

        _mm_store_si128(&out[i],

  #ifdef USE_SSE41
          _mm_max_epi8(packedbytes, Zero)
  #else
          _mm_subs_epi8(_mm_adds_epi8(packedbytes, k0x80s), k0x80s)
  #endif

        );
      }
      constexpr IndexType Start = NumChunks * 16;

  #else
      constexpr IndexType Start = 0;
  #endif

      for (IndexType i = Start; i < InputDimensions; ++i) {
        output[i] = static_cast<OutputType>(
            // really should be /127 but we need to make it fast
            // needs to be accounted for in the trainer
            std::max(0ll, std::min(127ll, (((long long)input[i] * input[i]) >> (2 * WeightScaleBits)) / 128)));
      }

      return output;
    }
  };

}  // namespace Stockfish::Eval::NNUE::Layers

#endif // NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
