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
#include <cstring>
#include <iostream>

#include "../../bitboard.h"
#include "../../memory.h"
#include "../simd.h"
#include "../nnue_common.h"
#include "../nnz_helper.h"

/*
  This file contains the definition for a fully connected layer (aka affine transform) with block sparse input.
*/

namespace Stockfish::Eval::NNUE::Layers {

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

#if (defined(USE_SSSE3) || defined(USE_LSX) || defined(USE_LASX) || (USE_NEON >= 8))
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
#if (defined(USE_SSSE3) || defined(USE_LSX) || defined(USE_LASX) || (USE_NEON >= 8))
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
    void propagate(const InputType*                        input,
                   OutputType*                             output,
                   [[maybe_unused]] const NNZInfo<InDims>& nnzInfo) const {

#if (defined(USE_SSSE3) || defined(USE_LSX) || defined(USE_LASX) || (USE_NEON >= 8))
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
    #elif defined(USE_LASX)
        using invec_t  = __m256i;
        using outvec_t = __m256i;
        #define vec_add_32 __lasx_xvadd_w
        #define vec_set_32 __lasx_xvreplgr2vr_w
        #define vec_add_dpbusd_32 SIMD::lasx_m256_add_dpbusd_epi32
    #elif defined(USE_LSX)
        using invec_t  = __m128i;
        using outvec_t = __m128i;
        #define vec_add_32 __lsx_vadd_w
        #define vec_set_32 __lsx_vreplgr2vr_w
        #define vec_add_dpbusd_32 SIMD::lsx_m128_add_dpbusd_epi32
    #endif
        constexpr IndexType OutputSimdWidth = sizeof(outvec_t) / sizeof(OutputType);
        constexpr IndexType NumAccums       = OutputDimensions / OutputSimdWidth;
        // If we're using high-latency dot product instructions, split the accumulators
        // to create 3 separate dependency chains and merge at the end
        constexpr IndexType NumRegs =
    #if defined(USE_VNNI) && defined(USE_AVX512)
          3 * NumAccums;
    #else
          NumAccums;
    #endif

        const outvec_t* biasvec = reinterpret_cast<const outvec_t*>(biases);
        outvec_t        acc[NumRegs];
        for (IndexType k = 0; k < NumAccums; ++k)
            acc[k] = biasvec[k];

        // convince GCC to not do weird pointer arithmetic in the following loops
        const std::int8_t* weights_cp = weights;

    #if defined(USE_AVX512)
        const auto* start = nnzInfo.nnz;
        const auto* end   = nnzInfo.nnz + nnzInfo.count;

        for (IndexType k = NumAccums; k < NumRegs; ++k)
            acc[k] = vec_zero();
        #if defined(USE_VNNI)
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
    #else
        static_assert(InputDimensions % 256 == 0);

        for (IndexType k = 0; k < InputDimensions / 256; ++k)
        {
            uint64_t  bits = load_as<uint64_t>(nnzInfo.bitset + k * 8);
            ptrdiff_t base = k * 64;

            auto* base_addr    = input + base * sizeof(std::int32_t);
            auto* weights_base = &weights_cp[base * OutputDimensions * ChunkSize];

        #if defined(USE_NEON_DOTPROD) && defined(__GNUC__) && !defined(__clang__)
            // GCC 15 pessimizes the following code on ARM64 by eliding the intermediate
            // computation of key pointers (base_addr, weights_base, col, input_addr), leading
            // to a lot of redundant indexing arithmetic in the while (bits) loop. The
            // optimization barriers force these pointers to be calculated and used.
            #if __GNUC__ >= 15
                #define FIX_GCC15_MISOPTIMIZATION
            #endif
        #endif

        #ifdef FIX_GCC15_MISOPTIMIZATION
            asm("" : "+r"(base_addr), "+r"(weights_base));  // opt barrier
        #endif

            while (bits)
            {
                ptrdiff_t   i          = pop_lsb(bits);
                const auto* input_addr = base_addr + i * sizeof(std::int32_t);
                auto        col =
                  reinterpret_cast<const invec_t*>(&weights_base[i * OutputDimensions * ChunkSize]);

        #ifdef FIX_GCC15_MISOPTIMIZATION
                asm("" : "+r"(col), "+r"(input_addr));
            #undef FIX_GCC15_MISOPTIMIZATION
        #endif

                const invec_t in = vec_set_32(load_as<std::int32_t>(input_addr));
                for (IndexType l = 0; l < NumAccums; ++l)
                    vec_add_dpbusd_32(acc[l], in, col[l]);
            }
        }
    #endif
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
