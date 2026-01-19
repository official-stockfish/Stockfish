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
void permute(std::array<T, N>& data, const std::array<std::size_t, OrderSize>& order) {
    constexpr std::size_t TotalSize = N * sizeof(T);

    static_assert(TotalSize % (BlockSize * OrderSize) == 0,
                  "ChunkSize * OrderSize must perfectly divide TotalSize");

    constexpr std::size_t ProcessChunkSize = BlockSize * OrderSize;

    std::array<std::byte, ProcessChunkSize> buffer{};

    std::byte* const bytes = reinterpret_cast<std::byte*>(data.data());

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

// Input feature converter
template<IndexType TransformedFeatureDimensions>
class FeatureTransformer {
    static constexpr bool UseThreats =
      (TransformedFeatureDimensions == TransformedFeatureDimensionsBig);
    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions       = PSQFeatureSet::Dimensions;
    static constexpr IndexType ThreatInputDimensions = ThreatFeatureSet::Dimensions;
    static constexpr IndexType TotalInputDimensions =
      InputDimensions + (UseThreats ? ThreatInputDimensions : 0);
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
        return (UseThreats ? ThreatFeatureSet::HashValue : PSQFeatureSet::HashValue)
             ^ (OutputDimensions * 2);
    }

    void permute_weights() {
        permute<16>(biases, PackusEpi16Order);
        permute<16>(weights, PackusEpi16Order);

        if constexpr (UseThreats)
            permute<8>(threatWeights, PackusEpi16Order);
    }

    void unpermute_weights() {
        permute<16>(biases, InversePackusEpi16Order);
        permute<16>(weights, InversePackusEpi16Order);

        if constexpr (UseThreats)
            permute<8>(threatWeights, InversePackusEpi16Order);
    }

    inline void scale_weights(bool read) {
        for (auto& w : weights)
            w = read ? w * 2 : w / 2;
        for (auto& b : biases)
            b = read ? b * 2 : b / 2;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
        read_leb_128(stream, biases);

        if constexpr (UseThreats)
        {
            read_little_endian<ThreatWeightType>(stream, threatWeights.data(),
                                                 ThreatInputDimensions * HalfDimensions);
            read_leb_128(stream, weights);

            read_leb_128(stream, threatPsqtWeights, psqtWeights);
        }
        else
        {
            read_leb_128(stream, weights);
            read_leb_128(stream, psqtWeights);
        }

        permute_weights();

        if constexpr (!UseThreats)
            scale_weights(true);

        return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {
        std::unique_ptr<FeatureTransformer> copy = std::make_unique<FeatureTransformer>(*this);

        copy->unpermute_weights();

        if constexpr (!UseThreats)
            copy->scale_weights(false);

        write_leb_128<BiasType>(stream, copy->biases);

        if constexpr (UseThreats)
        {
            write_little_endian<ThreatWeightType>(stream, copy->threatWeights.data(),
                                                  ThreatInputDimensions * HalfDimensions);
            write_leb_128<WeightType>(stream, copy->weights);

            auto combinedPsqtWeights =
              std::make_unique<std::array<PSQTWeightType, TotalInputDimensions * PSQTBuckets>>();

            std::copy(std::begin(copy->threatPsqtWeights),
                      std::begin(copy->threatPsqtWeights) + ThreatInputDimensions * PSQTBuckets,
                      combinedPsqtWeights->begin());

            std::copy(std::begin(copy->psqtWeights),
                      std::begin(copy->psqtWeights) + InputDimensions * PSQTBuckets,
                      combinedPsqtWeights->begin() + ThreatInputDimensions * PSQTBuckets);

            write_leb_128<PSQTWeightType>(stream, *combinedPsqtWeights);
        }
        else
        {
            write_leb_128<WeightType>(stream, copy->weights);
            write_leb_128<PSQTWeightType>(stream, copy->psqtWeights);
        }

        return !stream.fail();
    }

    std::size_t get_content_hash() const {
        std::size_t h = 0;
        hash_combine(h, get_raw_data_hash(biases));
        hash_combine(h, get_raw_data_hash(weights));
        hash_combine(h, get_raw_data_hash(psqtWeights));
        hash_combine(h, get_hash_value());
        return h;
    }

    // Convert input features
    std::int32_t transform(const Position&                           pos,
                           AccumulatorStack&                         accumulatorStack,
                           AccumulatorCaches::Cache<HalfDimensions>& cache,
                           OutputType*                               output,
                           int                                       bucket) const {

        using namespace SIMD;
        accumulatorStack.evaluate(pos, *this, cache);
        const auto& accumulatorState       = accumulatorStack.latest<PSQFeatureSet>();
        const auto& threatAccumulatorState = accumulatorStack.latest<ThreatFeatureSet>();

        const Color perspectives[2]  = {pos.side_to_move(), ~pos.side_to_move()};
        const auto& psqtAccumulation = (accumulatorState.acc<HalfDimensions>()).psqtAccumulation;
        auto        psqt =
          (psqtAccumulation[perspectives[0]][bucket] - psqtAccumulation[perspectives[1]][bucket]);

        if constexpr (UseThreats)
        {
            const auto& threatPsqtAccumulation =
              (threatAccumulatorState.acc<HalfDimensions>()).psqtAccumulation;
            psqt = (psqt + threatPsqtAccumulation[perspectives[0]][bucket]
                    - threatPsqtAccumulation[perspectives[1]][bucket])
                 / 2;
        }
        else
            psqt /= 2;

        const auto& accumulation = (accumulatorState.acc<HalfDimensions>()).accumulation;
        const auto& threatAccumulation =
          (threatAccumulatorState.acc<HalfDimensions>()).accumulation;

        for (IndexType p = 0; p < 2; ++p)
        {
            const IndexType offset = (HalfDimensions / 2) * p;

#if defined(VECTOR)

            constexpr IndexType OutputChunkSize = MaxChunkSize;
            static_assert((HalfDimensions / 2) % OutputChunkSize == 0);
            constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

            const vec_t Zero = vec_zero();
            const vec_t One  = vec_set_16(UseThreats ? 255 : 127 * 2);

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
            if constexpr (UseThreats)
            {
                const vec_t* tin0 =
                  reinterpret_cast<const vec_t*>(&(threatAccumulation[perspectives[p]][0]));
                const vec_t* tin1 = reinterpret_cast<const vec_t*>(
                  &(threatAccumulation[perspectives[p]][HalfDimensions / 2]));
                for (IndexType j = 0; j < NumOutputChunks; ++j)
                {
                    const vec_t acc0a = vec_add_16(in0[j * 2 + 0], tin0[j * 2 + 0]);
                    const vec_t acc0b = vec_add_16(in0[j * 2 + 1], tin0[j * 2 + 1]);
                    const vec_t acc1a = vec_add_16(in1[j * 2 + 0], tin1[j * 2 + 0]);
                    const vec_t acc1b = vec_add_16(in1[j * 2 + 1], tin1[j * 2 + 1]);

                    const vec_t sum0a =
                      vec_slli_16(vec_max_16(vec_min_16(acc0a, One), Zero), shift);
                    const vec_t sum0b =
                      vec_slli_16(vec_max_16(vec_min_16(acc0b, One), Zero), shift);
                    const vec_t sum1a = vec_min_16(acc1a, One);
                    const vec_t sum1b = vec_min_16(acc1b, One);

                    const vec_t pa = vec_mulhi_16(sum0a, sum1a);
                    const vec_t pb = vec_mulhi_16(sum0b, sum1b);

                    out[j] = vec_packus_16(pa, pb);
                }
            }
            else
            {
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
            }

#else

            for (IndexType j = 0; j < HalfDimensions / 2; ++j)
            {
                BiasType sum0 = accumulation[static_cast<int>(perspectives[p])][j + 0];
                BiasType sum1 =
                  accumulation[static_cast<int>(perspectives[p])][j + HalfDimensions / 2];

                if constexpr (UseThreats)
                {
                    BiasType sum0t = threatAccumulation[static_cast<int>(perspectives[p])][j + 0];
                    BiasType sum1t =
                      threatAccumulation[static_cast<int>(perspectives[p])][j + HalfDimensions / 2];
                    sum0 = std::clamp<BiasType>(sum0 + sum0t, 0, 255);
                    sum1 = std::clamp<BiasType>(sum1 + sum1t, 0, 255);
                }
                else
                {
                    sum0 = std::clamp<BiasType>(sum0, 0, 127 * 2);
                    sum1 = std::clamp<BiasType>(sum1, 0, 127 * 2);
                }

                output[offset + j] = static_cast<OutputType>(unsigned(sum0 * sum1) / 512);
            }

#endif
        }

        return psqt;
    }  // end of function transform()

    alignas(CacheLineSize) std::array<BiasType, HalfDimensions> biases;
    alignas(CacheLineSize) std::array<WeightType, HalfDimensions * InputDimensions> weights;
    alignas(CacheLineSize)
      std::array<ThreatWeightType,
                 UseThreats ? HalfDimensions * ThreatInputDimensions : 0> threatWeights;
    alignas(CacheLineSize) std::array<PSQTWeightType, InputDimensions * PSQTBuckets> psqtWeights;
    alignas(CacheLineSize)
      std::array<PSQTWeightType,
                 UseThreats ? ThreatInputDimensions * PSQTBuckets : 0> threatPsqtWeights;
};

}  // namespace Stockfish::Eval::NNUE


template<Stockfish::Eval::NNUE::IndexType TransformedFeatureDimensions>
struct std::hash<Stockfish::Eval::NNUE::FeatureTransformer<TransformedFeatureDimensions>> {
    std::size_t
    operator()(const Stockfish::Eval::NNUE::FeatureTransformer<TransformedFeatureDimensions>& ft)
      const noexcept {
        return ft.get_content_hash();
    }
};

#endif  // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
