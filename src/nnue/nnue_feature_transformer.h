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

// A class that converts the input features of the NNUE evaluation function

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include "nnue_common.h"
#include "nnue_architecture.h"

#include "../misc.h"

#include <cstring> // std::memset()

namespace Stockfish::Eval::NNUE {

  // If vector instructions are enabled, we update and refresh the
  // accumulator tile by tile such that each tile fits in the CPU's
  // vector registers.
  #define VECTOR

  static_assert(PSQTBuckets == 8, "Assumed by the current choice of constants.");

  #ifdef USE_AVX512
  typedef __m512i vec_t;
  typedef __m256i psqt_vec_t;
  #define vec_load(a) _mm512_load_si512(a)
  #define vec_store(a,b) _mm512_store_si512(a,b)
  #define vec_add_16(a,b) _mm512_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm512_sub_epi16(a,b)
  #define vec_load_psqt(a) _mm256_load_si256(a)
  #define vec_store_psqt(a,b) _mm256_store_si256(a,b)
  #define vec_add_psqt_32(a,b) _mm256_add_epi32(a,b)
  #define vec_sub_psqt_32(a,b) _mm256_sub_epi32(a,b)
  #define vec_zero_psqt() _mm256_setzero_si256()
  static constexpr IndexType NumRegs = 8; // only 8 are needed
  static constexpr IndexType NumPsqtRegs = 1;

  #elif USE_AVX2
  typedef __m256i vec_t;
  typedef __m256i psqt_vec_t;
  #define vec_load(a) _mm256_load_si256(a)
  #define vec_store(a,b) _mm256_store_si256(a,b)
  #define vec_add_16(a,b) _mm256_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm256_sub_epi16(a,b)
  #define vec_load_psqt(a) _mm256_load_si256(a)
  #define vec_store_psqt(a,b) _mm256_store_si256(a,b)
  #define vec_add_psqt_32(a,b) _mm256_add_epi32(a,b)
  #define vec_sub_psqt_32(a,b) _mm256_sub_epi32(a,b)
  #define vec_zero_psqt() _mm256_setzero_si256()
  static constexpr IndexType NumRegs = 16;
  static constexpr IndexType NumPsqtRegs = 1;

  #elif USE_SSE2
  typedef __m128i vec_t;
  typedef __m128i psqt_vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) _mm_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm_sub_epi16(a,b)
  #define vec_load_psqt(a) (*(a))
  #define vec_store_psqt(a,b) *(a)=(b)
  #define vec_add_psqt_32(a,b) _mm_add_epi32(a,b)
  #define vec_sub_psqt_32(a,b) _mm_sub_epi32(a,b)
  #define vec_zero_psqt() _mm_setzero_si128()
  static constexpr IndexType NumRegs = Is64Bit ? 16 : 8;
  static constexpr IndexType NumPsqtRegs = 2;

  #elif USE_MMX
  typedef __m64 vec_t;
  typedef std::int32_t psqt_vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) _mm_add_pi16(a,b)
  #define vec_sub_16(a,b) _mm_sub_pi16(a,b)
  #define vec_load_psqt(a) (*(a))
  #define vec_store_psqt(a,b) *(a)=(b)
  #define vec_add_psqt_32(a,b) a+b
  #define vec_sub_psqt_32(a,b) a-b
  #define vec_zero_psqt() 0
  static constexpr IndexType NumRegs = 8;
  static constexpr IndexType NumPsqtRegs = 8;

  #elif USE_NEON
  typedef int16x8_t vec_t;
  typedef int32x4_t psqt_vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) vaddq_s16(a,b)
  #define vec_sub_16(a,b) vsubq_s16(a,b)
  #define vec_load_psqt(a) (*(a))
  #define vec_store_psqt(a,b) *(a)=(b)
  #define vec_add_psqt_32(a,b) vaddq_s32(a,b)
  #define vec_sub_psqt_32(a,b) vsubq_s32(a,b)
  #define vec_zero_psqt() psqt_vec_t{0}
  static constexpr IndexType NumRegs = 16;
  static constexpr IndexType NumPsqtRegs = 2;

  #else
  #undef VECTOR

  #endif

  // Input feature converter
  class FeatureTransformer {

   private:
    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

    static constexpr int LazyThreshold = 1400;

    #ifdef VECTOR
    static constexpr IndexType TileHeight = NumRegs * sizeof(vec_t) / 2;
    static constexpr IndexType PsqtTileHeight = NumPsqtRegs * sizeof(psqt_vec_t) / 4;
    static_assert(HalfDimensions % TileHeight == 0, "TileHeight must divide HalfDimensions");
    static_assert(PSQTBuckets % PsqtTileHeight == 0, "PsqtTileHeight must divide PSQTBuckets");
    #endif

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions = FeatureSet::Dimensions;
    static constexpr IndexType OutputDimensions = HalfDimensions * 2;

    // Size of forward propagation buffer
    static constexpr std::size_t BufferSize =
        OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() {
      return FeatureSet::HashValue ^ OutputDimensions;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
      for (std::size_t i = 0; i < HalfDimensions; ++i)
        biases[i] = read_little_endian<BiasType>(stream);
      for (std::size_t i = 0; i < HalfDimensions * InputDimensions; ++i)
        weights[i] = read_little_endian<WeightType>(stream);
      for (std::size_t i = 0; i < PSQTBuckets * InputDimensions; ++i)
        psqtWeights[i] = read_little_endian<PSQTWeightType>(stream);
      return !stream.fail();
    }

    // Convert input features
    std::pair<std::int32_t, bool> transform(const Position& pos, OutputType* output, int bucket) const {
      update_accumulator(pos, WHITE);
      update_accumulator(pos, BLACK);

      const Color perspectives[2] = {pos.side_to_move(), ~pos.side_to_move()};
      const auto& accumulation = pos.state()->accumulator.accumulation;
      const auto& psqtAccumulation = pos.state()->accumulator.psqtAccumulation;

      const auto psqt = (
            psqtAccumulation[static_cast<int>(perspectives[0])][bucket]
          - psqtAccumulation[static_cast<int>(perspectives[1])][bucket]
        ) / 2;

      if (abs(psqt) > LazyThreshold * OutputScale)
        return { psqt, true };

  #if defined(USE_AVX512)
      constexpr IndexType NumChunks = HalfDimensions / (SimdWidth * 2);
      static_assert(HalfDimensions % (SimdWidth * 2) == 0);
      const __m512i Control = _mm512_setr_epi64(0, 2, 4, 6, 1, 3, 5, 7);
      const __m512i Zero = _mm512_setzero_si512();

  #elif defined(USE_AVX2)
      constexpr IndexType NumChunks = HalfDimensions / SimdWidth;
      constexpr int Control = 0b11011000;
      const __m256i Zero = _mm256_setzero_si256();

  #elif defined(USE_SSE2)
      constexpr IndexType NumChunks = HalfDimensions / SimdWidth;

  #ifdef USE_SSE41
      const __m128i Zero = _mm_setzero_si128();
  #else
      const __m128i k0x80s = _mm_set1_epi8(-128);
  #endif

  #elif defined(USE_MMX)
      constexpr IndexType NumChunks = HalfDimensions / SimdWidth;
      const __m64 k0x80s = _mm_set1_pi8(-128);

  #elif defined(USE_NEON)
      constexpr IndexType NumChunks = HalfDimensions / (SimdWidth / 2);
      const int8x8_t Zero = {0};
  #endif

      for (IndexType p = 0; p < 2; ++p) {
        const IndexType offset = HalfDimensions * p;

  #if defined(USE_AVX512)
        auto out = reinterpret_cast<__m512i*>(&output[offset]);
        for (IndexType j = 0; j < NumChunks; ++j) {
          __m512i sum0 = _mm512_load_si512(
              &reinterpret_cast<const __m512i*>(accumulation[perspectives[p]])[j * 2 + 0]);
          __m512i sum1 = _mm512_load_si512(
              &reinterpret_cast<const __m512i*>(accumulation[perspectives[p]])[j * 2 + 1]);
          _mm512_store_si512(&out[j], _mm512_permutexvar_epi64(Control,
              _mm512_max_epi8(_mm512_packs_epi16(sum0, sum1), Zero)));
        }

  #elif defined(USE_AVX2)
        auto out = reinterpret_cast<__m256i*>(&output[offset]);
        for (IndexType j = 0; j < NumChunks; ++j) {
          __m256i sum0 = _mm256_load_si256(
              &reinterpret_cast<const __m256i*>(accumulation[perspectives[p]])[j * 2 + 0]);
          __m256i sum1 = _mm256_load_si256(
              &reinterpret_cast<const __m256i*>(accumulation[perspectives[p]])[j * 2 + 1]);
          _mm256_store_si256(&out[j], _mm256_permute4x64_epi64(_mm256_max_epi8(
              _mm256_packs_epi16(sum0, sum1), Zero), Control));
        }

  #elif defined(USE_SSE2)
        auto out = reinterpret_cast<__m128i*>(&output[offset]);
        for (IndexType j = 0; j < NumChunks; ++j) {
          __m128i sum0 = _mm_load_si128(&reinterpret_cast<const __m128i*>(
              accumulation[perspectives[p]])[j * 2 + 0]);
          __m128i sum1 = _mm_load_si128(&reinterpret_cast<const __m128i*>(
              accumulation[perspectives[p]])[j * 2 + 1]);
      const __m128i packedbytes = _mm_packs_epi16(sum0, sum1);

          _mm_store_si128(&out[j],

  #ifdef USE_SSE41
              _mm_max_epi8(packedbytes, Zero)
  #else
              _mm_subs_epi8(_mm_adds_epi8(packedbytes, k0x80s), k0x80s)
  #endif

          );
        }

  #elif defined(USE_MMX)
        auto out = reinterpret_cast<__m64*>(&output[offset]);
        for (IndexType j = 0; j < NumChunks; ++j) {
          __m64 sum0 = *(&reinterpret_cast<const __m64*>(
              accumulation[perspectives[p]])[j * 2 + 0]);
          __m64 sum1 = *(&reinterpret_cast<const __m64*>(
              accumulation[perspectives[p]])[j * 2 + 1]);
          const __m64 packedbytes = _mm_packs_pi16(sum0, sum1);
          out[j] = _mm_subs_pi8(_mm_adds_pi8(packedbytes, k0x80s), k0x80s);
        }

  #elif defined(USE_NEON)
        const auto out = reinterpret_cast<int8x8_t*>(&output[offset]);
        for (IndexType j = 0; j < NumChunks; ++j) {
          int16x8_t sum = reinterpret_cast<const int16x8_t*>(
              accumulation[perspectives[p]])[j];
          out[j] = vmax_s8(vqmovn_s16(sum), Zero);
        }

  #else
        for (IndexType j = 0; j < HalfDimensions; ++j) {
          BiasType sum = accumulation[static_cast<int>(perspectives[p])][j];
          output[offset + j] = static_cast<OutputType>(
              std::max<int>(0, std::min<int>(127, sum)));
        }
  #endif

      }
  #if defined(USE_MMX)
      _mm_empty();
  #endif

      return { psqt, false };
    }

   private:
    void update_accumulator(const Position& pos, const Color perspective) const {

      // The size must be enough to contain the largest possible update.
      // That might depend on the feature set and generally relies on the
      // feature set's update cost calculation to be correct and never
      // allow updates with more added/removed features than MaxActiveDimensions.
      using IndexList = ValueList<IndexType, FeatureSet::MaxActiveDimensions>;

  #ifdef VECTOR
      // Gcc-10.2 unnecessarily spills AVX2 registers if this array
      // is defined in the VECTOR code below, once in each branch
      vec_t acc[NumRegs];
      psqt_vec_t psqt[NumPsqtRegs];
  #endif

      // Look for a usable accumulator of an earlier position. We keep track
      // of the estimated gain in terms of features to be added/subtracted.
      StateInfo *st = pos.state(), *next = nullptr;
      int gain = FeatureSet::refresh_cost(pos);
      while (st->accumulator.state[perspective] == EMPTY)
      {
        // This governs when a full feature refresh is needed and how many
        // updates are better than just one full refresh.
        if (   FeatureSet::requires_refresh(st, perspective)
            || (gain -= FeatureSet::update_cost(st) + 1) < 0)
          break;
        next = st;
        st = st->previous;
      }

      if (st->accumulator.state[perspective] == COMPUTED)
      {
        if (next == nullptr)
          return;

        // Update incrementally in two steps. First, we update the "next"
        // accumulator. Then, we update the current accumulator (pos.state()).

        // Gather all features to be updated.
        const Square ksq = pos.square<KING>(perspective);
        IndexList removed[2], added[2];
        FeatureSet::append_changed_indices(
          ksq, next, perspective, removed[0], added[0]);
        for (StateInfo *st2 = pos.state(); st2 != next; st2 = st2->previous)
          FeatureSet::append_changed_indices(
            ksq, st2, perspective, removed[1], added[1]);

        // Mark the accumulators as computed.
        next->accumulator.state[perspective] = COMPUTED;
        pos.state()->accumulator.state[perspective] = COMPUTED;

        // Now update the accumulators listed in states_to_update[], where the last element is a sentinel.
        StateInfo *states_to_update[3] =
          { next, next == pos.state() ? nullptr : pos.state(), nullptr };
  #ifdef VECTOR
        for (IndexType j = 0; j < HalfDimensions / TileHeight; ++j)
        {
          // Load accumulator
          auto accTile = reinterpret_cast<vec_t*>(
            &st->accumulator.accumulation[perspective][j * TileHeight]);
          for (IndexType k = 0; k < NumRegs; ++k)
            acc[k] = vec_load(&accTile[k]);

          for (IndexType i = 0; states_to_update[i]; ++i)
          {
            // Difference calculation for the deactivated features
            for (const auto index : removed[i])
            {
              const IndexType offset = HalfDimensions * index + j * TileHeight;
              auto column = reinterpret_cast<const vec_t*>(&weights[offset]);
              for (IndexType k = 0; k < NumRegs; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);
            }

            // Difference calculation for the activated features
            for (const auto index : added[i])
            {
              const IndexType offset = HalfDimensions * index + j * TileHeight;
              auto column = reinterpret_cast<const vec_t*>(&weights[offset]);
              for (IndexType k = 0; k < NumRegs; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);
            }

            // Store accumulator
            accTile = reinterpret_cast<vec_t*>(
              &states_to_update[i]->accumulator.accumulation[perspective][j * TileHeight]);
            for (IndexType k = 0; k < NumRegs; ++k)
              vec_store(&accTile[k], acc[k]);
          }
        }

        for (IndexType j = 0; j < PSQTBuckets / PsqtTileHeight; ++j)
        {
          // Load accumulator
          auto accTilePsqt = reinterpret_cast<psqt_vec_t*>(
            &st->accumulator.psqtAccumulation[perspective][j * PsqtTileHeight]);
          for (std::size_t k = 0; k < NumPsqtRegs; ++k)
            psqt[k] = vec_load_psqt(&accTilePsqt[k]);

          for (IndexType i = 0; states_to_update[i]; ++i)
          {
            // Difference calculation for the deactivated features
            for (const auto index : removed[i])
            {
              const IndexType offset = PSQTBuckets * index + j * PsqtTileHeight;
              auto columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
              for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
            }

            // Difference calculation for the activated features
            for (const auto index : added[i])
            {
              const IndexType offset = PSQTBuckets * index + j * PsqtTileHeight;
              auto columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
              for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
            }

            // Store accumulator
            accTilePsqt = reinterpret_cast<psqt_vec_t*>(
              &states_to_update[i]->accumulator.psqtAccumulation[perspective][j * PsqtTileHeight]);
            for (std::size_t k = 0; k < NumPsqtRegs; ++k)
              vec_store_psqt(&accTilePsqt[k], psqt[k]);
          }
        }

  #else
        for (IndexType i = 0; states_to_update[i]; ++i)
        {
          std::memcpy(states_to_update[i]->accumulator.accumulation[perspective],
              st->accumulator.accumulation[perspective],
              HalfDimensions * sizeof(BiasType));

          for (std::size_t k = 0; k < PSQTBuckets; ++k)
            states_to_update[i]->accumulator.psqtAccumulation[perspective][k] = st->accumulator.psqtAccumulation[perspective][k];

          st = states_to_update[i];

          // Difference calculation for the deactivated features
          for (const auto index : removed[i])
          {
            const IndexType offset = HalfDimensions * index;

            for (IndexType j = 0; j < HalfDimensions; ++j)
              st->accumulator.accumulation[perspective][j] -= weights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
              st->accumulator.psqtAccumulation[perspective][k] -= psqtWeights[index * PSQTBuckets + k];
          }

          // Difference calculation for the activated features
          for (const auto index : added[i])
          {
            const IndexType offset = HalfDimensions * index;

            for (IndexType j = 0; j < HalfDimensions; ++j)
              st->accumulator.accumulation[perspective][j] += weights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
              st->accumulator.psqtAccumulation[perspective][k] += psqtWeights[index * PSQTBuckets + k];
          }
        }
  #endif
      }
      else
      {
        // Refresh the accumulator
        auto& accumulator = pos.state()->accumulator;
        accumulator.state[perspective] = COMPUTED;
        IndexList active;
        FeatureSet::append_active_indices(pos, perspective, active);

  #ifdef VECTOR
        for (IndexType j = 0; j < HalfDimensions / TileHeight; ++j)
        {
          auto biasesTile = reinterpret_cast<const vec_t*>(
              &biases[j * TileHeight]);
          for (IndexType k = 0; k < NumRegs; ++k)
            acc[k] = biasesTile[k];

          for (const auto index : active)
          {
            const IndexType offset = HalfDimensions * index + j * TileHeight;
            auto column = reinterpret_cast<const vec_t*>(&weights[offset]);

            for (unsigned k = 0; k < NumRegs; ++k)
              acc[k] = vec_add_16(acc[k], column[k]);
          }

          auto accTile = reinterpret_cast<vec_t*>(
              &accumulator.accumulation[perspective][j * TileHeight]);
          for (unsigned k = 0; k < NumRegs; k++)
            vec_store(&accTile[k], acc[k]);
        }

        for (IndexType j = 0; j < PSQTBuckets / PsqtTileHeight; ++j)
        {
          for (std::size_t k = 0; k < NumPsqtRegs; ++k)
            psqt[k] = vec_zero_psqt();

          for (const auto index : active)
          {
            const IndexType offset = PSQTBuckets * index + j * PsqtTileHeight;
            auto columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);

            for (std::size_t k = 0; k < NumPsqtRegs; ++k)
              psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
          }

          auto accTilePsqt = reinterpret_cast<psqt_vec_t*>(
            &accumulator.psqtAccumulation[perspective][j * PsqtTileHeight]);
          for (std::size_t k = 0; k < NumPsqtRegs; ++k)
            vec_store_psqt(&accTilePsqt[k], psqt[k]);
        }

  #else
        std::memcpy(accumulator.accumulation[perspective], biases,
            HalfDimensions * sizeof(BiasType));

        for (std::size_t k = 0; k < PSQTBuckets; ++k)
          accumulator.psqtAccumulation[perspective][k] = 0;

        for (const auto index : active)
        {
          const IndexType offset = HalfDimensions * index;

          for (IndexType j = 0; j < HalfDimensions; ++j)
            accumulator.accumulation[perspective][j] += weights[offset + j];

          for (std::size_t k = 0; k < PSQTBuckets; ++k)
            accumulator.psqtAccumulation[perspective][k] += psqtWeights[index * PSQTBuckets + k];
        }
  #endif
      }

  #if defined(USE_MMX)
      _mm_empty();
  #endif
    }

    using BiasType = std::int16_t;
    using WeightType = std::int16_t;
    using PSQTWeightType = std::int32_t;

    alignas(CacheLineSize) BiasType biases[HalfDimensions];
    alignas(CacheLineSize) WeightType weights[HalfDimensions * InputDimensions];
    alignas(CacheLineSize) PSQTWeightType psqtWeights[InputDimensions * PSQTBuckets];
  };

}  // namespace Stockfish::Eval::NNUE

#endif // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
