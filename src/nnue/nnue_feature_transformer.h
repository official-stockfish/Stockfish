/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

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

// A class that converts the input features of the NNUE evaluation function

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iosfwd>

#include "../position.h"
#include "../types.h"
#include "nnue_accumulator.h"
#include "nnue_architecture.h"
#include "nnue_common.h"

namespace Stockfish::Eval::NNUE {

using BiasType       = std::int16_t;
using WeightType     = std::int16_t;
using PSQTWeightType = std::int32_t;

// If vector instructions are enabled, we update and refresh the
// accumulator tile by tile such that each tile fits in the CPU's
// vector registers.
#define VECTOR

static_assert(PSQTBuckets % 8 == 0,
              "Per feature PSQT values cannot be processed at granularity lower than 8 at a time.");

#ifdef USE_AVX512
using vec_t      = __m512i;
using psqt_vec_t = __m256i;
    #define vec_load(a) _mm512_load_si512(a)
    #define vec_store(a, b) _mm512_store_si512(a, b)
    #define vec_add_16(a, b) _mm512_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm512_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm512_mulhi_epi16(a, b)
    #define vec_zero() _mm512_setzero_epi32()
    #define vec_set_16(a) _mm512_set1_epi16(a)
    #define vec_max_16(a, b) _mm512_max_epi16(a, b)
    #define vec_min_16(a, b) _mm512_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm512_slli_epi16(a, b)
    // Inverse permuted at load time
    #define vec_packus_16(a, b) _mm512_packus_epi16(a, b)
    #define vec_load_psqt(a) _mm256_load_si256(a)
    #define vec_store_psqt(a, b) _mm256_store_si256(a, b)
    #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
    #define vec_zero_psqt() _mm256_setzero_si256()
    #define NumRegistersSIMD 16
    #define MaxChunkSize 64

#elif USE_AVX2
using vec_t      = __m256i;
using psqt_vec_t = __m256i;
    #define vec_load(a) _mm256_load_si256(a)
    #define vec_store(a, b) _mm256_store_si256(a, b)
    #define vec_add_16(a, b) _mm256_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm256_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm256_mulhi_epi16(a, b)
    #define vec_zero() _mm256_setzero_si256()
    #define vec_set_16(a) _mm256_set1_epi16(a)
    #define vec_max_16(a, b) _mm256_max_epi16(a, b)
    #define vec_min_16(a, b) _mm256_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm256_slli_epi16(a, b)
    // Inverse permuted at load time
    #define vec_packus_16(a, b) _mm256_packus_epi16(a, b)
    #define vec_load_psqt(a) _mm256_load_si256(a)
    #define vec_store_psqt(a, b) _mm256_store_si256(a, b)
    #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
    #define vec_zero_psqt() _mm256_setzero_si256()
    #define NumRegistersSIMD 16
    #define MaxChunkSize 32

#elif USE_SSE2
using vec_t      = __m128i;
using psqt_vec_t = __m128i;
    #define vec_load(a) (*(a))
    #define vec_store(a, b) *(a) = (b)
    #define vec_add_16(a, b) _mm_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm_mulhi_epi16(a, b)
    #define vec_zero() _mm_setzero_si128()
    #define vec_set_16(a) _mm_set1_epi16(a)
    #define vec_max_16(a, b) _mm_max_epi16(a, b)
    #define vec_min_16(a, b) _mm_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm_slli_epi16(a, b)
    #define vec_packus_16(a, b) _mm_packus_epi16(a, b)
    #define vec_load_psqt(a) (*(a))
    #define vec_store_psqt(a, b) *(a) = (b)
    #define vec_add_psqt_32(a, b) _mm_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm_sub_epi32(a, b)
    #define vec_zero_psqt() _mm_setzero_si128()
    #define NumRegistersSIMD (Is64Bit ? 16 : 8)
    #define MaxChunkSize 16

#elif USE_NEON
using vec_t      = int16x8_t;
using psqt_vec_t = int32x4_t;
    #define vec_load(a) (*(a))
    #define vec_store(a, b) *(a) = (b)
    #define vec_add_16(a, b) vaddq_s16(a, b)
    #define vec_sub_16(a, b) vsubq_s16(a, b)
    #define vec_mulhi_16(a, b) vqdmulhq_s16(a, b)
    #define vec_zero() \
        vec_t { 0 }
    #define vec_set_16(a) vdupq_n_s16(a)
    #define vec_max_16(a, b) vmaxq_s16(a, b)
    #define vec_min_16(a, b) vminq_s16(a, b)
    #define vec_slli_16(a, b) vshlq_s16(a, vec_set_16(b))
    #define vec_packus_16(a, b) reinterpret_cast<vec_t>(vcombine_u8(vqmovun_s16(a), vqmovun_s16(b)))
    #define vec_load_psqt(a) (*(a))
    #define vec_store_psqt(a, b) *(a) = (b)
    #define vec_add_psqt_32(a, b) vaddq_s32(a, b)
    #define vec_sub_psqt_32(a, b) vsubq_s32(a, b)
    #define vec_zero_psqt() \
        psqt_vec_t { 0 }
    #define NumRegistersSIMD 16
    #define MaxChunkSize 16

#else
    #undef VECTOR

#endif

// Returns the inverse of a permutation
template<std::size_t Len>
constexpr std::array<std::size_t, Len>
invert_permutation(const std::array<std::size_t, Len>& order) {
    std::array<std::size_t, Len> inverse{};
    for (std::size_t i = 0; i < order.size(); i++)
        inverse[order[i]] = i;
    return inverse;
}

// Divide a byte region of size TotalSize to chunks of size
// BlockSize, and permute the blocks by a given order
template<std::size_t BlockSize, typename T, std::size_t N, std::size_t OrderSize>
void permute(T (&data)[N], const std::array<std::size_t, OrderSize>& order) {
    constexpr std::size_t TotalSize = N * sizeof(T);

    static_assert(TotalSize % (BlockSize * OrderSize) == 0,
                  "ChunkSize * OrderSize must perfectly divide TotalSize");

    constexpr std::size_t ProcessChunkSize = BlockSize * OrderSize;

    std::array<std::byte, ProcessChunkSize> buffer{};

    std::byte* const bytes = reinterpret_cast<std::byte*>(data);

    for (std::size_t i = 0; i < TotalSize; i += ProcessChunkSize)
    {
        std::byte* const values = &bytes[i];

        for (std::size_t j = 0; j < OrderSize; j++)
        {
            auto* const buffer_chunk = &buffer[j * BlockSize];
            auto* const value_chunk  = &values[order[j] * BlockSize];

            std::copy(value_chunk, value_chunk + BlockSize, buffer_chunk);
        }

        std::copy(std::begin(buffer), std::end(buffer), values);
    }
}

// Compute optimal SIMD register count for feature transformer accumulation.
template<IndexType TransformedFeatureWidth, IndexType HalfDimensions>
class SIMDTiling {
#ifdef VECTOR
    // We use __m* types as template arguments, which causes GCC to emit warnings
    // about losing some attribute information. This is irrelevant to us as we
    // only take their size, so the following pragma are harmless.
    #if defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wignored-attributes"
    #endif

    template<typename SIMDRegisterType, typename LaneType, int NumLanes, int MaxRegisters>
    static constexpr int BestRegisterCount() {
        constexpr std::size_t RegisterSize = sizeof(SIMDRegisterType);
        constexpr std::size_t LaneSize     = sizeof(LaneType);

        static_assert(RegisterSize >= LaneSize);
        static_assert(MaxRegisters <= NumRegistersSIMD);
        static_assert(MaxRegisters > 0);
        static_assert(NumRegistersSIMD > 0);
        static_assert(RegisterSize % LaneSize == 0);
        static_assert((NumLanes * LaneSize) % RegisterSize == 0);

        const int ideal = (NumLanes * LaneSize) / RegisterSize;
        if (ideal <= MaxRegisters)
            return ideal;

        // Look for the largest divisor of the ideal register count that is smaller than MaxRegisters
        for (int divisor = MaxRegisters; divisor > 1; --divisor)
            if (ideal % divisor == 0)
                return divisor;

        return 1;
    }

    #if defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif

   public:
    static constexpr int NumRegs =
      BestRegisterCount<vec_t, WeightType, TransformedFeatureWidth, NumRegistersSIMD>();
    static constexpr int NumPsqtRegs =
      BestRegisterCount<psqt_vec_t, PSQTWeightType, PSQTBuckets, NumRegistersSIMD>();

    static constexpr IndexType TileHeight     = NumRegs * sizeof(vec_t) / 2;
    static constexpr IndexType PsqtTileHeight = NumPsqtRegs * sizeof(psqt_vec_t) / 4;

    static_assert(HalfDimensions % TileHeight == 0, "TileHeight must divide HalfDimensions");
    static_assert(PSQTBuckets % PsqtTileHeight == 0, "PsqtTileHeight must divide PSQTBuckets");
#endif
};


// Input feature converter
template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> AccumulatorState::*accPtr>
class FeatureTransformer {

    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = FeatureSet::Dimensions;
    static constexpr IndexType OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr std::size_t BufferSize = OutputDimensions * sizeof(OutputType);

    // Store the order by which 128-bit blocks of a 1024-bit data must
    // be permuted so that calling packus on adjacent vectors of 16-bit
    // integers loaded from the data results in the pre-permutation order
    static constexpr auto PackusEpi16Order = []() -> std::array<std::size_t, 8> {
#if defined(USE_AVX512)
        // _mm512_packus_epi16 after permutation:
        // |   0   |   2   |   4   |   6   | // Vector 0
        // |   1   |   3   |   5   |   7   | // Vector 1
        // | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | // Packed Result
        return {0, 2, 4, 6, 1, 3, 5, 7};
#elif defined(USE_AVX2)
        // _mm256_packus_epi16 after permutation:
        // |   0   |   2   |  |   4   |   6   | // Vector 0, 2
        // |   1   |   3   |  |   5   |   7   | // Vector 1, 3
        // | 0 | 1 | 2 | 3 |  | 4 | 5 | 6 | 7 | // Packed Result
        return {0, 2, 1, 3, 4, 6, 5, 7};
#else
        return {0, 1, 2, 3, 4, 5, 6, 7};
#endif
    }();

    static constexpr auto InversePackusEpi16Order = invert_permutation(PackusEpi16Order);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() {
        return FeatureSet::HashValue ^ (OutputDimensions * 2);
    }

    void permute_weights() {
        permute<16>(biases, PackusEpi16Order);
        permute<16>(weights, PackusEpi16Order);
    }

    void unpermute_weights() {
        permute<16>(biases, InversePackusEpi16Order);
        permute<16>(weights, InversePackusEpi16Order);
    }

    inline void scale_weights(bool read) {
        for (IndexType j = 0; j < InputDimensions; ++j)
        {
            WeightType* w = &weights[j * HalfDimensions];
            for (IndexType i = 0; i < HalfDimensions; ++i)
                w[i] = read ? w[i] * 2 : w[i] / 2;
        }

        for (IndexType i = 0; i < HalfDimensions; ++i)
            biases[i] = read ? biases[i] * 2 : biases[i] / 2;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {

        read_leb_128<BiasType>(stream, biases, HalfDimensions);
        read_leb_128<WeightType>(stream, weights, HalfDimensions * InputDimensions);
        read_leb_128<PSQTWeightType>(stream, psqtWeights, PSQTBuckets * InputDimensions);

        permute_weights();
        scale_weights(true);
        return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) {

        unpermute_weights();
        scale_weights(false);

        write_leb_128<BiasType>(stream, biases, HalfDimensions);
        write_leb_128<WeightType>(stream, weights, HalfDimensions * InputDimensions);
        write_leb_128<PSQTWeightType>(stream, psqtWeights, PSQTBuckets * InputDimensions);

        permute_weights();
        scale_weights(true);
        return !stream.fail();
    }

    // Convert input features
    std::int32_t transform(const Position&                           pos,
                           AccumulatorStack&                         accumulatorStack,
                           AccumulatorCaches::Cache<HalfDimensions>* cache,
                           OutputType*                               output,
                           int                                       bucket) const {

        accumulatorStack.evaluate(pos, *this, *cache);
        const auto& accumulatorState = accumulatorStack.latest();

        const Color perspectives[2]  = {pos.side_to_move(), ~pos.side_to_move()};
        const auto& psqtAccumulation = (accumulatorState.*accPtr).psqtAccumulation;
        const auto  psqt =
          (psqtAccumulation[perspectives[0]][bucket] - psqtAccumulation[perspectives[1]][bucket])
          / 2;

        const auto& accumulation = (accumulatorState.*accPtr).accumulation;

        for (IndexType p = 0; p < 2; ++p)
        {
            const IndexType offset = (HalfDimensions / 2) * p;

#if defined(VECTOR)

            constexpr IndexType OutputChunkSize = MaxChunkSize;
            static_assert((HalfDimensions / 2) % OutputChunkSize == 0);
            constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

            const vec_t Zero = vec_zero();
            const vec_t One  = vec_set_16(127 * 2);

            const vec_t* in0 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][0]));
            const vec_t* in1 =
              reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
            vec_t* out = reinterpret_cast<vec_t*>(output + offset);

            // Per the NNUE architecture, here we want to multiply pairs of
            // clipped elements and divide the product by 128. To do this,
            // we can naively perform min/max operation to clip each of the
            // four int16 vectors, mullo pairs together, then pack them into
            // one int8 vector. However, there exists a faster way.

            // The idea here is to use the implicit clipping from packus to
            // save us two vec_max_16 instructions. This clipping works due
            // to the fact that any int16 integer below zero will be zeroed
            // on packus.

            // Consider the case where the second element is negative.
            // If we do standard clipping, that element will be zero, which
            // means our pairwise product is zero. If we perform packus and
            // remove the lower-side clip for the second element, then our
            // product before packus will be negative, and is zeroed on pack.
            // The two operation produce equivalent results, but the second
            // one (using packus) saves one max operation per pair.

            // But here we run into a problem: mullo does not preserve the
            // sign of the multiplication. We can get around this by doing
            // mulhi, which keeps the sign. But that requires an additional
            // tweak.

            // mulhi cuts off the last 16 bits of the resulting product,
            // which is the same as performing a rightward shift of 16 bits.
            // We can use this to our advantage. Recall that we want to
            // divide the final product by 128, which is equivalent to a
            // 7-bit right shift. Intuitively, if we shift the clipped
            // value left by 9, and perform mulhi, which shifts the product
            // right by 16 bits, then we will net a right shift of 7 bits.
            // However, this won't work as intended. Since we clip the
            // values to have a maximum value of 127, shifting it by 9 bits
            // might occupy the signed bit, resulting in some positive
            // values being interpreted as negative after the shift.

            // There is a way, however, to get around this limitation. When
            // loading the network, scale accumulator weights and biases by
            // 2. To get the same pairwise multiplication result as before,
            // we need to divide the product by 128 * 2 * 2 = 512, which
            // amounts to a right shift of 9 bits. So now we only have to
            // shift left by 7 bits, perform mulhi (shifts right by 16 bits)
            // and net a 9 bit right shift. Since we scaled everything by
            // two, the values are clipped at 127 * 2 = 254, which occupies
            // 8 bits. Shifting it by 7 bits left will no longer occupy the
            // signed bit, so we are safe.

            // Note that on NEON processors, we shift left by 6 instead
            // because the instruction "vqdmulhq_s16" also doubles the
            // return value after the multiplication, adding an extra shift
            // to the left by 1, so we compensate by shifting less before
            // the multiplication.

            constexpr int shift =
    #if defined(USE_SSE2)
              7;
    #else
              6;
    #endif

            for (IndexType j = 0; j < NumOutputChunks; ++j)
            {
                const vec_t sum0a =
                  vec_slli_16(vec_max_16(vec_min_16(in0[j * 2 + 0], One), Zero), shift);
                const vec_t sum0b =
                  vec_slli_16(vec_max_16(vec_min_16(in0[j * 2 + 1], One), Zero), shift);
                const vec_t sum1a = vec_min_16(in1[j * 2 + 0], One);
                const vec_t sum1b = vec_min_16(in1[j * 2 + 1], One);

                const vec_t pa = vec_mulhi_16(sum0a, sum1a);
                const vec_t pb = vec_mulhi_16(sum0b, sum1b);

                out[j] = vec_packus_16(pa, pb);
            }

#else

            for (IndexType j = 0; j < HalfDimensions / 2; ++j)
            {
                BiasType sum0 = accumulation[static_cast<int>(perspectives[p])][j + 0];
                BiasType sum1 =
                  accumulation[static_cast<int>(perspectives[p])][j + HalfDimensions / 2];
                sum0               = std::clamp<BiasType>(sum0, 0, 127 * 2);
                sum1               = std::clamp<BiasType>(sum1, 0, 127 * 2);
                output[offset + j] = static_cast<OutputType>(unsigned(sum0 * sum1) / 512);
            }

#endif
        }

        return psqt;
    }  // end of function transform()

    alignas(CacheLineSize) BiasType biases[HalfDimensions];
    alignas(CacheLineSize) WeightType weights[HalfDimensions * InputDimensions];
    alignas(CacheLineSize) PSQTWeightType psqtWeights[InputDimensions * PSQTBuckets];
};

}  // namespace Stockfish::Eval::NNUE

#endif  // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
