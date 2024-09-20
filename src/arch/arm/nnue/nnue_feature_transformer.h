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

#ifndef ARM_NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define ARM_NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

// Check ARM/AArch64 SIMD features.
// If none is defined, fall back to the generic implementation.
#ifndef __ARM_NEON

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
   private:
    static constexpr int NumQReg = 16;

   public:
    static constexpr int OptimalAccRegisterCount =
      optimal_register_count<16, NumQReg, sizeof(WeightType), TransformedFeatureDimensions>();
    static constexpr int OptimalPSQTRegisterCount =
      optimal_register_count<16, NumQReg, sizeof(PSQTWeightType), PSQTBuckets>();

    static constexpr IndexType TileHeight = OptimalAccRegisterCount * 16 / sizeof(WeightType);
    static constexpr IndexType PsqtTileHeight =
      OptimalPSQTRegisterCount * 16 / sizeof(PSQTWeightType);

    static_assert(HalfDimensions % TileHeight == 0,
                  "HalfDimensions must be multiple of TileHeight");
    static_assert(PSQTBuckets % PsqtTileHeight == 0,
                  "PSQTBuckets must be multiple of PsqtTileHeight");
};

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
template<bool Write>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::permute_weights() {}

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
template<Color Perspective, bool CurrentOnly>
void FeatureTransformer<TransformedFeatureDimensions, accPtr>::
  apply_accumulator_updates_incremental(StateInfo*             computed,
                                        StateInfo*             next,
                                        FeatureSet::IndexList& removed,
                                        FeatureSet::IndexList& added) const {
    // The most common case when updating the accumulator incrementally.
    // Calculates feature differences directly without using tiling mechanism.
    if ((removed.size() == 1 || removed.size() == 2) && added.size() == 1)
    {
        const auto accIn =
          reinterpret_cast<const int16x8_t*>(&(computed->*accPtr).accumulation[Perspective][0]);
        const auto accOut =
          reinterpret_cast<int16x8_t*>(&(next->*accPtr).accumulation[Perspective][0]);

        const IndexType offsetR0 = HalfDimensions * removed[0];
        const auto      columnR0 = reinterpret_cast<const int16x8_t*>(&weights[offsetR0]);
        const IndexType offsetA  = HalfDimensions * added[0];
        const auto      columnA  = reinterpret_cast<const int16x8_t*>(&weights[offsetA]);

        if (removed.size() == 1)
        {
            for (IndexType i = 0; i < HalfDimensions * sizeof(WeightType) / 16; ++i)
                accOut[i] = vaddq_s16(vsubq_s16(accIn[i], columnR0[i]), columnA[i]);
        }
        else
        {
            const IndexType offsetR1 = HalfDimensions * removed[1];
            const auto      columnR1 = reinterpret_cast<const int16x8_t*>(&weights[offsetR1]);

            for (IndexType i = 0; i < HalfDimensions * sizeof(WeightType) / 16; ++i)
                accOut[i] =
                  vsubq_s16(vaddq_s16(accIn[i], columnA[i]), vaddq_s16(columnR0[i], columnR1[i]));
        }

        const auto accPsqtIn =
          reinterpret_cast<const int32x4_t*>(&(computed->*accPtr).psqtAccumulation[Perspective][0]);
        const auto accPsqtOut =
          reinterpret_cast<int32x4_t*>(&(next->*accPtr).psqtAccumulation[Perspective][0]);

        const IndexType offsetPsqtR0 = PSQTBuckets * removed[0];
        auto columnPsqtR0 = reinterpret_cast<const int32x4_t*>(&psqtWeights[offsetPsqtR0]);
        const IndexType offsetPsqtA = PSQTBuckets * added[0];
        auto            columnPsqtA = reinterpret_cast<const int32x4_t*>(&psqtWeights[offsetPsqtA]);

        if (removed.size() == 1)
        {
            for (IndexType i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / 16; ++i)
                accPsqtOut[i] = vaddq_s32(vsubq_s32(accPsqtIn[i], columnPsqtR0[i]), columnPsqtA[i]);
        }
        else
        {
            const IndexType offsetPsqtR1 = PSQTBuckets * removed[1];
            const auto      columnPsqtR1 =
              reinterpret_cast<const int32x4_t*>(&psqtWeights[offsetPsqtR1]);

            for (IndexType i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / 16; ++i)
                accPsqtOut[i] = vsubq_s32(vaddq_s32(accPsqtIn[i], columnPsqtA[i]),
                                          vaddq_s32(columnPsqtR0[i], columnPsqtR1[i]));
        }
    }
    else
    {
        int16x8_t acc[Details::OptimalAccRegisterCount];

        for (IndexType i = 0; i < HalfDimensions / Details::TileHeight; ++i)
        {
            const IndexType offsetRow = i * Details::TileHeight;

            const auto accTileIn = reinterpret_cast<const int16x8_t*>(
              &(computed->*accPtr).accumulation[Perspective][offsetRow]);
            for (std::size_t j = 0; j < array_size(acc); ++j)
                acc[j] = accTileIn[j];

            for (const auto index : removed)
            {
                const IndexType offset = HalfDimensions * index + offsetRow;
                const auto      column = reinterpret_cast<const int16x8_t*>(&weights[offset]);
                for (std::size_t j = 0; j < array_size(acc); ++j)
                    acc[j] = vsubq_s16(acc[j], column[j]);
            }

            for (const auto index : added)
            {
                const IndexType offset = HalfDimensions * index + offsetRow;
                const auto      column = reinterpret_cast<const int16x8_t*>(&weights[offset]);
                for (std::size_t j = 0; j < array_size(acc); ++j)
                    acc[j] = vaddq_s16(acc[j], column[j]);
            }

            const auto accTileOut =
              reinterpret_cast<int16x8_t*>(&(next->*accPtr).accumulation[Perspective][offsetRow]);
            for (std::size_t j = 0; j < array_size(acc); ++j)
                accTileOut[j] = acc[j];
        }

        int32x4_t psqt[Details::OptimalPSQTRegisterCount];

        for (IndexType i = 0; i < PSQTBuckets / Details::PsqtTileHeight; ++i)
        {
            const IndexType offsetRow = i * Details::PsqtTileHeight;

            auto accTilePsqtIn = reinterpret_cast<const int32x4_t*>(
              &(computed->*accPtr).psqtAccumulation[Perspective][offsetRow]);
            for (std::size_t j = 0; j < array_size(psqt); ++j)
                psqt[j] = accTilePsqtIn[j];

            for (const auto index : removed)
            {
                const IndexType offset = PSQTBuckets * index + offsetRow;
                auto columnPsqt        = reinterpret_cast<const int32x4_t*>(&psqtWeights[offset]);
                for (std::size_t j = 0; j < array_size(psqt); ++j)
                    psqt[j] = vsubq_s32(psqt[j], columnPsqt[j]);
            }

            for (const auto index : added)
            {
                const IndexType offset = PSQTBuckets * index + offsetRow;
                auto columnPsqt        = reinterpret_cast<const int32x4_t*>(&psqtWeights[offset]);
                for (std::size_t j = 0; j < array_size(psqt); ++j)
                    psqt[j] = vaddq_s32(psqt[j], columnPsqt[j]);
            }

            auto accTilePsqtOut = reinterpret_cast<int32x4_t*>(
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
    int16x8_t acc[Details::OptimalAccRegisterCount];

    for (IndexType j = 0; j < HalfDimensions / Details::TileHeight; ++j)
    {
        const IndexType offsetRow = j * Details::TileHeight;

        const auto accTile =
          reinterpret_cast<int16x8_t*>(&accumulator.accumulation[Perspective][offsetRow]);
        const auto entryTile = reinterpret_cast<int16x8_t*>(&entry.accumulation[offsetRow]);

        for (IndexType k = 0; k < array_size(acc); ++k)
            acc[k] = entryTile[k];

        std::size_t i = 0;
        for (; i < std::min(removed.size(), added.size()); ++i)
        {
            const IndexType offsetR = HalfDimensions * removed[i] + offsetRow;
            const auto      columnR = reinterpret_cast<const int16x8_t*>(&weights[offsetR]);
            const IndexType offsetA = HalfDimensions * added[i] + offsetRow;
            const auto      columnA = reinterpret_cast<const int16x8_t*>(&weights[offsetA]);

            for (std::size_t k = 0; k < array_size(acc); ++k)
                acc[k] = vaddq_s16(acc[k], vsubq_s16(columnA[k], columnR[k]));
        }
        for (; i < removed.size(); ++i)
        {
            const IndexType offset = HalfDimensions * removed[i] + offsetRow;
            const auto      column = reinterpret_cast<const int16x8_t*>(&weights[offset]);

            for (std::size_t k = 0; k < array_size(acc); ++k)
                acc[k] = vsubq_s16(acc[k], column[k]);
        }
        for (; i < added.size(); ++i)
        {
            const IndexType offset = HalfDimensions * added[i] + offsetRow;
            const auto      column = reinterpret_cast<const int16x8_t*>(&weights[offset]);

            for (std::size_t k = 0; k < array_size(acc); ++k)
                acc[k] = vaddq_s16(acc[k], column[k]);
        }

        for (std::size_t k = 0; k < array_size(acc); k++)
            entryTile[k] = acc[k];
        for (std::size_t k = 0; k < array_size(acc); k++)
            accTile[k] = acc[k];
    }

    int32x4_t psqt[Details::OptimalPSQTRegisterCount];

    for (IndexType j = 0; j < PSQTBuckets / Details::PsqtTileHeight; ++j)
    {
        const IndexType offsetRow = j * Details::PsqtTileHeight;

        const auto accTilePsqt =
          reinterpret_cast<int32x4_t*>(&accumulator.psqtAccumulation[Perspective][offsetRow]);
        const auto entryTilePsqt = reinterpret_cast<int32x4_t*>(&entry.psqtAccumulation[offsetRow]);

        for (std::size_t k = 0; k < array_size(psqt); ++k)
            psqt[k] = entryTilePsqt[k];

        for (std::size_t i = 0; i < removed.size(); ++i)
        {
            const IndexType offset     = PSQTBuckets * removed[i] + offsetRow;
            const auto      columnPsqt = reinterpret_cast<const int32x4_t*>(&psqtWeights[offset]);

            for (std::size_t k = 0; k < array_size(psqt); ++k)
                psqt[k] = vsubq_s32(psqt[k], columnPsqt[k]);
        }
        for (std::size_t i = 0; i < added.size(); ++i)
        {
            const IndexType offset     = PSQTBuckets * added[i] + offsetRow;
            const auto      columnPsqt = reinterpret_cast<const int32x4_t*>(&psqtWeights[offset]);

            for (std::size_t k = 0; k < array_size(psqt); ++k)
                psqt[k] = vaddq_s32(psqt[k], columnPsqt[k]);
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
    static constexpr IndexType OutputChunkSize = 16 / sizeof(OutputType);
    static_assert((HalfDimensions / 2) % OutputChunkSize == 0);

    static constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

    const Color perspectives[2] = {pos.side_to_move(), ~pos.side_to_move()};
    const auto& accumulation    = (pos.state()->*accPtr).accumulation;

    for (IndexType p = 0; p < 2; ++p)
    {
        const auto in0 = reinterpret_cast<const int16x8_t*>(&(accumulation[perspectives[p]][0]));
        const auto in1 =
          reinterpret_cast<const int16x8_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
        const auto out = reinterpret_cast<uint8x16_t*>(&output[(HalfDimensions / 2) * p]);

        for (IndexType j = 0; j < NumOutputChunks; ++j)
        {
            static const int16x8_t Zeroes = vdupq_n_s16(0);
            static const int16x8_t Ones   = vdupq_n_s16(127 * 2);

            const int16x8_t sum0a =
              vshlq_n_s16(vmaxq_s16(vminq_s16(in0[j * 2 + 0], Ones), Zeroes), 6);
            const int16x8_t sum0b =
              vshlq_n_s16(vmaxq_s16(vminq_s16(in0[j * 2 + 1], Ones), Zeroes), 6);
            const int16x8_t sum1a = vminq_s16(in1[j * 2 + 0], Ones);
            const int16x8_t sum1b = vminq_s16(in1[j * 2 + 1], Ones);

            const int16x8_t pa = vqdmulhq_s16(sum0a, sum1a);
            const int16x8_t pb = vqdmulhq_s16(sum0b, sum1b);

            out[j] = vcombine_u8(vqmovun_s16(pa), vqmovun_s16(pb));
        }
    }
}

}  // namespace Stockfish::Eval::NNUE

#endif  // !__ARM_NEON

#endif  // ARM_NNUE_FEATURE_TRANSFORMER_H_INCLUDED
