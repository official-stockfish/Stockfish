/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

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

// Definition of layer AffineTransform of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#include <iostream>
#include "../nnue_common.h"

namespace Eval::NNUE::Layers {

  // Affine transformation layer
  template <typename PreviousLayer, IndexType OutputDimensions>
  class AffineTransform {
   public:
    // Input/output type
    using InputType = typename PreviousLayer::OutputType;
    using OutputType = std::int32_t;
    static_assert(std::is_same<InputType, std::uint8_t>::value, "");

    // Number of input/output dimensions
    static constexpr IndexType kInputDimensions =
        PreviousLayer::kOutputDimensions;
    static constexpr IndexType kOutputDimensions = OutputDimensions;
    static constexpr IndexType kPaddedInputDimensions =
        CeilToMultiple<IndexType>(kInputDimensions, kMaxSimdWidth);

    // Size of forward propagation buffer used in this layer
    static constexpr std::size_t kSelfBufferSize =
        CeilToMultiple(kOutputDimensions * sizeof(OutputType), kCacheLineSize);

    // Size of the forward propagation buffer used from the input layer to this layer
    static constexpr std::size_t kBufferSize =
        PreviousLayer::kBufferSize + kSelfBufferSize;

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t GetHashValue() {
      std::uint32_t hash_value = 0xCC03DAE4u;
      hash_value += kOutputDimensions;
      hash_value ^= PreviousLayer::GetHashValue() >> 1;
      hash_value ^= PreviousLayer::GetHashValue() << 31;
      return hash_value;
    }

   // Read network parameters
    bool ReadParameters(std::istream& stream) {
      if (!previous_layer_.ReadParameters(stream)) return false;
      stream.read(reinterpret_cast<char*>(biases_),
                  kOutputDimensions * sizeof(BiasType));
      stream.read(reinterpret_cast<char*>(weights_),
                  kOutputDimensions * kPaddedInputDimensions *
                  sizeof(WeightType));
      return !stream.fail();
    }

    // Forward propagation
    const OutputType* Propagate(
        const TransformedFeatureType* transformed_features, char* buffer) const {
      const auto input = previous_layer_.Propagate(
          transformed_features, buffer + kSelfBufferSize);
      const auto output = reinterpret_cast<OutputType*>(buffer);

  #if defined(USE_AVX512)
      constexpr IndexType kNumChunks = kPaddedInputDimensions / (kSimdWidth * 2);
      const __m512i kOnes = _mm512_set1_epi16(1);
      const auto input_vector = reinterpret_cast<const __m512i*>(input);

  #elif defined(USE_AVX2)
      constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
      const __m256i kOnes = _mm256_set1_epi16(1);
      const auto input_vector = reinterpret_cast<const __m256i*>(input);

  #elif defined(USE_SSSE3)
      constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
      const __m128i kOnes = _mm_set1_epi16(1);
      const auto input_vector = reinterpret_cast<const __m128i*>(input);

  #elif defined(USE_NEON)
      constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
      const auto input_vector = reinterpret_cast<const int8x8_t*>(input);
  #endif

      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        const IndexType offset = i * kPaddedInputDimensions;

  #if defined(USE_AVX512)
        __m512i sum = _mm512_setzero_si512();
        const auto row = reinterpret_cast<const __m512i*>(&weights_[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {

  #if defined(__MINGW32__) || defined(__MINGW64__)
            __m512i product = _mm512_maddubs_epi16(_mm512_loadu_si512(&input_vector[j]), _mm512_load_si512(&row[j]));
  #else
            __m512i product = _mm512_maddubs_epi16(_mm512_load_si512(&input_vector[j]), _mm512_load_si512(&row[j]));
  #endif

            product = _mm512_madd_epi16(product, kOnes);
            sum = _mm512_add_epi32(sum, product);
        }
        output[i] = _mm512_reduce_add_epi32(sum) + biases_[i];

        // Note: Changing kMaxSimdWidth from 32 to 64 breaks loading existing networks.
        // As a result kPaddedInputDimensions may not be an even multiple of 64(512bit)
        // and we have to do one more 256bit chunk.
        if (kPaddedInputDimensions != kNumChunks * kSimdWidth * 2)
        {
            const auto iv_256  = reinterpret_cast<const __m256i*>(input);
            const auto row_256 = reinterpret_cast<const __m256i*>(&weights_[offset]);
            int j = kNumChunks * 2;

  #if defined(__MINGW32__) || defined(__MINGW64__)  // See HACK comment below in AVX2.
            __m256i sum256 = _mm256_maddubs_epi16(_mm256_loadu_si256(&iv_256[j]), _mm256_load_si256(&row_256[j]));
  #else
            __m256i sum256 = _mm256_maddubs_epi16(_mm256_load_si256(&iv_256[j]), _mm256_load_si256(&row_256[j]));
  #endif

            sum256 = _mm256_madd_epi16(sum256, _mm256_set1_epi16(1));
            sum256 = _mm256_hadd_epi32(sum256, sum256);
            sum256 = _mm256_hadd_epi32(sum256, sum256);
            const __m128i lo = _mm256_extracti128_si256(sum256, 0);
            const __m128i hi = _mm256_extracti128_si256(sum256, 1);
            output[i] += _mm_cvtsi128_si32(lo) + _mm_cvtsi128_si32(hi);
        }

  #elif defined(USE_AVX2)
        __m256i sum = _mm256_setzero_si256();
        const auto row = reinterpret_cast<const __m256i*>(&weights_[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          __m256i product = _mm256_maddubs_epi16(

  #if defined(__MINGW32__) || defined(__MINGW64__)
            // HACK: Use _mm256_loadu_si256() instead of _mm256_load_si256. Because the binary
            //       compiled with g++ in MSYS2 crashes here because the output memory is not aligned
            //       even though alignas is specified.
            _mm256_loadu_si256
  #else
            _mm256_load_si256
  #endif

            (&input_vector[j]), _mm256_load_si256(&row[j]));
          product = _mm256_madd_epi16(product, kOnes);
          sum = _mm256_add_epi32(sum, product);
        }
        sum = _mm256_hadd_epi32(sum, sum);
        sum = _mm256_hadd_epi32(sum, sum);
        const __m128i lo = _mm256_extracti128_si256(sum, 0);
        const __m128i hi = _mm256_extracti128_si256(sum, 1);
        output[i] = _mm_cvtsi128_si32(lo) + _mm_cvtsi128_si32(hi) + biases_[i];

  #elif defined(USE_SSSE3)
        __m128i sum = _mm_cvtsi32_si128(biases_[i]);
        const auto row = reinterpret_cast<const __m128i*>(&weights_[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          __m128i product = _mm_maddubs_epi16(
              _mm_load_si128(&input_vector[j]), _mm_load_si128(&row[j]));
          product = _mm_madd_epi16(product, kOnes);
          sum = _mm_add_epi32(sum, product);
        }
        sum = _mm_hadd_epi32(sum, sum);
        sum = _mm_hadd_epi32(sum, sum);
        output[i] = _mm_cvtsi128_si32(sum);

  #elif defined(USE_NEON)
        int32x4_t sum = {biases_[i]};
        const auto row = reinterpret_cast<const int8x8_t*>(&weights_[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          int16x8_t product = vmull_s8(input_vector[j * 2], row[j * 2]);
          product = vmlal_s8(product, input_vector[j * 2 + 1], row[j * 2 + 1]);
          sum = vpadalq_s16(sum, product);
        }
        output[i] = sum[0] + sum[1] + sum[2] + sum[3];

  #else
        OutputType sum = biases_[i];
        for (IndexType j = 0; j < kInputDimensions; ++j) {
          sum += weights_[offset + j] * input[j];
        }
        output[i] = sum;
  #endif

      }
      return output;
    }

   private:
    using BiasType = OutputType;
    using WeightType = std::int8_t;

    PreviousLayer previous_layer_;

    alignas(kCacheLineSize) BiasType biases_[kOutputDimensions];
    alignas(kCacheLineSize)
        WeightType weights_[kOutputDimensions * kPaddedInputDimensions];
  };

}  // namespace Eval::NNUE::Layers

#endif // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
