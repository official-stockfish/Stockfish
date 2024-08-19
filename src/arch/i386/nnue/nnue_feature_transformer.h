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

#ifndef I386_NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define I386_NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

// Check x86/AMD64 SIMD extensions.
// If none is defined, fall back to the generic implementation.
#ifndef __SSE2__

#include "arch/generic/nnue/nnue_feature_transformer.h"

#else

#include "../arch.h"

#include <algorithm>
#include <cstring>

#include "misc.h"
#include "position.h"
#include "types.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE {

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
struct FeatureTransformer<TransformedFeatureDimensions, accPtr>::Details {
#if defined(__AVX512F__) && defined(__AVX512BW__) && !defined(NO_AVX512)
    // The size of the current PSQT weights array is too small for AVX-512.
    using vec_t      = __m512i;
    using psqt_vec_t = __m256i;
#elif defined(__AVX2__)
    using vec_t      = __m256i;
    using psqt_vec_t = __m256i;
#else
    using vec_t      = __m128i;
    using psqt_vec_t = __m128i;
#endif

   private:
#if defined(__AVX512F__)
    // EVEX enconding scheme, but uses 16 only. Need to check <=32
    static constexpr int NumXMM = 16;
#else
    static constexpr int NumXMM = is_64bit() ? 16 : 8;
#endif

   public:
    static constexpr std::size_t AccRegisterSize  = sizeof(vec_t);
    static constexpr std::size_t PSQTRegisterSize = sizeof(psqt_vec_t);

    static constexpr int OptimalAccRegisterCount =
      optimal_register_count<AccRegisterSize,
                             NumXMM,
                             sizeof(WeightType),
                             TransformedFeatureDimensions>();
    static constexpr int OptimalPSQTRegisterCount =
      optimal_register_count<PSQTRegisterSize, NumXMM, sizeof(PSQTWeightType), PSQTBuckets>();

    static constexpr IndexType TileHeight =
      OptimalAccRegisterCount * AccRegisterSize / sizeof(WeightType);
    static constexpr IndexType PsqtTileHeight =
      OptimalPSQTRegisterCount * PSQTRegisterSize / sizeof(PSQTWeightType);

    static_assert(HalfDimensions % TileHeight == 0,
                  "HalfDimensions must be multiple of TileHeight");
    static_assert(PSQTBuckets % PsqtTileHeight == 0,
                  "PSQTBuckets must be multiple of PsqtTileHeight");
};

template<std::size_t RegisterSize, bool Write>
static inline constexpr void permute_pack(std::uint64_t* v) {
    if constexpr (RegisterSize == 64)
        if constexpr (Write)
        {
            std::uint64_t tmp0 = v[2], tmp1 = v[3];
            v[2] = v[8], v[3] = v[9];
            v[8] = v[4], v[9] = v[5];
            v[4] = tmp0, v[5] = tmp1;
            tmp0 = v[6], tmp1 = v[7];
            v[6] = v[10], v[7] = v[11];
            v[10] = v[12], v[11] = v[13];
            v[12] = tmp0, v[13] = tmp1;
        }
        else
        {
            std::uint64_t tmp0 = v[2], tmp1 = v[3];
            v[2] = v[4], v[3] = v[5];
            v[4] = v[8], v[5] = v[9];
            v[8] = tmp0, v[9] = tmp1;
            tmp0 = v[6], tmp1 = v[7];
            v[6] = v[12], v[7] = v[13];
            v[12] = v[10], v[13] = v[11];
            v[10] = tmp0, v[11] = tmp1;
        }
    else if constexpr (RegisterSize == 32)
    {
        std::swap(v[2], v[4]);
        std::swap(v[3], v[5]);
    }
}

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
template<bool Write>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::permute_weights() {
    // The weight numbers are permuted preliminarily, due to the use of
    // AVX2/AVX-512 pack intrinsics.
    if constexpr (Details::AccRegisterSize >= 32)
    {
        constexpr IndexType Width = Details::AccRegisterSize == 64 ? 16 : 8;

        for (IndexType i = 0; i < HalfDimensions * sizeof(BiasType) / 8; i += Width)
            permute_pack<Details::AccRegisterSize, Write>(
              &reinterpret_cast<std::uint64_t*>(biases)[i]);

        for (IndexType j = 0; j < InputDimensions; ++j)
            for (IndexType i = 0; i < HalfDimensions * sizeof(WeightType) / 8; i += Width)
                permute_pack<Details::AccRegisterSize, Write>(
                  &reinterpret_cast<std::uint64_t*>(&weights[j * HalfDimensions])[i]);
    }
}

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
template<Color Perspective, bool CurrentOnly>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::
  apply_accumulator_updates_incremental(StateInfo*             computed,
                                        StateInfo*             next,
                                        FeatureSet::IndexList& removed,
                                        FeatureSet::IndexList& added) const {
    using vec_t      = typename Details::vec_t;
    using psqt_vec_t = typename Details::psqt_vec_t;

    // The most common case when updating the accumulator incrementally.
    // Calculates feature differences directly without using tiling mechanism.
    if ((removed.size() == 1 || removed.size() == 2) && added.size() == 1)
    {
        const auto accIn =
          reinterpret_cast<const vec_t*>(&(computed->*accPtr).accumulation[Perspective][0]);
        const auto accOut = reinterpret_cast<vec_t*>(&(next->*accPtr).accumulation[Perspective][0]);

        const IndexType offsetR0 = HalfDimensions * removed[0];
        const auto      columnR0 = reinterpret_cast<const vec_t*>(&weights[offsetR0]);
        const IndexType offsetA  = HalfDimensions * added[0];
        const auto      columnA  = reinterpret_cast<const vec_t*>(&weights[offsetA]);

        if (removed.size() == 1)
        {
            for (IndexType i = 0; i < HalfDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                accOut[i] = _mm_add_epi16_v(_mm_sub_epi16_v(accIn[i], columnR0[i]), columnA[i]);
        }
        else
        {
            const IndexType offsetR1 = HalfDimensions * removed[1];
            const auto      columnR1 = reinterpret_cast<const vec_t*>(&weights[offsetR1]);

            for (IndexType i = 0; i < HalfDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                accOut[i] = _mm_sub_epi16_v(_mm_add_epi16_v(accIn[i], columnA[i]),
                                            _mm_add_epi16_v(columnR0[i], columnR1[i]));
        }

        const auto accPsqtIn = reinterpret_cast<const psqt_vec_t*>(
          &(computed->*accPtr).psqtAccumulation[Perspective][0]);
        const auto accPsqtOut =
          reinterpret_cast<psqt_vec_t*>(&(next->*accPtr).psqtAccumulation[Perspective][0]);

        const IndexType offsetPsqtR0 = PSQTBuckets * removed[0];
        auto columnPsqtR0 = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtR0]);
        const IndexType offsetPsqtA = PSQTBuckets * added[0];
        auto columnPsqtA = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtA]);

        if (removed.size() == 1)
        {
            for (IndexType i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t);
                 ++i)
                accPsqtOut[i] =
                  _mm_add_epi32_v(_mm_sub_epi32_v(accPsqtIn[i], columnPsqtR0[i]), columnPsqtA[i]);
        }
        else
        {
            const IndexType offsetPsqtR1 = PSQTBuckets * removed[1];
            const auto      columnPsqtR1 =
              reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtR1]);

            for (IndexType i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t);
                 ++i)
                accPsqtOut[i] = _mm_sub_epi32_v(_mm_add_epi32_v(accPsqtIn[i], columnPsqtA[i]),
                                                _mm_add_epi32_v(columnPsqtR0[i], columnPsqtR1[i]));
        }
    }
    else
    {
        vec_t acc[Details::OptimalAccRegisterCount];

        for (IndexType i = 0; i < HalfDimensions / Details::TileHeight; ++i)
        {
            const IndexType offsetRow = i * Details::TileHeight;

            const auto accTileIn = reinterpret_cast<const vec_t*>(
              &(computed->*accPtr).accumulation[Perspective][offsetRow]);
            for (std::size_t j = 0; j < array_size(acc); ++j)
                acc[j] = accTileIn[j];

            for (const auto index : removed)
            {
                const IndexType offset = HalfDimensions * index + offsetRow;
                const auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);
                for (std::size_t j = 0; j < array_size(acc); ++j)
                    acc[j] = _mm_sub_epi16_v(acc[j], column[j]);
            }

            for (const auto index : added)
            {
                const IndexType offset = HalfDimensions * index + offsetRow;
                const auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);
                for (std::size_t j = 0; j < array_size(acc); ++j)
                    acc[j] = _mm_add_epi16_v(acc[j], column[j]);
            }

            const auto accTileOut =
              reinterpret_cast<vec_t*>(&(next->*accPtr).accumulation[Perspective][offsetRow]);
            for (std::size_t j = 0; j < array_size(acc); ++j)
                accTileOut[j] = acc[j];
        }

        psqt_vec_t psqt[Details::OptimalPSQTRegisterCount];

        for (IndexType i = 0; i < PSQTBuckets / Details::PsqtTileHeight; ++i)
        {
            const IndexType offsetRow = i * Details::PsqtTileHeight;

            auto accTilePsqtIn = reinterpret_cast<const psqt_vec_t*>(
              &(computed->*accPtr).psqtAccumulation[Perspective][offsetRow]);
            for (std::size_t j = 0; j < array_size(psqt); ++j)
                psqt[j] = accTilePsqtIn[j];

            for (const auto index : removed)
            {
                const IndexType offset = PSQTBuckets * index + offsetRow;
                auto columnPsqt        = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
                for (std::size_t j = 0; j < array_size(psqt); ++j)
                    psqt[j] = _mm_sub_epi32_v(psqt[j], columnPsqt[j]);
            }

            for (const auto index : added)
            {
                const IndexType offset = PSQTBuckets * index + offsetRow;
                auto columnPsqt        = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
                for (std::size_t j = 0; j < array_size(psqt); ++j)
                    psqt[j] = _mm_add_epi32_v(psqt[j], columnPsqt[j]);
            }

            auto accTilePsqtOut = reinterpret_cast<psqt_vec_t*>(
              &(next->*accPtr).psqtAccumulation[Perspective][offsetRow]);
            for (std::size_t j = 0; j < array_size(psqt); ++j)
                accTilePsqtOut[j] = psqt[j];
        }
    }
}

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
template<Color Perspective>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::
  apply_accumulator_updates_refresh_cache(
    Accumulator<TransformedFeatureDimensions>&                accumulator,
    typename AccumulatorCaches::Cache<HalfDimensions>::Entry& entry,
    FeatureSet::IndexList                                     removed,
    FeatureSet::IndexList                                     added) const {
    using vec_t      = typename Details::vec_t;
    using psqt_vec_t = typename Details::psqt_vec_t;

    vec_t acc[Details::OptimalAccRegisterCount];

    for (IndexType j = 0; j < HalfDimensions / Details::TileHeight; ++j)
    {
        const IndexType offsetRow = j * Details::TileHeight;

        const auto accTile =
          reinterpret_cast<vec_t*>(&accumulator.accumulation[Perspective][offsetRow]);
        const auto entryTile = reinterpret_cast<vec_t*>(&entry.accumulation[offsetRow]);

        for (IndexType k = 0; k < array_size(acc); ++k)
            acc[k] = entryTile[k];

        std::size_t i = 0;
        for (; i < std::min(removed.size(), added.size()); ++i)
        {
            const IndexType offsetR = HalfDimensions * removed[i] + offsetRow;
            const auto      columnR = reinterpret_cast<const vec_t*>(&weights[offsetR]);
            const IndexType offsetA = HalfDimensions * added[i] + offsetRow;
            const auto      columnA = reinterpret_cast<const vec_t*>(&weights[offsetA]);

            for (std::size_t k = 0; k < array_size(acc); ++k)
                acc[k] = _mm_add_epi16_v(acc[k], _mm_sub_epi16_v(columnA[k], columnR[k]));
        }
        for (; i < removed.size(); ++i)
        {
            const IndexType offset = HalfDimensions * removed[i] + offsetRow;
            const auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);

            for (std::size_t k = 0; k < array_size(acc); ++k)
                acc[k] = _mm_sub_epi16_v(acc[k], column[k]);
        }
        for (; i < added.size(); ++i)
        {
            const IndexType offset = HalfDimensions * added[i] + offsetRow;
            const auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);

            for (std::size_t k = 0; k < array_size(acc); ++k)
                acc[k] = _mm_add_epi16_v(acc[k], column[k]);
        }

        for (std::size_t k = 0; k < array_size(acc); k++)
            entryTile[k] = acc[k];
        for (std::size_t k = 0; k < array_size(acc); k++)
            accTile[k] = acc[k];
    }

    psqt_vec_t psqt[Details::OptimalPSQTRegisterCount];

    for (IndexType j = 0; j < PSQTBuckets / Details::PsqtTileHeight; ++j)
    {
        const IndexType offsetRow = j * Details::PsqtTileHeight;

        const auto accTilePsqt =
          reinterpret_cast<psqt_vec_t*>(&accumulator.psqtAccumulation[Perspective][offsetRow]);
        const auto entryTilePsqt =
          reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[offsetRow]);

        for (std::size_t k = 0; k < array_size(psqt); ++k)
            psqt[k] = entryTilePsqt[k];

        for (std::size_t i = 0; i < removed.size(); ++i)
        {
            const IndexType offset     = PSQTBuckets * removed[i] + offsetRow;
            const auto      columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);

            for (std::size_t k = 0; k < array_size(psqt); ++k)
                psqt[k] = _mm_sub_epi32_v(psqt[k], columnPsqt[k]);
        }
        for (std::size_t i = 0; i < added.size(); ++i)
        {
            const IndexType offset     = PSQTBuckets * added[i] + offsetRow;
            const auto      columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);

            for (std::size_t k = 0; k < array_size(psqt); ++k)
                psqt[k] = _mm_add_epi32_v(psqt[k], columnPsqt[k]);
        }

        for (std::size_t k = 0; k < array_size(psqt); ++k)
            entryTilePsqt[k] = psqt[k];
        for (std::size_t k = 0; k < array_size(psqt); ++k)
            accTilePsqt[k] = psqt[k];
    }
}

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::convert_accumulators(
  const Position& pos, OutputType* output) const {
    using vec_t = typename Details::vec_t;

    static constexpr IndexType OutputChunkSize = Details::AccRegisterSize / sizeof(OutputType);
    static_assert((HalfDimensions / 2) % OutputChunkSize == 0);

    static constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

    const Color perspectives[2] = {pos.side_to_move(), ~pos.side_to_move()};
    const auto& accumulation    = (pos.state()->*accPtr).accumulation;

    for (IndexType p = 0; p < 2; ++p)
    {
        const auto in0 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][0]));
        const auto in1 =
          reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
        const auto out = reinterpret_cast<vec_t*>(&output[(HalfDimensions / 2) * p]);

        for (IndexType j = 0; j < NumOutputChunks; ++j)
        {
            // What we want to do is multiply inputs in a pairwise manner
            // (after clipping), and then shift right by 9. Instead, we
            // shift left by 7, and use mulhi, stripping the bottom 16 bits,
            // effectively shifting right by 16, resulting in a net shift
            // of 9 bits. We use mulhi because it maintains the sign of
            // the multiplication (unlike mullo), allowing us to make use
            // of packus to clip 2 of the inputs, resulting in a save of 2
            // "_mm_max_epi16_v" calls.

            static const vec_t Zeroes = _mm_setzero_v<vec_t>();
            static const vec_t Ones   = _mm_set1_epi16_v<vec_t>(127 * 2);

            const vec_t sum0a =
              _mm_slli_epi16_v(_mm_max_epi16_v(_mm_min_epi16_v(in0[j * 2 + 0], Ones), Zeroes), 7);
            const vec_t sum0b =
              _mm_slli_epi16_v(_mm_max_epi16_v(_mm_min_epi16_v(in0[j * 2 + 1], Ones), Zeroes), 7);
            const vec_t sum1a = _mm_min_epi16_v(in1[j * 2 + 0], Ones);
            const vec_t sum1b = _mm_min_epi16_v(in1[j * 2 + 1], Ones);

            const vec_t pa = _mm_mulhi_epi16_v(sum0a, sum1a);
            const vec_t pb = _mm_mulhi_epi16_v(sum0b, sum1b);

            out[j] = _mm_packus_epi16_v(pa, pb);
        }
    }
}

}  // namespace Stockfish::Eval::NNUE

#endif  // !__SSE2__

#endif  // I386_NNUE_FEATURE_TRANSFORMER_H_INCLUDED
