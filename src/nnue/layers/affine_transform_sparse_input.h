/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

// Definition of layer AffineTransformSparseInput of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "../../bitboard.h"
#include "../../memory.h"
#include "../simd.h"
#include "../nnue_common.h"

/*
  This file contains the definition for a fully connected layer (aka affine transform) with block sparse input.
*/

namespace Stockfish::Eval::NNUE::Layers {

#if (USE_SSSE3 | (USE_NEON >= 8))
static constexpr int lsb_index64[64] = {
  0,  47, 1,  56, 48, 27, 2,  60, 57, 49, 41, 37, 28, 16, 3,  61, 54, 58, 35, 52, 50, 42,
  21, 44, 38, 32, 29, 23, 17, 11, 4,  62, 46, 55, 26, 59, 40, 36, 15, 53, 34, 51, 20, 43,
  31, 22, 10, 45, 25, 39, 14, 33, 19, 30, 9,  24, 13, 18, 8,  12, 7,  6,  5,  63};

constexpr int constexpr_lsb(uint64_t bb) {
    assert(bb != 0);
    constexpr uint64_t debruijn64 = 0x03F79D71B4CB0A89ULL;
    return lsb_index64[((bb ^ (bb - 1)) * debruijn64) >> 58];
}

alignas(CacheLineSize) static constexpr struct OffsetIndices {

    std::uint16_t offset_indices[256][8];

    constexpr OffsetIndices() :
        offset_indices() {
        for (int i = 0; i < 256; ++i)
        {
            std::uint64_t j = i, k = 0;
            while (j)
            {
                offset_indices[i][k++] = constexpr_lsb(j);
                j &= j - 1;
            }
            while (k < 8)
                offset_indices[i][k++] = 0;
        }
    }

} Lookup;

    #if defined(__GNUC__) || defined(__clang__)
        #define RESTRICT __restrict__
    #elif defined(_MSC_VER)
        #define RESTRICT __restrict
    #else
        #define RESTRICT
    #endif

// Find indices of nonzero 32-bit values in a packed byte buffer.
// The input pointer addresses a sequence of 32-bit blocks stored in a
// std::uint8_t array.
template<const IndexType InputDimensions>
void find_nnz(const std::uint8_t* RESTRICT input,
              std::uint16_t* RESTRICT      out,
              IndexType&                   count_out) {

    #if defined(USE_AVX512ICL)

    constexpr IndexType SimdWidthIn  = 64;  // 512 bits
    constexpr IndexType SimdWidthOut = 32;  // 512 bits / 16 bits
    constexpr IndexType NumChunks    = InputDimensions / SimdWidthOut;
    const __m512i       increment    = _mm512_set1_epi16(SimdWidthOut);
    __m512i             base = _mm512_set_epi16(  // Same permute order as _mm512_packus_epi32()
      31, 30, 29, 28, 15, 14, 13, 12, 27, 26, 25, 24, 11, 10, 9, 8, 23, 22, 21, 20, 7, 6, 5, 4, 19,
      18, 17, 16, 3, 2, 1, 0);

    IndexType count = 0;
    for (IndexType i = 0; i < NumChunks; ++i)
    {
        const __m512i inputV0 = _mm512_load_si512(input + i * 2 * SimdWidthIn);
        const __m512i inputV1 = _mm512_load_si512(input + i * 2 * SimdWidthIn + SimdWidthIn);

        // Get a bitmask and gather non zero indices
        const __m512i   inputV01 = _mm512_packus_epi32(inputV0, inputV1);
        const __mmask32 nnzMask  = _mm512_test_epi16_mask(inputV01, inputV01);

        // Avoid _mm512_mask_compressstoreu_epi16() as it's 256 uOps on Zen4
        __m512i nnz = _mm512_maskz_compress_epi16(nnzMask, base);
        _mm512_storeu_si512(out + count, nnz);

        count += popcount(nnzMask);
        base = _mm512_add_epi16(base, increment);
    }
    count_out = count;

    #elif defined(USE_AVX512)

    constexpr IndexType SimdWidth = 16;  // 512 bits / 32 bits
    constexpr IndexType NumChunks = InputDimensions / SimdWidth;
    const __m512i       increment = _mm512_set1_epi32(SimdWidth);
    __m512i base = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);

    IndexType count = 0;
    for (IndexType i = 0; i < NumChunks; ++i)
    {
        const __m512i inputV = _mm512_load_si512(input + i * SimdWidth * sizeof(std::uint32_t));

        // Get a bitmask and gather non zero indices
        const __mmask16 nnzMask = _mm512_test_epi32_mask(inputV, inputV);
        const __m512i   nnzV    = _mm512_maskz_compress_epi32(nnzMask, base);
        _mm512_mask_cvtepi32_storeu_epi16(out + count, 0xFFFF, nnzV);
        count += popcount(nnzMask);
        base = _mm512_add_epi32(base, increment);
    }
    count_out = count;

    #else

    using namespace SIMD;

    constexpr IndexType InputSimdWidth = sizeof(vec_uint_t) / sizeof(std::int32_t);
    // Outputs are processed 8 elements at a time, even if the SIMD width is narrower
    constexpr IndexType ChunkSize      = 8;
    constexpr IndexType NumChunks      = InputDimensions / ChunkSize;
    constexpr IndexType InputsPerChunk = ChunkSize / InputSimdWidth;

    static_assert(InputsPerChunk > 0 && "SIMD width too wide");

    const auto     inputVector = reinterpret_cast<const vec_uint_t*>(input);
    IndexType      count       = 0;
    vec128_t       base        = vec128_zero;
    const vec128_t increment   = vec128_set_16(8);
    for (IndexType i = 0; i < NumChunks; ++i)
    {
        // bitmask of nonzero values in this chunk
        unsigned nnz = 0;
        for (IndexType j = 0; j < InputsPerChunk; ++j)
        {
            const vec_uint_t inputChunk = inputVector[i * InputsPerChunk + j];
            nnz |= unsigned(vec_nnz(inputChunk)) << (j * InputSimdWidth);
        }
        const vec128_t offsets =
          vec128_load(reinterpret_cast<const vec128_t*>(&Lookup.offset_indices[nnz]));
        vec128_storeu(reinterpret_cast<vec128_t*>(out + count), vec128_add(base, offsets));
        count += popcount(nnz);
        base = vec128_add(base, increment);
    }
    count_out = count;
    #endif
}

#endif

// Sparse input implementation
template<IndexType InDims, IndexType OutDims>
class AffineTransformSparseInput {
   public:
    // Input/output type
    using InputType  = std::uint8_t;
    using OutputType = std::int32_t;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = OutDims;

    static_assert(OutputDimensions % 16 == 0,
                  "Only implemented for OutputDimensions divisible by 16.");

    static constexpr IndexType PaddedInputDimensions =
      ceil_to_multiple<IndexType>(InputDimensions, MaxSimdWidth);
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, MaxSimdWidth);

#if (USE_SSSE3 | (USE_NEON >= 8))
    static constexpr IndexType ChunkSize = 4;
#else
    static constexpr IndexType ChunkSize = 1;
#endif

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
        return (i / ChunkSize) % (PaddedInputDimensions / ChunkSize) * OutputDimensions * ChunkSize
             + i / PaddedInputDimensions * ChunkSize + i % ChunkSize;
    }

    static constexpr IndexType get_weight_index(IndexType i) {
#if (USE_SSSE3 | (USE_NEON >= 8))
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

    std::size_t get_content_hash() const {
        std::size_t h = 0;
        hash_combine(h, get_raw_data_hash(biases));
        hash_combine(h, get_raw_data_hash(weights));
        hash_combine(h, get_hash_value(0));
        return h;
    }

    // Forward propagation
    void propagate(const InputType* input, OutputType* output) const {

#if (USE_SSSE3 | (USE_NEON >= 8))
    #if defined(USE_AVX512)
        using invec_t  = __m512i;
        using outvec_t = __m512i;
        #define vec_add_32 _mm512_add_epi32
        #define vec_set_32 _mm512_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m512_add_dpbusd_epi32
    #elif defined(USE_AVX2)
        using invec_t  = __m256i;
        using outvec_t = __m256i;
        #define vec_add_32 _mm256_add_epi32
        #define vec_set_32 _mm256_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m256_add_dpbusd_epi32
    #elif defined(USE_SSSE3)
        using invec_t  = __m128i;
        using outvec_t = __m128i;
        #define vec_set_32 _mm_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m128_add_dpbusd_epi32
    #elif defined(USE_NEON_DOTPROD)
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
        #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
        #define vec_add_dpbusd_32 SIMD::dotprod_m128_add_dpbusd_epi32
    #elif defined(USE_NEON)
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
        #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
        #define vec_add_dpbusd_32 SIMD::neon_m128_add_dpbusd_epi32
    #endif
        constexpr IndexType OutputSimdWidth = sizeof(outvec_t) / sizeof(OutputType);
        constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(InputDimensions, 8) / ChunkSize;
        constexpr IndexType NumAccums = OutputDimensions / OutputSimdWidth;
        // If we're using high-latency dot product instructions, split the accumulators
        // to create 3 separate dependency chains and merge at the end
        constexpr IndexType NumRegs =
    #if defined(USE_VNNI)
          3 * NumAccums;
    #else
          NumAccums;
    #endif
        std::uint16_t nnz[NumChunks];
        IndexType     count;

        // Find indices of nonzero 32-bit blocks
        find_nnz<NumChunks>(input, nnz, count);

        const outvec_t* biasvec = reinterpret_cast<const outvec_t*>(biases);
        outvec_t        acc[NumRegs];
        for (IndexType k = 0; k < NumAccums; ++k)
            acc[k] = biasvec[k];

        const auto* start = nnz;
        const auto* end   = nnz + count;

        // convince GCC to not do weird pointer arithmetic in the following loop
        const std::int8_t* weights_cp = weights;
    #if defined(USE_VNNI)
        for (IndexType k = NumAccums; k < NumRegs; ++k)
            acc[k] = vec_zero();

        while (start < end - 2)
        {
            const std::ptrdiff_t i0 = *start++;
            const std::ptrdiff_t i1 = *start++;
            const std::ptrdiff_t i2 = *start++;
            const invec_t        in0 =
              vec_set_32(load_as<std::int32_t>(input + i0 * sizeof(std::int32_t)));
            const invec_t in1 =
              vec_set_32(load_as<std::int32_t>(input + i1 * sizeof(std::int32_t)));
            const invec_t in2 =
              vec_set_32(load_as<std::int32_t>(input + i2 * sizeof(std::int32_t)));
            const auto col0 =
              reinterpret_cast<const invec_t*>(&weights_cp[i0 * OutputDimensions * ChunkSize]);
            const auto col1 =
              reinterpret_cast<const invec_t*>(&weights_cp[i1 * OutputDimensions * ChunkSize]);
            const auto col2 =
              reinterpret_cast<const invec_t*>(&weights_cp[i2 * OutputDimensions * ChunkSize]);
            for (IndexType k = 0; k < NumAccums; ++k)
            {
                vec_add_dpbusd_32(acc[k], in0, col0[k]);
                vec_add_dpbusd_32(acc[k + NumAccums], in1, col1[k]);
                vec_add_dpbusd_32(acc[k + 2 * NumAccums], in2, col2[k]);
            }
        }
        for (IndexType k = 0; k < NumAccums; ++k)
            acc[k] = vec_add_32(vec_add_32(acc[k], acc[k + NumAccums]), acc[k + 2 * NumAccums]);
    #endif
        while (start < end)
        {
            const std::ptrdiff_t i = *start++;
            const invec_t in = vec_set_32(load_as<std::int32_t>(input + i * sizeof(std::int32_t)));
            const auto    col =
              reinterpret_cast<const invec_t*>(&weights_cp[i * OutputDimensions * ChunkSize]);
            for (IndexType k = 0; k < NumAccums; ++k)
                vec_add_dpbusd_32(acc[k], in, col[k]);
        }

        outvec_t* outptr = reinterpret_cast<outvec_t*>(output);
        for (IndexType k = 0; k < NumAccums; ++k)
            outptr[k] = acc[k];

    #undef vec_set_32
    #undef vec_add_dpbusd_32
    #ifdef vec_add_32
        #undef vec_add_32
    #endif
#else
        // Use dense implementation for the other architectures.
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

#endif  // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
