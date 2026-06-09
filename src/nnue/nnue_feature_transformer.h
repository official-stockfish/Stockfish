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

// A class that converts the input features of the NNUE evaluation function

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <iterator>

#include "../position.h"
#include "../types.h"
#include "nnue_accumulator.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "simd.h"

namespace Stockfish::Eval::NNUE {

// Returns the inverse of a permutation
template<usize Len>
constexpr std::array<usize, Len> invert_permutation(const std::array<usize, Len>& order) {
    std::array<usize, Len> inverse{};
    for (usize i = 0; i < order.size(); i++)
        inverse[order[i]] = i;
    return inverse;
}

// Divide a byte region of size TotalSize to chunks of size
// BlockSize, and permute the blocks by a given order
template<usize BlockSize, typename T, usize N, usize OrderSize>
void permute(std::array<T, N>& data, const std::array<usize, OrderSize>& order) {
    constexpr usize TotalSize = N * sizeof(T);

    static_assert(TotalSize % (BlockSize * OrderSize) == 0,
                  "ChunkSize * OrderSize must perfectly divide TotalSize");

    constexpr usize ProcessChunkSize = BlockSize * OrderSize;

    std::array<std::byte, ProcessChunkSize> buffer{};

    std::byte* const bytes = reinterpret_cast<std::byte*>(data.data());

    for (usize i = 0; i < TotalSize; i += ProcessChunkSize)
    {
        std::byte* const values = &bytes[i];

        for (usize j = 0; j < OrderSize; j++)
        {
            auto* const buffer_chunk = &buffer[j * BlockSize];
            auto* const value_chunk  = &values[order[j] * BlockSize];

            std::copy(value_chunk, value_chunk + BlockSize, buffer_chunk);
        }

        std::copy(std::begin(buffer), std::end(buffer), values);
    }
}

// Input feature converter
class FeatureTransformer {
    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = L1;

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType ThreatInputDimensions = ThreatFeatureSet::Dimensions;
    static constexpr IndexType InputDimensions  = PSQFeatureSet::Dimensions + ThreatInputDimensions;
    static constexpr IndexType OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr usize BufferSize = OutputDimensions * sizeof(OutputType);

    // Store the order by which 128-bit blocks of a 1024-bit data must
    // be permuted so that calling packus on adjacent vectors of 16-bit
    // integers loaded from the data results in the pre-permutation order
    static constexpr auto PackusEpi16Order = []() -> std::array<usize, 8> {
#if defined(USE_AVX512)
        // _mm512_packus_epi16 after permutation:
        // |   0   |   2   |   4   |   6   | // Vector 0
        // |   1   |   3   |   5   |   7   | // Vector 1
        // | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | // Packed Result
        return {0, 2, 4, 6, 1, 3, 5, 7};
#elif defined(USE_AVX2) || defined(USE_LASX)
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

    static constexpr u32 combine_hash(std::initializer_list<u32> hashes) {
        u32 hash = 0;
        for (const auto component_hash : hashes)
        {
            hash = (hash << 1) | (hash >> 31);
            hash ^= component_hash;
        }
        return hash;
    }

    // Hash value embedded in the evaluation file
    static constexpr u32 get_hash_value() {
        return combine_hash({ThreatFeatureSet::HashValue, PSQFeatureSet::HashValue})
             ^ (OutputDimensions * 2);
    }

    void permute_weights() {
        permute<16>(biases, PackusEpi16Order);
        permute<16>(weights, PackusEpi16Order);

        permute<8>(threatWeights, PackusEpi16Order);
    }

    void unpermute_weights() {
        permute<16>(biases, InversePackusEpi16Order);
        permute<16>(weights, InversePackusEpi16Order);
        permute<8>(threatWeights, InversePackusEpi16Order);
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
        read_leb_128(stream, biases);

        read_little_endian<ThreatWeightType>(stream, threatWeights.data(),
                                             ThreatInputDimensions * HalfDimensions);
        read_leb_128(stream, threatPsqtWeights);

        read_leb_128(stream, weights);
        read_leb_128(stream, psqtWeights);

        permute_weights();

        return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
        std::unique_ptr<FeatureTransformer> copy = std::make_unique<FeatureTransformer>(*this);

        copy->unpermute_weights();

        write_leb_128<BiasType>(stream, copy->biases);


        write_little_endian<ThreatWeightType>(stream, copy->threatWeights.data(),
                                              ThreatInputDimensions * HalfDimensions);
        write_leb_128<PSQTWeightType>(stream, copy->threatPsqtWeights);

        write_leb_128<WeightType>(stream, copy->weights);
        write_leb_128<PSQTWeightType>(stream, copy->psqtWeights);

        return !stream.fail();
    }

    usize get_content_hash() const {
        usize h = 0;

        hash_combine(h, get_raw_data_hash(biases));
        hash_combine(h, get_raw_data_hash(weights));
        hash_combine(h, get_raw_data_hash(psqtWeights));

        hash_combine(h, get_raw_data_hash(threatWeights));
        hash_combine(h, get_raw_data_hash(threatPsqtWeights));

        hash_combine(h, get_hash_value());

        return h;
    }

    // Convert input features
    i32 transform(const Position&            pos,
                  AccumulatorStack&          accumulatorStack,
                  AccumulatorCaches&         cache,
                  OutputType*                output,
                  int                        bucket,
                  NNZInfo<OutputDimensions>& nnzInfo) const {

        using namespace SIMD;
        accumulatorStack.evaluate(pos, *this, cache);
        const auto& accumulatorState = accumulatorStack.latest();

        const Color perspectives[2]  = {pos.side_to_move(), ~pos.side_to_move()};
        const auto& psqtAccumulation = accumulatorState.psqtAccumulation;
        const auto  psqt =
          (psqtAccumulation[perspectives[0]][bucket] - psqtAccumulation[perspectives[1]][bucket])
          / 2;

        const auto& accumulation = accumulatorState.accumulation;

        for (IndexType p = 0; p < 2; ++p)
        {
            const IndexType offset = (HalfDimensions / 2) * p;

            [[maybe_unused]] auto cursor = nnzInfo.make_cursor(p);

#if defined(VECTOR)

            constexpr IndexType OutputChunkSize = MaxChunkSize;
            static_assert((HalfDimensions / 2) % OutputChunkSize == 0);
            constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

            [[maybe_unused]] const vec_t   Zero  = vec_zero();
            [[maybe_unused]] const vec_t   FtMax = vec_set_16(FtMaxVal);
            [[maybe_unused]] constexpr int shift = 7;

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

            for (IndexType j = 0; j < NumOutputChunks; j += 2)
            {
                vec_t packed[2];
                for (IndexType k = 0; k < 2; ++k)
                {
                    const IndexType i = (j + k) * 2;

                    vec_t acc0a = in0[i + 0];
                    vec_t acc0b = in0[i + 1];
                    vec_t acc1a = in1[i + 0];
                    vec_t acc1b = in1[i + 1];

                    static_assert(FtMaxVal == 255);

    #if defined(USE_NEON)
                    uint16x8_t mul0 = vmull_u8(vqmovun_s16(acc0a), vqmovun_s16(acc1a));
                    uint16x8_t mul1 = vmull_u8(vqmovun_s16(acc0b), vqmovun_s16(acc1b));

                    uint8x16x2_t uzp =
                      vuzpq_u8(vreinterpretq_u8_u16(mul0), vreinterpretq_u8_u16(mul1));
                    uint8x16_t pab    = vshrq_n_u8(uzp.val[1], 1);
                    vec_t      result = reinterpret_cast<vec_t>(pab);
    #elif defined(USE_LSX) || defined(USE_LASX)
                    vec_t pa = vec_packus_16(acc0a, acc0b);
                    vec_t pb = vec_packus_16(acc1a, acc1b);

                    vec_t hi     = vec_mulhi_8(pa, pb);
                    vec_t result = vec_srli_8(hi, 1);
    #elif defined(__wasm__)
                    // _mm_mulhi_epi16 is lowered to 32-bit multiplies, so we take
                    // a similar approach as the NEON path.
                    vec_t mul0 = vec_packus_16(acc0a, acc0b);
                    vec_t mul1 = vec_packus_16(acc1a, acc1b);

                    vec_t low = wasm_u16x8_extmul_low_u8x16(mul0, mul1);
                    vec_t hi  = wasm_u16x8_extmul_high_u8x16(mul0, mul1);

                    // equivalent to vuzp2_u8
                    vec_t merged = wasm_i8x16_shuffle(low, hi, 1, 3, 5, 7, 9, 11, 13, 15, 17, 19,
                                                      21, 23, 25, 27, 29, 31);
                    vec_t result = wasm_u8x16_shr(merged, 1);
    #else
                    vec_t sum0a = vec_slli_16(vec_max_16(vec_min_16(acc0a, FtMax), Zero), shift);
                    vec_t sum0b = vec_slli_16(vec_max_16(vec_min_16(acc0b, FtMax), Zero), shift);
                    vec_t sum1a = vec_min_16(acc1a, FtMax);
                    vec_t sum1b = vec_min_16(acc1b, FtMax);

                    vec_t pa = vec_mulhi_16(sum0a, sum1a);
                    vec_t pb = vec_mulhi_16(sum0b, sum1b);

                    vec_t result = vec_packus_16(pa, pb);
    #endif

                    packed[k] = out[j + k] = result;
                }

                cursor.record2(packed[0], packed[1]);
            }

#else

            for (IndexType j = 0; j < HalfDimensions / 2; ++j)
            {
                BiasType sum0 = accumulation[static_cast<int>(perspectives[p])][j + 0];
                BiasType sum1 =
                  accumulation[static_cast<int>(perspectives[p])][j + HalfDimensions / 2];

                sum0 = std::clamp<BiasType>(sum0, 0, FtMaxVal);
                sum1 = std::clamp<BiasType>(sum1, 0, FtMaxVal);

                output[offset + j] = static_cast<OutputType>(unsigned(sum0 * sum1) / 512);
            }

#endif
        }

        return psqt;
    }  // end of function transform()

    alignas(CacheLineSize) std::array<BiasType, HalfDimensions> biases;
    alignas(
      CacheLineSize) std::array<WeightType, HalfDimensions * PSQFeatureSet::Dimensions> weights;
    alignas(CacheLineSize)
      std::array<ThreatWeightType, HalfDimensions * ThreatFeatureSet::Dimensions> threatWeights;
    alignas(CacheLineSize)
      std::array<PSQTWeightType, PSQFeatureSet::Dimensions * PSQTBuckets> psqtWeights;
    alignas(CacheLineSize)
      std::array<PSQTWeightType, ThreatFeatureSet::Dimensions * PSQTBuckets> threatPsqtWeights;
};

}  // namespace Stockfish::Eval::NNUE

template<>
struct std::hash<Stockfish::Eval::NNUE::FeatureTransformer> {
    Stockfish::usize
    operator()(const Stockfish::Eval::NNUE::FeatureTransformer& ft) const noexcept {
        return ft.get_content_hash();
    }
};

#endif  // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
