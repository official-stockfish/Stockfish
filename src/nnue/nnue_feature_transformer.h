/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

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
#include "features/index_list.h"

#include <cstring> // std::memset()

namespace Eval::NNUE {

  // If vector instructions are enabled, we update and refresh the
  // accumulator tile by tile such that each tile fits in the CPU's
  // vector registers.
  #define VECTOR

  #ifdef USE_AVX512
  typedef __m512i vec_t;
  #define vec_load(a) _mm512_load_si512(a)
  #define vec_store(a,b) _mm512_store_si512(a,b)
  #define vec_add_16(a,b) _mm512_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm512_sub_epi16(a,b)
  static constexpr IndexType kNumRegs = 8; // only 8 are needed

  #elif USE_AVX2
  typedef __m256i vec_t;
  #define vec_load(a) _mm256_load_si256(a)
  #define vec_store(a,b) _mm256_store_si256(a,b)
  #define vec_add_16(a,b) _mm256_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm256_sub_epi16(a,b)
  static constexpr IndexType kNumRegs = 16;

  #elif USE_SSE2
  typedef __m128i vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) _mm_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm_sub_epi16(a,b)
  static constexpr IndexType kNumRegs = Is64Bit ? 16 : 8;

  #elif USE_MMX
  typedef __m64 vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) _mm_add_pi16(a,b)
  #define vec_sub_16(a,b) _mm_sub_pi16(a,b)
  static constexpr IndexType kNumRegs = 8;

  #elif USE_NEON
  typedef int16x8_t vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) vaddq_s16(a,b)
  #define vec_sub_16(a,b) vsubq_s16(a,b)
  static constexpr IndexType kNumRegs = 16;

  #else
  #undef VECTOR

  #endif

  // Input feature converter
  class FeatureTransformer {

   private:
    // Number of output dimensions for one side
    static constexpr IndexType kHalfDimensions = kTransformedFeatureDimensions;

    #ifdef VECTOR
    static constexpr IndexType kTileHeight = kNumRegs * sizeof(vec_t) / 2;
    static_assert(kHalfDimensions % kTileHeight == 0, "kTileHeight must divide kHalfDimensions");
    #endif

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType kInputDimensions = RawFeatures::kDimensions;
    static constexpr IndexType kOutputDimensions = kHalfDimensions * 2;

    // Size of forward propagation buffer
    static constexpr std::size_t kBufferSize =
        kOutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t GetHashValue() {

      return RawFeatures::kHashValue ^ kOutputDimensions;
    }

    // Read network parameters
    bool ReadParameters(std::istream& stream) {

      for (std::size_t i = 0; i < kHalfDimensions; ++i)
        biases_[i] = read_little_endian<BiasType>(stream);
      for (std::size_t i = 0; i < kHalfDimensions * kInputDimensions; ++i)
        weights_[i] = read_little_endian<WeightType>(stream);
      return !stream.fail();
    }

    // Convert input features
    void Transform(const Position& pos, OutputType* output) const {

      UpdateAccumulator(pos, WHITE);
      UpdateAccumulator(pos, BLACK);

      const auto& accumulation = pos.state()->accumulator.accumulation;

  #if defined(USE_AVX512)
      constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth * 2);
      static_assert(kHalfDimensions % (kSimdWidth * 2) == 0);
      const __m512i kControl = _mm512_setr_epi64(0, 2, 4, 6, 1, 3, 5, 7);
      const __m512i kZero = _mm512_setzero_si512();

  #elif defined(USE_AVX2)
      constexpr IndexType kNumChunks = kHalfDimensions / kSimdWidth;
      constexpr int kControl = 0b11011000;
      const __m256i kZero = _mm256_setzero_si256();

  #elif defined(USE_SSE2)
      constexpr IndexType kNumChunks = kHalfDimensions / kSimdWidth;

  #ifdef USE_SSE41
      const __m128i kZero = _mm_setzero_si128();
  #else
      const __m128i k0x80s = _mm_set1_epi8(-128);
  #endif

  #elif defined(USE_MMX)
      constexpr IndexType kNumChunks = kHalfDimensions / kSimdWidth;
      const __m64 k0x80s = _mm_set1_pi8(-128);

  #elif defined(USE_NEON)
      constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth / 2);
      const int8x8_t kZero = {0};
  #endif

      const Color perspectives[2] = {pos.side_to_move(), ~pos.side_to_move()};
      for (IndexType p = 0; p < 2; ++p) {
        const IndexType offset = kHalfDimensions * p;

  #if defined(USE_AVX512)
        auto out = reinterpret_cast<__m512i*>(&output[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          __m512i sum0 = _mm512_load_si512(
              &reinterpret_cast<const __m512i*>(accumulation[perspectives[p]][0])[j * 2 + 0]);
          __m512i sum1 = _mm512_load_si512(
              &reinterpret_cast<const __m512i*>(accumulation[perspectives[p]][0])[j * 2 + 1]);
          _mm512_store_si512(&out[j], _mm512_permutexvar_epi64(kControl,
              _mm512_max_epi8(_mm512_packs_epi16(sum0, sum1), kZero)));
        }

  #elif defined(USE_AVX2)
        auto out = reinterpret_cast<__m256i*>(&output[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          __m256i sum0 = _mm256_load_si256(
              &reinterpret_cast<const __m256i*>(accumulation[perspectives[p]][0])[j * 2 + 0]);
          __m256i sum1 = _mm256_load_si256(
              &reinterpret_cast<const __m256i*>(accumulation[perspectives[p]][0])[j * 2 + 1]);
          _mm256_store_si256(&out[j], _mm256_permute4x64_epi64(_mm256_max_epi8(
              _mm256_packs_epi16(sum0, sum1), kZero), kControl));
        }

  #elif defined(USE_SSE2)
        auto out = reinterpret_cast<__m128i*>(&output[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          __m128i sum0 = _mm_load_si128(&reinterpret_cast<const __m128i*>(
              accumulation[perspectives[p]][0])[j * 2 + 0]);
          __m128i sum1 = _mm_load_si128(&reinterpret_cast<const __m128i*>(
              accumulation[perspectives[p]][0])[j * 2 + 1]);
      const __m128i packedbytes = _mm_packs_epi16(sum0, sum1);

          _mm_store_si128(&out[j],

  #ifdef USE_SSE41
              _mm_max_epi8(packedbytes, kZero)
  #else
              _mm_subs_epi8(_mm_adds_epi8(packedbytes, k0x80s), k0x80s)
  #endif

          );
        }

  #elif defined(USE_MMX)
        auto out = reinterpret_cast<__m64*>(&output[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          __m64 sum0 = *(&reinterpret_cast<const __m64*>(
              accumulation[perspectives[p]][0])[j * 2 + 0]);
          __m64 sum1 = *(&reinterpret_cast<const __m64*>(
              accumulation[perspectives[p]][0])[j * 2 + 1]);
          const __m64 packedbytes = _mm_packs_pi16(sum0, sum1);
          out[j] = _mm_subs_pi8(_mm_adds_pi8(packedbytes, k0x80s), k0x80s);
        }

  #elif defined(USE_NEON)
        const auto out = reinterpret_cast<int8x8_t*>(&output[offset]);
        for (IndexType j = 0; j < kNumChunks; ++j) {
          int16x8_t sum = reinterpret_cast<const int16x8_t*>(
              accumulation[perspectives[p]][0])[j];
          out[j] = vmax_s8(vqmovn_s16(sum), kZero);
        }

  #else
        for (IndexType j = 0; j < kHalfDimensions; ++j) {
          BiasType sum = accumulation[static_cast<int>(perspectives[p])][0][j];
          output[offset + j] = static_cast<OutputType>(
              std::max<int>(0, std::min<int>(127, sum)));
        }
  #endif

      }
  #if defined(USE_MMX)
      _mm_empty();
  #endif
    }

   private:
    void UpdateAccumulator(const Position& pos, const Color c) const {

  #ifdef VECTOR
      // Gcc-10.2 unnecessarily spills AVX2 registers if this array
      // is defined in the VECTOR code below, once in each branch
      vec_t acc[kNumRegs];
  #endif

      // Look for a usable accumulator of an earlier position. We keep track
      // of the estimated gain in terms of features to be added/subtracted.
      StateInfo *st = pos.state(), *next = nullptr;
      int gain = pos.count<ALL_PIECES>() - 2;
      while (st->accumulator.state[c] == EMPTY)
      {
        auto& dp = st->dirtyPiece;
        // The first condition tests whether an incremental update is
        // possible at all: if this side's king has moved, it is not possible.
        static_assert(std::is_same_v<RawFeatures::SortedTriggerSet,
              Features::CompileTimeList<Features::TriggerEvent, Features::TriggerEvent::kFriendKingMoved>>,
              "Current code assumes that only kFriendlyKingMoved refresh trigger is being used.");
        if (   dp.piece[0] == make_piece(c, KING)
            || (gain -= dp.dirty_num + 1) < 0)
          break;
        next = st;
        st = st->previous;
      }

      if (st->accumulator.state[c] == COMPUTED)
      {
        if (next == nullptr)
          return;

        // Update incrementally in two steps. First, we update the "next"
        // accumulator. Then, we update the current accumulator (pos.state()).

        // Gather all features to be updated. This code assumes HalfKP features
        // only and doesn't support refresh triggers.
        static_assert(std::is_same_v<Features::FeatureSet<Features::HalfKP<Features::Side::kFriend>>,
                                     RawFeatures>);
        Features::IndexList removed[2], added[2];
        Features::HalfKP<Features::Side::kFriend>::AppendChangedIndices(pos,
            next->dirtyPiece, c, &removed[0], &added[0]);
        for (StateInfo *st2 = pos.state(); st2 != next; st2 = st2->previous)
          Features::HalfKP<Features::Side::kFriend>::AppendChangedIndices(pos,
              st2->dirtyPiece, c, &removed[1], &added[1]);

        // Mark the accumulators as computed.
        next->accumulator.state[c] = COMPUTED;
        pos.state()->accumulator.state[c] = COMPUTED;

        // Now update the accumulators listed in info[], where the last element is a sentinel.
        StateInfo *info[3] =
          { next, next == pos.state() ? nullptr : pos.state(), nullptr };
  #ifdef VECTOR
        for (IndexType j = 0; j < kHalfDimensions / kTileHeight; ++j)
        {
          // Load accumulator
          auto accTile = reinterpret_cast<vec_t*>(
            &st->accumulator.accumulation[c][0][j * kTileHeight]);
          for (IndexType k = 0; k < kNumRegs; ++k)
            acc[k] = vec_load(&accTile[k]);

          for (IndexType i = 0; info[i]; ++i)
          {
            // Difference calculation for the deactivated features
            for (const auto index : removed[i])
            {
              const IndexType offset = kHalfDimensions * index + j * kTileHeight;
              auto column = reinterpret_cast<const vec_t*>(&weights_[offset]);
              for (IndexType k = 0; k < kNumRegs; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);
            }

            // Difference calculation for the activated features
            for (const auto index : added[i])
            {
              const IndexType offset = kHalfDimensions * index + j * kTileHeight;
              auto column = reinterpret_cast<const vec_t*>(&weights_[offset]);
              for (IndexType k = 0; k < kNumRegs; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);
            }

            // Store accumulator
            accTile = reinterpret_cast<vec_t*>(
              &info[i]->accumulator.accumulation[c][0][j * kTileHeight]);
            for (IndexType k = 0; k < kNumRegs; ++k)
              vec_store(&accTile[k], acc[k]);
          }
        }

  #else
        for (IndexType i = 0; info[i]; ++i)
        {
          std::memcpy(info[i]->accumulator.accumulation[c][0],
              st->accumulator.accumulation[c][0],
              kHalfDimensions * sizeof(BiasType));
          st = info[i];

          // Difference calculation for the deactivated features
          for (const auto index : removed[i])
          {
            const IndexType offset = kHalfDimensions * index;

            for (IndexType j = 0; j < kHalfDimensions; ++j)
              st->accumulator.accumulation[c][0][j] -= weights_[offset + j];
          }

          // Difference calculation for the activated features
          for (const auto index : added[i])
          {
            const IndexType offset = kHalfDimensions * index;

            for (IndexType j = 0; j < kHalfDimensions; ++j)
              st->accumulator.accumulation[c][0][j] += weights_[offset + j];
          }
        }
  #endif
      }
      else
      {
        // Refresh the accumulator
        auto& accumulator = pos.state()->accumulator;
        accumulator.state[c] = COMPUTED;
        Features::IndexList active;
        Features::HalfKP<Features::Side::kFriend>::AppendActiveIndices(pos, c, &active);

  #ifdef VECTOR
        for (IndexType j = 0; j < kHalfDimensions / kTileHeight; ++j)
        {
          auto biasesTile = reinterpret_cast<const vec_t*>(
              &biases_[j * kTileHeight]);
          for (IndexType k = 0; k < kNumRegs; ++k)
            acc[k] = biasesTile[k];

          for (const auto index : active)
          {
            const IndexType offset = kHalfDimensions * index + j * kTileHeight;
            auto column = reinterpret_cast<const vec_t*>(&weights_[offset]);

            for (unsigned k = 0; k < kNumRegs; ++k)
              acc[k] = vec_add_16(acc[k], column[k]);
          }

          auto accTile = reinterpret_cast<vec_t*>(
              &accumulator.accumulation[c][0][j * kTileHeight]);
          for (unsigned k = 0; k < kNumRegs; k++)
            vec_store(&accTile[k], acc[k]);
        }

  #else
        std::memcpy(accumulator.accumulation[c][0], biases_,
            kHalfDimensions * sizeof(BiasType));

        for (const auto index : active)
        {
          const IndexType offset = kHalfDimensions * index;

          for (IndexType j = 0; j < kHalfDimensions; ++j)
            accumulator.accumulation[c][0][j] += weights_[offset + j];
        }
  #endif
      }

  #if defined(USE_MMX)
      _mm_empty();
  #endif
    }

    using BiasType = std::int16_t;
    using WeightType = std::int16_t;

    alignas(kCacheLineSize) BiasType biases_[kHalfDimensions];
    alignas(kCacheLineSize)
        WeightType weights_[kHalfDimensions * kInputDimensions];
  };

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
