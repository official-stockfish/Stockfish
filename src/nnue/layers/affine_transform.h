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

// Definition of layer AffineTransform of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#include <cstdint>
#include <iostream>

#include "../nnue_common.h"
#include "simd.h"

/*
  This file contains the definition for a fully connected layer (aka affine transform).

    - expected use-case is for when PaddedInputDimensions == 32 and InputDimensions <= 32.
      - that's why AVX512 is hard to implement
    - expected use-case is small layers
    - inputs are processed in chunks of 4, weights are respectively transposed
    - accumulation happens directly to int32s
*/

namespace Stockfish::Eval::NNUE::Layers {

#if defined(USE_SSSE3) || defined(USE_NEON_DOTPROD)
    #define ENABLE_SEQ_OPT
#endif

// Fallback implementation for older/other architectures.
// Requires the input to be padded to at least 16 values.
#ifndef ENABLE_SEQ_OPT

template<IndexType InputDimensions, IndexType PaddedInputDimensions, IndexType OutputDimensions>
static void affine_transform_non_ssse3(std::int32_t*       output,
                                       const std::int8_t*  weights,
                                       const std::int32_t* biases,
                                       const std::uint8_t* input) {
    #if defined(USE_SSE2) || defined(USE_NEON)
        #if defined(USE_SSE2)
    // At least a multiple of 16, with SSE2.
    constexpr IndexType NumChunks   = ceil_to_multiple<IndexType>(InputDimensions, 16) / 16;
    const __m128i       Zeros       = _mm_setzero_si128();
    const auto          inputVector = reinterpret_cast<const __m128i*>(input);

        #elif defined(USE_NEON)
    constexpr IndexType NumChunks   = ceil_to_multiple<IndexType>(InputDimensions, 16) / 16;
    const auto          inputVector = reinterpret_cast<const int8x8_t*>(input);
        #endif

    for (IndexType i = 0; i < OutputDimensions; ++i)
    {
        const IndexType offset = i * PaddedInputDimensions;

        #if defined(USE_SSE2)
        __m128i    sumLo = _mm_cvtsi32_si128(biases[i]);
        __m128i    sumHi = Zeros;
        const auto row   = reinterpret_cast<const __m128i*>(&weights[offset]);
        for (IndexType j = 0; j < NumChunks; ++j)
        {
            __m128i row_j           = _mm_load_si128(&row[j]);
            __m128i input_j         = _mm_load_si128(&inputVector[j]);
            __m128i extendedRowLo   = _mm_srai_epi16(_mm_unpacklo_epi8(row_j, row_j), 8);
            __m128i extendedRowHi   = _mm_srai_epi16(_mm_unpackhi_epi8(row_j, row_j), 8);
            __m128i extendedInputLo = _mm_unpacklo_epi8(input_j, Zeros);
            __m128i extendedInputHi = _mm_unpackhi_epi8(input_j, Zeros);
            __m128i productLo       = _mm_madd_epi16(extendedRowLo, extendedInputLo);
            __m128i productHi       = _mm_madd_epi16(extendedRowHi, extendedInputHi);
            sumLo                   = _mm_add_epi32(sumLo, productLo);
            sumHi                   = _mm_add_epi32(sumHi, productHi);
        }
        __m128i sum           = _mm_add_epi32(sumLo, sumHi);
        __m128i sumHigh_64    = _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2));
        sum                   = _mm_add_epi32(sum, sumHigh_64);
        __m128i sum_second_32 = _mm_shufflelo_epi16(sum, _MM_SHUFFLE(1, 0, 3, 2));
        sum                   = _mm_add_epi32(sum, sum_second_32);
        output[i]             = _mm_cvtsi128_si32(sum);

        #elif defined(USE_NEON)

        int32x4_t  sum = {biases[i]};
        const auto row = reinterpret_cast<const int8x8_t*>(&weights[offset]);
        for (IndexType j = 0; j < NumChunks; ++j)
        {
            int16x8_t product = vmull_s8(inputVector[j * 2], row[j * 2]);
            product           = vmlal_s8(product, inputVector[j * 2 + 1], row[j * 2 + 1]);
            sum               = vpadalq_s16(sum, product);
        }
        output[i] = sum[0] + sum[1] + sum[2] + sum[3];

        #endif
    }
    #else
    std::memcpy(output, biases, sizeof(std::int32_t) * OutputDimensions);

    // Traverse weights in transpose order to take advantage of input sparsity
    for (IndexType i = 0; i < InputDimensions; ++i)
        if (input[i])
        {
            const std::int8_t* w  = &weights[i];
            const int          in = input[i];
            for (IndexType j = 0; j < OutputDimensions; ++j)
                output[j] += w[j * PaddedInputDimensions] * in;
        }
    #endif
}

#endif  // !ENABLE_SEQ_OPT

template<IndexType InDims, IndexType OutDims>
class AffineTransform {
   public:
    // Input/output type
    using InputType  = std::uint8_t;
    using OutputType = std::int32_t;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = OutDims;

    static constexpr IndexType PaddedInputDimensions =
      ceil_to_multiple<IndexType>(InputDimensions, MaxSimdWidth);
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, MaxSimdWidth);

    using OutputBuffer = OutputType[PaddedOutputDimensions];

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value(std::uint32_t prevHash) {
        std::uint32_t hashValue = 0xCC03DAE4u;
        hashValue += OutputDimensions;
        hashValue ^= prevHash >> 1;
        hashValue ^= prevHash << 31;
        return hashValue;
    }

    static constexpr IndexType get_weight_index_scrambled(IndexType i) {
        return (i / 4) % (PaddedInputDimensions / 4) * OutputDimensions * 4
             + i / PaddedInputDimensions * 4 + i % 4;
    }

    static constexpr IndexType get_weight_index(IndexType i) {
#ifdef ENABLE_SEQ_OPT
        return get_weight_index_scrambled(i);
#else
        return i;
#endif
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
    void propagate(const InputType* input, OutputType* output) const {

#ifdef ENABLE_SEQ_OPT

        if constexpr (OutputDimensions > 1)
        {
    #if defined(USE_AVX512)
            using vec_t = __m512i;
        #define vec_set_32 _mm512_set1_epi32
        #define vec_add_dpbusd_32 Simd::m512_add_dpbusd_epi32
    #elif defined(USE_AVX2)
            using vec_t = __m256i;
        #define vec_set_32 _mm256_set1_epi32
        #define vec_add_dpbusd_32 Simd::m256_add_dpbusd_epi32
    #elif defined(USE_SSSE3)
            using vec_t = __m128i;
        #define vec_set_32 _mm_set1_epi32
        #define vec_add_dpbusd_32 Simd::m128_add_dpbusd_epi32
    #elif defined(USE_NEON_DOTPROD)
            using vec_t = int32x4_t;
        #define vec_set_32 vdupq_n_s32
        #define vec_add_dpbusd_32(acc, a, b) \
            Simd::dotprod_m128_add_dpbusd_epi32(acc, vreinterpretq_s8_s32(a), \
                                                vreinterpretq_s8_s32(b))
    #endif

            static constexpr IndexType OutputSimdWidth = sizeof(vec_t) / sizeof(OutputType);

            static_assert(OutputDimensions % OutputSimdWidth == 0);

            constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(InputDimensions, 8) / 4;
            constexpr IndexType NumRegs   = OutputDimensions / OutputSimdWidth;

            const auto   input32 = reinterpret_cast<const std::int32_t*>(input);
            const vec_t* biasvec = reinterpret_cast<const vec_t*>(biases);
            vec_t        acc[NumRegs];
            for (IndexType k = 0; k < NumRegs; ++k)
                acc[k] = biasvec[k];

            for (IndexType i = 0; i < NumChunks; ++i)
            {
                const vec_t in0 = vec_set_32(input32[i]);
                const auto  col0 =
                  reinterpret_cast<const vec_t*>(&weights[i * OutputDimensions * 4]);

                for (IndexType k = 0; k < NumRegs; ++k)
                    vec_add_dpbusd_32(acc[k], in0, col0[k]);
            }

            vec_t* outptr = reinterpret_cast<vec_t*>(output);
            for (IndexType k = 0; k < NumRegs; ++k)
                outptr[k] = acc[k];

    #undef vec_set_32
    #undef vec_add_dpbusd_32
        }
        else if constexpr (OutputDimensions == 1)
        {
    // We cannot use AVX512 for the last layer because there are only 32 inputs
    // and the buffer is not padded to 64 elements.
    #if defined(USE_AVX2)
            using vec_t = __m256i;
        #define vec_setzero() _mm256_setzero_si256()
        #define vec_set_32 _mm256_set1_epi32
        #define vec_add_dpbusd_32 Simd::m256_add_dpbusd_epi32
        #define vec_hadd Simd::m256_hadd
    #elif defined(USE_SSSE3)
            using vec_t = __m128i;
        #define vec_setzero() _mm_setzero_si128()
        #define vec_set_32 _mm_set1_epi32
        #define vec_add_dpbusd_32 Simd::m128_add_dpbusd_epi32
        #define vec_hadd Simd::m128_hadd
    #elif defined(USE_NEON_DOTPROD)
            using vec_t = int32x4_t;
        #define vec_setzero() vdupq_n_s32(0)
        #define vec_set_32 vdupq_n_s32
        #define vec_add_dpbusd_32(acc, a, b) \
            Simd::dotprod_m128_add_dpbusd_epi32(acc, vreinterpretq_s8_s32(a), \
                                                vreinterpretq_s8_s32(b))
        #define vec_hadd Simd::neon_m128_hadd
    #endif

            const auto inputVector = reinterpret_cast<const vec_t*>(input);

            static constexpr IndexType InputSimdWidth = sizeof(vec_t) / sizeof(InputType);

            static_assert(PaddedInputDimensions % InputSimdWidth == 0);

            constexpr IndexType NumChunks = PaddedInputDimensions / InputSimdWidth;
            vec_t               sum0      = vec_setzero();
            const auto          row0      = reinterpret_cast<const vec_t*>(&weights[0]);

            for (int j = 0; j < int(NumChunks); ++j)
            {
                const vec_t in = inputVector[j];
                vec_add_dpbusd_32(sum0, in, row0[j]);
            }
            output[0] = vec_hadd(sum0, biases[0]);

    #undef vec_setzero
    #undef vec_set_32
    #undef vec_add_dpbusd_32
    #undef vec_hadd
        }
#else
        // Use old implementation for the other architectures.
        affine_transform_non_ssse3<InputDimensions, PaddedInputDimensions, OutputDimensions>(
          output, weights, biases, input);
#endif
    }

   private:
    using BiasType   = OutputType;
    using WeightType = std::int8_t;

    alignas(CacheLineSize) BiasType biases[OutputDimensions];
    alignas(CacheLineSize) WeightType weights[OutputDimensions * PaddedInputDimensions];
};

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
