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

// Find indices of nonzero 32-bit(for KNM is 64-bit) values in a packed byte buffer.
// The input pointer addresses a sequence of 32-bit(for KNM is 64-bit) blocks stored
// in a std::uint8_t array.
template<const IndexType InputDimensions>
void find_nnz(const std::uint8_t* RESTRICT input,
              std::uint16_t* RESTRICT      out,
              IndexType&                   count_out) {

    #if defined(USE_KNM)

    constexpr IndexType SimdWidth = 8;  // 512 bits / 64 bits
    constexpr IndexType NumChunks = InputDimensions / SimdWidth;
    const __m512i       increment = _mm512_set1_epi64(SimdWidth);
    __m512i base = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);

    IndexType count = 0;
    for (IndexType i = 0; i < NumChunks; ++i)
    {
        const __m512i inputV = _mm512_load_si512(input + i * SimdWidth * sizeof(std::uint64_t));

        // Get a bitmask and gather non zero indices
        const __mmask8 nnzMask = _mm512_test_epi64_mask(inputV, inputV);
        const __m512i  nnzV    = _mm512_maskz_compress_epi64(nnzMask, base);
        _mm512_mask_cvtepi64_storeu_epi16(out + count, 0xFF, nnzV);
        count += popcount(nnzMask);
        base = _mm512_add_epi64(base, increment);
    }
    count_out = count;

    #elif defined(USE_AVX512ICL)

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

    // Scrambled index mapping for the weight layout optimized for 4VNNIW SIMD instructions.
    //
    // The weight matrix (originally OutputDimensions x PaddedInputDimensions) is reorganized
    // to group inputs in pairs of 8 units and outputs in blocks of 16 neurons.
    // For each 8‑unit input pair and each 16‑neuron output group, the weights for the 16×8 = 128
    // connections are stored in four consecutive chunks, each chunk corresponding to a 2‑unit
    // sub‑group within the input pair (units 0‑1, 2‑3, 4‑5, 6‑7).
    //
    // Within a chunk, the weights for the 16 neurons are interleaved per neuron:
    //   [neuron0_unit0, neuron0_unit1, neuron1_unit0, neuron1_unit1, ...,
    //    neuron15_unit0, neuron15_unit1]
    //
    // Thus the overall memory order is:
    //   for each input pair p (0..PaddedInputDimensions/8 - 1)
    //     for each output group g (0..OutputDimensions/16 - 1)
    //       for each 2‑unit sub‑group s (0..3)
    //         for each neuron n in group g (0..15)
    //           for each unit offset b within the sub‑group (0..1)
    //             weight of output neuron (g*16 + n) for input unit (p*8 + s*2 + b)
    //
    // The mapping from the linear index i = out * InUnits + unit (where InUnits = PaddedInputDimensions)
    // to the new linear index is:
    //   pair   = unit / 8
    //   sg     = (unit % 8) / 2          // sub‑group within the 8‑unit pair
    //   sub    = (unit % 8) % 2          // offset inside the 2‑unit sub‑group
    //   out_g  = out / 16                // output group
    //   n      = out % 16                // neuron inside the group
    //   new_idx = (pair * OutputDimensions * 8) + (out_g * 128) + (sg * 32) + (n * 2) + sub
    //
    // This layout is designed to feed 4VNNIW instructions that process 16 neurons × 8 inputs
    // in a single operation.
    static constexpr IndexType get_weight_index_scrambled_4vnniw(IndexType i) {
        constexpr IndexType InBytes = PaddedInputDimensions;

        const IndexType out = i / InBytes;
        const IndexType byte = i % InBytes;

        const IndexType pair = byte >> 3;          // byte / 8
        const IndexType g = (byte >> 1) & 3;       // (byte/2) % 4
        const IndexType sub = byte & 1;            // byte % 2

        const IndexType out_group = out >> 4;      // out / 16
        const IndexType n = out & 0xF;             // out % 16

        return (pair * OutputDimensions << 3)      // pair * OutputDimensions * 8
             + (out_group << 7)                    // out_group * 128
             + (g << 5)                            // g * 32
             + (n << 1)                            // n * 2
             + sub;
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
#if defined(USE_KNM)
            weights[get_weight_index_scrambled_4vnniw(i)] = static_cast<std::int16_t>(read_little_endian<WeightType>(stream));
#else
            weights[get_weight_index(i)] = read_little_endian<WeightType>(stream);
#endif

        return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
        write_little_endian<BiasType>(stream, biases, OutputDimensions);

        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
#if defined(USE_KNM)
            write_little_endian<WeightType>(stream, static_cast<WeightType>(weights[get_weight_index_scrambled_4vnniw(i)]));
#else
            write_little_endian<WeightType>(stream, weights[get_weight_index(i)]);
#endif

        return !stream.fail();
    }

    std::size_t get_content_hash() const {
        std::size_t h = 0;
        hash_combine(h, get_raw_data_hash(biases));
#if defined(USE_KNM)
        WeightType originalWeights[OutputDimensions * PaddedInputDimensions];
        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            originalWeights[i] = static_cast<WeightType>(weights[get_weight_index_scrambled_4vnniw(get_weight_index(i))]);
        hash_combine(h, get_raw_data_hash(originalWeights));
#else
        hash_combine(h, get_raw_data_hash(weights));
#endif
        hash_combine(h, get_hash_value(0));
        return h;
    }

    // Forward propagation
    void propagate(const InputType* input, OutputType* output) const {

#if (USE_SSSE3 | (USE_NEON >= 8))
    #if defined(USE_AVX512) || defined(USE_KNM)
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
        constexpr IndexType NumChunks =
    #if defined(USE_KNM)
          ceil_to_multiple<IndexType>(InputDimensions, 8) / 8;
    #else
          ceil_to_multiple<IndexType>(InputDimensions, 8) / ChunkSize;
    #endif
        constexpr IndexType NumAccums = OutputDimensions / OutputSimdWidth;
        // If we're using high-latency dot product instructions, split the accumulators
        // to create 3 separate dependency chains and merge at the end
        constexpr IndexType NumRegs =
    #if defined(USE_VNNI) || defined(USE_KNM)
          3 * NumAccums;
    #else
          NumAccums;
    #endif
        std::uint16_t nnz[NumChunks];
        IndexType     count;

        // Find indices of nonzero 32-bit(for KNM is 64-bit) blocks
        find_nnz<NumChunks>(input, nnz, count);

        const outvec_t* biasvec = reinterpret_cast<const outvec_t*>(biases);
        outvec_t        acc[NumRegs];
        for (IndexType k = 0; k < NumAccums; ++k)
            acc[k] = biasvec[k];

        const auto* start = nnz;
        const auto* end   = nnz + count;

        // convince GCC to not do weird pointer arithmetic in the following loop
    #if defined(USE_KNM)
        const std::int16_t* weights_cp = weights;
    #else
        const std::int8_t* weights_cp = weights;
    #endif
    #if defined(USE_VNNI) || defined(USE_KNM)
        for (IndexType k = NumAccums; k < NumRegs; ++k)
        #if defined(USE_KNM)
            acc[k] = _mm512_setzero_epi32();
        #else
            acc[k] = vec_zero();
        #endif

        while (start < end - 2)
        {
            const std::ptrdiff_t i0 = *start++;
            const std::ptrdiff_t i1 = *start++;
            const std::ptrdiff_t i2 = *start++;
        #if defined(USE_KNM)
            __m128i in0 =
              _mm_cvtepu8_epi16(_mm_set1_epi64(load_as<__m64>(input + i0 * sizeof(__m64))));
            __m128i in1 =
              _mm_cvtepu8_epi16(_mm_set1_epi64(load_as<__m64>(input + i1 * sizeof(__m64))));
            __m128i in2 =
              _mm_cvtepu8_epi16(_mm_set1_epi64(load_as<__m64>(input + i2 * sizeof(__m64))));
            const auto w0 =
              reinterpret_cast<const invec_t*>(&weights_cp[i0 * 128 * NumAccums]);
            const auto w1 =
              reinterpret_cast<const invec_t*>(&weights_cp[i1 * 128 * NumAccums]);
            const auto w2 =
              reinterpret_cast<const invec_t*>(&weights_cp[i2 * 128 * NumAccums]);
            for (IndexType k = 0; k < NumAccums; ++k)
            {
                __asm__ volatile( // Using inline assembly to avoid bugs caused by compiler optimization
                    "vmovdqa32 %0, %%zmm8 \n\t"
                    "vmovdqa32 %1, %%zmm9 \n\t"
                    "vmovdqa32 %2, %%zmm10 \n\t"
                    "vmovdqa32 %3, %%zmm0 \n\t"
                    "vmovdqa32 %4, %%zmm1 \n\t"
                    "vmovdqa32 %5, %%zmm2 \n\t"
                    "vmovdqa32 %6, %%zmm3 \n\t"
                    "vp4dpwssd %7, %%zmm0, %%zmm8 \n\t"
                    "vmovdqa32 %8, %%zmm4 \n\t"
                    "vmovdqa32 %9, %%zmm5 \n\t"
                    "vmovdqa64 %%zmm8, %0 \n\t"
                    "vmovdqa32 %10, %%zmm6 \n\t"
                    "vmovdqa32 %11, %%zmm7 \n\t"
                    "vp4dpwssd %12, %%zmm4, %%zmm9 \n\t"
                    "vmovdqa32 %13, %%zmm0 \n\t"
                    "vmovdqa32 %14, %%zmm1 \n\t"
                    "vmovdqa64 %%zmm9, %1 \n\t"
                    "vmovdqa32 %15, %%zmm2 \n\t"
                    "vmovdqa32 %16, %%zmm3 \n\t"
                    "vp4dpwssd %17, %%zmm0, %%zmm10 \n\t"
                    "vmovdqa64 %%zmm10, %2"
                    :
                    "+m"(acc[k]),
                    "+m"(acc[k + NumAccums]),
                    "+m"(acc[k + 2 * NumAccums])
                    :
                    "m"(w0[k * 4 + 0]), "m"(w0[k * 4 + 1]), "m"(w0[k * 4 + 2]), "m"(w0[k * 4 + 3]), "m"(in0),
                    "m"(w1[k * 4 + 0]), "m"(w1[k * 4 + 1]), "m"(w1[k * 4 + 2]), "m"(w1[k * 4 + 3]), "m"(in1),
                    "m"(w2[k * 4 + 0]), "m"(w2[k * 4 + 1]), "m"(w2[k * 4 + 2]), "m"(w2[k * 4 + 3]), "m"(in2)
                    :
                    "zmm0", "zmm1", "zmm2", "zmm3", "zmm4",
                    "zmm5", "zmm6", "zmm7", "zmm8", "zmm9",
                    "zmm10"
                );
                /*
                acc[k] =
                  _mm512_4dpwssd_epi32(acc[k], w0[k * 4 + 0], w0[k * 4 + 1], w0[k * 4 + 2], w0[k * 4 + 3], &in0);
                acc[k + NumAccums] =
                  _mm512_4dpwssd_epi32(acc[k + NumAccums], w1[k * 4 + 0], w1[k * 4 + 1], w1[k * 4 + 2], w1[k * 4 + 3], &in1);
                acc[k + 2 * NumAccums] =
                  _mm512_4dpwssd_epi32(acc[k + 2 * NumAccums], w2[k * 4 + 0], w2[k * 4 + 1], w2[k * 4 + 2], w2[k * 4 + 3], &in2);
                */
            }
        #else
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
        #endif
        }
        for (IndexType k = 0; k < NumAccums; ++k)
            acc[k] = vec_add_32(vec_add_32(acc[k], acc[k + NumAccums]), acc[k + 2 * NumAccums]);
    #endif
        while (start < end)
        {
            const std::ptrdiff_t i = *start++;
        #if defined(USE_KNM)
            __m128i in =
              _mm_cvtepu8_epi16(_mm_set1_epi64(load_as<__m64>(input + i * sizeof(__m64))));
            const auto w =
              reinterpret_cast<const invec_t*>(&weights_cp[i * 128 * NumAccums]);
            for (IndexType k = 0; k < NumAccums; ++k)
                __asm__ volatile( // Using inline assembly to avoid bugs caused by compiler optimization
                    "vmovdqa32 %0, %%zmm4 \n\t"
                    "vmovdqa32 %1, %%zmm0 \n\t"
                    "vmovdqa32 %2, %%zmm1 \n\t"
                    "vmovdqa32 %3, %%zmm2 \n\t"
                    "vmovdqa32 %4, %%zmm3 \n\t"
                    "vp4dpwssd %5, %%zmm0, %%zmm4 \n\t"
                    "vmovdqa64 %%zmm4, %0"
                    : "+m"(acc[k])
                    : "m"(w[k * 4 + 0]), "m"(w[k * 4 + 1]), "m"(w[k * 4 + 2]), "m"(w[k * 4 + 3]), "m"(in)
                    : "zmm0", "zmm1", "zmm2", "zmm3", "zmm4"
                );
            //    acc[k] = _mm512_4dpwssd_epi32(acc[k], w[k * 4 + 0], w[k * 4 + 1], w[k * 4 + 2], w[k * 4 + 3], &in);
        #else
            const invec_t in = vec_set_32(load_as<std::int32_t>(input + i * sizeof(std::int32_t)));
            const auto    col =
              reinterpret_cast<const invec_t*>(&weights_cp[i * OutputDimensions * ChunkSize]);
            for (IndexType k = 0; k < NumAccums; ++k)
                vec_add_dpbusd_32(acc[k], in, col[k]);
        #endif
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
#if defined(USE_KNM)
    // Use AVX-512_4VNNIW instructions, the type of weights must be int16.
    alignas(CacheLineSize) std::int16_t weights[OutputDimensions * PaddedInputDimensions];
#else
    alignas(CacheLineSize) WeightType weights[OutputDimensions * PaddedInputDimensions];
#endif
};

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
