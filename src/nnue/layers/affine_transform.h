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

// Definition of layer AffineTransform of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#include <iostream>
#include "../nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

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
#if defined (USE_AVX512)
    static constexpr const IndexType kOutputSimdWidth = kSimdWidth / 2;
#elif defined (USE_SSSE3)
    static constexpr const IndexType kOutputSimdWidth = kSimdWidth / 4;
#endif

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
      for (std::size_t i = 0; i < kOutputDimensions; ++i)
        biases_[i] = read_little_endian<BiasType>(stream);
      for (std::size_t i = 0; i < kOutputDimensions * kPaddedInputDimensions; ++i)
#if !defined (USE_SSSE3)
        weights_[i] = read_little_endian<WeightType>(stream);
#else
        weights_[
          (i / 4) % (kPaddedInputDimensions / 4) * kOutputDimensions * 4 +
          i / kPaddedInputDimensions * 4 +
          i % 4
        ] = read_little_endian<WeightType>(stream);

      // Determine if eights of weight and input products can be summed using 16bits
      // without saturation. We assume worst case combinations of 0 and 127 for all inputs.
      if (kOutputDimensions > 1 && !stream.fail())
      {
          canSaturate16.count = 0;
#if !defined(USE_VNNI)
          for (IndexType i = 0; i < kPaddedInputDimensions; i += 16)
              for (IndexType j = 0; j < kOutputDimensions; ++j)
                  for (int x = 0; x < 2; ++x)
                  {
                      WeightType* w = &weights_[i * kOutputDimensions + j * 4 + x * 2];
                      int sum[2] = {0, 0};
                      for (int k = 0; k < 8; ++k)
                      {
                          IndexType idx = k / 2 * kOutputDimensions * 4 + k % 2;
                          sum[w[idx] < 0] += w[idx];
                      }
                      for (int sign : {-1, 1})
                          while (sign * sum[sign == -1] > 258)
                          {
                              int maxK = 0, maxW = 0;
                              for (int k = 0; k < 8; ++k)
                              {
                                  IndexType idx = k / 2 * kOutputDimensions * 4 + k % 2;
                                  if (maxW < sign * w[idx])
                                      maxK = k, maxW = sign * w[idx];
                              }

                              IndexType idx = maxK / 2 * kOutputDimensions * 4 + maxK % 2;
                              sum[sign == -1] -= w[idx];
                              canSaturate16.add(j, i + maxK / 2 * 4 + maxK % 2 + x * 2, w[idx]);
                              w[idx] = 0;
                          }
                  }

          // Non functional optimization for faster more linear access
          std::sort(canSaturate16.ids, canSaturate16.ids + canSaturate16.count,
                    [](const typename CanSaturate::Entry& e1, const typename CanSaturate::Entry& e2)
                    { return e1.in == e2.in ? e1.out < e2.out : e1.in < e2.in; });
#endif
      }
#endif

      return !stream.fail();
    }

    // Forward propagation
    const OutputType* Propagate(
        const TransformedFeatureType* transformed_features, char* buffer) const {
      const auto input = previous_layer_.Propagate(
          transformed_features, buffer + kSelfBufferSize);

#if defined (USE_AVX512)

      [[maybe_unused]] const __m512i kOnes512 = _mm512_set1_epi16(1);

      [[maybe_unused]] auto m512_hadd = [](__m512i sum, int bias) -> int {
        return _mm512_reduce_add_epi32(sum) + bias;
      };

      [[maybe_unused]] auto m512_add_dpbusd_epi32 = [=](__m512i& acc, __m512i a, __m512i b) {
#if defined (USE_VNNI)
        acc = _mm512_dpbusd_epi32(acc, a, b);
#else
        __m512i product0 = _mm512_maddubs_epi16(a, b);
        product0 = _mm512_madd_epi16(product0, kOnes512);
        acc = _mm512_add_epi32(acc, product0);
#endif
      };

      [[maybe_unused]] auto m512_add_dpbusd_epi32x4 = [=](__m512i& acc, __m512i a0, __m512i b0, __m512i a1, __m512i b1,
                                                                        __m512i a2, __m512i b2, __m512i a3, __m512i b3) {
#if defined (USE_VNNI)
        acc = _mm512_dpbusd_epi32(acc, a0, b0);
        acc = _mm512_dpbusd_epi32(acc, a1, b1);
        acc = _mm512_dpbusd_epi32(acc, a2, b2);
        acc = _mm512_dpbusd_epi32(acc, a3, b3);
#else
        __m512i product0 = _mm512_maddubs_epi16(a0, b0);
        __m512i product1 = _mm512_maddubs_epi16(a1, b1);
        __m512i product2 = _mm512_maddubs_epi16(a2, b2);
        __m512i product3 = _mm512_maddubs_epi16(a3, b3);
        product0 = _mm512_add_epi16(product0, product1);
        product2 = _mm512_add_epi16(product2, product3);
        product0 = _mm512_add_epi16(product0, product2);
        product0 = _mm512_madd_epi16(product0, kOnes512);
        acc = _mm512_add_epi32(acc, product0);
#endif
      };

#endif
#if defined (USE_AVX2)

      [[maybe_unused]] const __m256i kOnes256 = _mm256_set1_epi16(1);

      [[maybe_unused]] auto m256_hadd = [](__m256i sum, int bias) -> int {
        __m128i sum128 = _mm_add_epi32(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1));
        sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_BADC));
        sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_CDAB));
        return _mm_cvtsi128_si32(sum128) + bias;
      };

      [[maybe_unused]] auto m256_add_dpbusd_epi32 = [=](__m256i& acc, __m256i a, __m256i b) {
#if defined (USE_VNNI)
        acc = _mm256_dpbusd_epi32(acc, a, b);
#else
        __m256i product0 = _mm256_maddubs_epi16(a, b);
        product0 = _mm256_madd_epi16(product0, kOnes256);
        acc = _mm256_add_epi32(acc, product0);
#endif
      };

      [[maybe_unused]] auto m256_add_dpbusd_epi32x4 = [=](__m256i& acc, __m256i a0, __m256i b0, __m256i a1, __m256i b1,
                                                                        __m256i a2, __m256i b2, __m256i a3, __m256i b3) {
#if defined (USE_VNNI)
        acc = _mm256_dpbusd_epi32(acc, a0, b0);
        acc = _mm256_dpbusd_epi32(acc, a1, b1);
        acc = _mm256_dpbusd_epi32(acc, a2, b2);
        acc = _mm256_dpbusd_epi32(acc, a3, b3);
#else
        __m256i product0 = _mm256_maddubs_epi16(a0, b0);
        __m256i product1 = _mm256_maddubs_epi16(a1, b1);
        __m256i product2 = _mm256_maddubs_epi16(a2, b2);
        __m256i product3 = _mm256_maddubs_epi16(a3, b3);
        product0 = _mm256_add_epi16(product0, product1);
        product2 = _mm256_add_epi16(product2, product3);
        product0 = _mm256_add_epi16(product0, product2);
        product0 = _mm256_madd_epi16(product0, kOnes256);
        acc = _mm256_add_epi32(acc, product0);
#endif
      };

#endif
#if defined (USE_SSSE3)

      [[maybe_unused]] const __m128i kOnes128 = _mm_set1_epi16(1);

      [[maybe_unused]] auto m128_hadd = [](__m128i sum, int bias) -> int {
        sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0x4E)); //_MM_PERM_BADC
        sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0xB1)); //_MM_PERM_CDAB
        return _mm_cvtsi128_si32(sum) + bias;
      };

      [[maybe_unused]] auto m128_add_dpbusd_epi32 = [=](__m128i& acc, __m128i a, __m128i b) {
        __m128i product0 = _mm_maddubs_epi16(a, b);
        product0 = _mm_madd_epi16(product0, kOnes128);
        acc = _mm_add_epi32(acc, product0);
      };

      [[maybe_unused]] auto m128_add_dpbusd_epi32x4 = [=](__m128i& acc, __m128i a0, __m128i b0, __m128i a1, __m128i b1,
                                                                        __m128i a2, __m128i b2, __m128i a3, __m128i b3) {
        __m128i product0 = _mm_maddubs_epi16(a0, b0);
        __m128i product1 = _mm_maddubs_epi16(a1, b1);
        __m128i product2 = _mm_maddubs_epi16(a2, b2);
        __m128i product3 = _mm_maddubs_epi16(a3, b3);
        product0 = _mm_adds_epi16(product0, product1);
        product2 = _mm_adds_epi16(product2, product3);
        product0 = _mm_adds_epi16(product0, product2);
        product0 = _mm_madd_epi16(product0, kOnes128);
        acc = _mm_add_epi32(acc, product0);
      };

#endif

#if defined (USE_AVX512)
      using vec_t = __m512i;
      #define vec_setzero _mm512_setzero_si512
      #define vec_set_32 _mm512_set1_epi32
      auto& vec_add_dpbusd_32 = m512_add_dpbusd_epi32;
      auto& vec_add_dpbusd_32x4 = m512_add_dpbusd_epi32x4;
      auto& vec_hadd = m512_hadd;
#elif defined (USE_AVX2)
      using vec_t = __m256i;
      #define vec_setzero _mm256_setzero_si256
      #define vec_set_32 _mm256_set1_epi32
      auto& vec_add_dpbusd_32 = m256_add_dpbusd_epi32;
      auto& vec_add_dpbusd_32x4 = m256_add_dpbusd_epi32x4;
      auto& vec_hadd = m256_hadd;
#elif defined (USE_SSSE3)
      using vec_t = __m128i;
      #define vec_setzero _mm_setzero_si128
      #define vec_set_32 _mm_set1_epi32
      auto& vec_add_dpbusd_32 = m128_add_dpbusd_epi32;
      auto& vec_add_dpbusd_32x4 = m128_add_dpbusd_epi32x4;
      auto& vec_hadd = m128_hadd;
#endif

#if defined (USE_SSSE3)

      const auto output = reinterpret_cast<OutputType*>(buffer);
      const auto input_vector = reinterpret_cast<const vec_t*>(input);

      static_assert(kOutputDimensions % kOutputSimdWidth == 0 || kOutputDimensions == 1);

      // kOutputDimensions is either 1 or a multiple of kSimdWidth
      // because then it is also an input dimension.
      if constexpr (kOutputDimensions % kOutputSimdWidth == 0)
      {
          constexpr IndexType kNumChunks = kPaddedInputDimensions / 4;

          const auto input32 = reinterpret_cast<const std::int32_t*>(input);
          vec_t* outptr = reinterpret_cast<vec_t*>(output);
          std::memcpy(output, biases_, kOutputDimensions * sizeof(OutputType));

          for (int i = 0; i < (int)kNumChunks - 3; i += 4)
          {
              const vec_t in0 = vec_set_32(input32[i + 0]);
              const vec_t in1 = vec_set_32(input32[i + 1]);
              const vec_t in2 = vec_set_32(input32[i + 2]);
              const vec_t in3 = vec_set_32(input32[i + 3]);
              const auto col0 = reinterpret_cast<const vec_t*>(&weights_[(i + 0) * kOutputDimensions * 4]);
              const auto col1 = reinterpret_cast<const vec_t*>(&weights_[(i + 1) * kOutputDimensions * 4]);
              const auto col2 = reinterpret_cast<const vec_t*>(&weights_[(i + 2) * kOutputDimensions * 4]);
              const auto col3 = reinterpret_cast<const vec_t*>(&weights_[(i + 3) * kOutputDimensions * 4]);
              for (int j = 0; j * kOutputSimdWidth < kOutputDimensions; ++j)
                  vec_add_dpbusd_32x4(outptr[j], in0, col0[j], in1, col1[j], in2, col2[j], in3, col3[j]);
          }
          for (int i = 0; i < canSaturate16.count; ++i)
              output[canSaturate16.ids[i].out] += input[canSaturate16.ids[i].in] * canSaturate16.ids[i].w;
      }
      else if constexpr (kOutputDimensions == 1)
      {
#if defined (USE_AVX512)
          if constexpr (kPaddedInputDimensions % (kSimdWidth * 2) != 0)
          {
              constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
              const auto input_vector256 = reinterpret_cast<const __m256i*>(input);

              __m256i sum0 = _mm256_setzero_si256();
              const auto row0 = reinterpret_cast<const __m256i*>(&weights_[0]);

              for (int j = 0; j < (int)kNumChunks; ++j)
              {
                  const __m256i in = input_vector256[j];
                  m256_add_dpbusd_epi32(sum0, in, row0[j]);
              }
              output[0] = m256_hadd(sum0, biases_[0]);
          }
          else
#endif
          {
#if defined (USE_AVX512)
              constexpr IndexType kNumChunks = kPaddedInputDimensions / (kSimdWidth * 2);
#else
              constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
#endif
              vec_t sum0 = vec_setzero();
              const auto row0 = reinterpret_cast<const vec_t*>(&weights_[0]);

              for (int j = 0; j < (int)kNumChunks; ++j)
              {
                  const vec_t in = input_vector[j];
                  vec_add_dpbusd_32(sum0, in, row0[j]);
              }
              output[0] = vec_hadd(sum0, biases_[0]);
          }
      }

#else

// Use old implementation for the other architectures.

      auto output = reinterpret_cast<OutputType*>(buffer);

#if defined(USE_SSE2)
      constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
      const __m128i kZeros = _mm_setzero_si128();
      const auto input_vector = reinterpret_cast<const __m128i*>(input);

#elif defined(USE_MMX)
      constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
      const __m64 kZeros = _mm_setzero_si64();
      const auto input_vector = reinterpret_cast<const __m64*>(input);

#elif defined(USE_NEON)
      constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
      const auto input_vector = reinterpret_cast<const int8x8_t*>(input);
#endif

      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        const IndexType offset = i * kPaddedInputDimensions;

#if defined(USE_SSE2)
        __m128i sum_lo = _mm_cvtsi32_si128(biases_[i]);
        __m128i sum_hi = kZeros;
        const auto row = reinterpret_cast<const __m128i*>(&weights_[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          __m128i row_j = _mm_load_si128(&row[j]);
          __m128i input_j = _mm_load_si128(&input_vector[j]);
          __m128i extended_row_lo = _mm_srai_epi16(_mm_unpacklo_epi8(row_j, row_j), 8);
          __m128i extended_row_hi = _mm_srai_epi16(_mm_unpackhi_epi8(row_j, row_j), 8);
          __m128i extended_input_lo = _mm_unpacklo_epi8(input_j, kZeros);
          __m128i extended_input_hi = _mm_unpackhi_epi8(input_j, kZeros);
          __m128i product_lo = _mm_madd_epi16(extended_row_lo, extended_input_lo);
          __m128i product_hi = _mm_madd_epi16(extended_row_hi, extended_input_hi);
          sum_lo = _mm_add_epi32(sum_lo, product_lo);
          sum_hi = _mm_add_epi32(sum_hi, product_hi);
        }
        __m128i sum = _mm_add_epi32(sum_lo, sum_hi);
        __m128i sum_high_64 = _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2));
        sum = _mm_add_epi32(sum, sum_high_64);
        __m128i sum_second_32 = _mm_shufflelo_epi16(sum, _MM_SHUFFLE(1, 0, 3, 2));
        sum = _mm_add_epi32(sum, sum_second_32);
        output[i] = _mm_cvtsi128_si32(sum);

#elif defined(USE_MMX)
        __m64 sum_lo = _mm_cvtsi32_si64(biases_[i]);
        __m64 sum_hi = kZeros;
        const auto row = reinterpret_cast<const __m64*>(&weights_[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          __m64 row_j = row[j];
          __m64 input_j = input_vector[j];
          __m64 extended_row_lo = _mm_srai_pi16(_mm_unpacklo_pi8(row_j, row_j), 8);
          __m64 extended_row_hi = _mm_srai_pi16(_mm_unpackhi_pi8(row_j, row_j), 8);
          __m64 extended_input_lo = _mm_unpacklo_pi8(input_j, kZeros);
          __m64 extended_input_hi = _mm_unpackhi_pi8(input_j, kZeros);
          __m64 product_lo = _mm_madd_pi16(extended_row_lo, extended_input_lo);
          __m64 product_hi = _mm_madd_pi16(extended_row_hi, extended_input_hi);
          sum_lo = _mm_add_pi32(sum_lo, product_lo);
          sum_hi = _mm_add_pi32(sum_hi, product_hi);
        }
        __m64 sum = _mm_add_pi32(sum_lo, sum_hi);
        sum = _mm_add_pi32(sum, _mm_unpackhi_pi32(sum, sum));
        output[i] = _mm_cvtsi64_si32(sum);

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
#if defined(USE_MMX)
      _mm_empty();
#endif

#endif

      return output;
    }

   private:
    using BiasType = OutputType;
    using WeightType = std::int8_t;

    PreviousLayer previous_layer_;

    alignas(kCacheLineSize) BiasType biases_[kOutputDimensions];
    alignas(kCacheLineSize) WeightType weights_[kOutputDimensions * kPaddedInputDimensions];
#if defined (USE_SSSE3)
    struct CanSaturate {
        int count;
        struct Entry {
            uint16_t out;
            uint16_t in;
            int8_t w;
        } ids[kPaddedInputDimensions * kOutputDimensions * 3 / 4];

        void add(int i, int j, int8_t w) {
            ids[count].out = i;
            ids[count].in = j;
            ids[count].w = w;
            ++count;
        }
    } canSaturate16;
#endif
  };

}  // namespace Stockfish::Eval::NNUE::Layers

#endif // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
