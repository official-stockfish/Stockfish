/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

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

#include <cstring> // std::memset()

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
  typedef __m512i vec_t;
  typedef __m256i psqt_vec_t;
  #define vec_load(a) _mm512_load_si512(a)
  #define vec_store(a,b) _mm512_store_si512(a,b)
  #define vec_add_16(a,b) _mm512_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm512_sub_epi16(a,b)
  #define vec_mul_16(a,b) _mm512_mullo_epi16(a,b)
  #define vec_zero() _mm512_setzero_epi32()
  #define vec_set_16(a) _mm512_set1_epi16(a)
  #define vec_max_16(a,b) _mm512_max_epi16(a,b)
  #define vec_min_16(a,b) _mm512_min_epi16(a,b)
  inline vec_t vec_msb_pack_16(vec_t a, vec_t b){
    vec_t compacted = _mm512_packs_epi16(_mm512_srli_epi16(a,7),_mm512_srli_epi16(b,7));
    return _mm512_permutexvar_epi64(_mm512_setr_epi64(0, 2, 4, 6, 1, 3, 5, 7), compacted);
  }
  #define vec_load_psqt(a) _mm256_load_si256(a)
  #define vec_store_psqt(a,b) _mm256_store_si256(a,b)
  #define vec_add_psqt_32(a,b) _mm256_add_epi32(a,b)
  #define vec_sub_psqt_32(a,b) _mm256_sub_epi32(a,b)
  #define vec_zero_psqt() _mm256_setzero_si256()
  #define NumRegistersSIMD 32
  #define MaxChunkSize 64

  #elif USE_AVX2
  typedef __m256i vec_t;
  typedef __m256i psqt_vec_t;
  #define vec_load(a) _mm256_load_si256(a)
  #define vec_store(a,b) _mm256_store_si256(a,b)
  #define vec_add_16(a,b) _mm256_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm256_sub_epi16(a,b)
  #define vec_mul_16(a,b) _mm256_mullo_epi16(a,b)
  #define vec_zero() _mm256_setzero_si256()
  #define vec_set_16(a) _mm256_set1_epi16(a)
  #define vec_max_16(a,b) _mm256_max_epi16(a,b)
  #define vec_min_16(a,b) _mm256_min_epi16(a,b)
  inline vec_t vec_msb_pack_16(vec_t a, vec_t b){
    vec_t compacted = _mm256_packs_epi16(_mm256_srli_epi16(a,7), _mm256_srli_epi16(b,7));
    return _mm256_permute4x64_epi64(compacted, 0b11011000);
  }
  #define vec_load_psqt(a) _mm256_load_si256(a)
  #define vec_store_psqt(a,b) _mm256_store_si256(a,b)
  #define vec_add_psqt_32(a,b) _mm256_add_epi32(a,b)
  #define vec_sub_psqt_32(a,b) _mm256_sub_epi32(a,b)
  #define vec_zero_psqt() _mm256_setzero_si256()
  #define NumRegistersSIMD 16
  #define MaxChunkSize 32

  #elif USE_SSE2
  typedef __m128i vec_t;
  typedef __m128i psqt_vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) _mm_add_epi16(a,b)
  #define vec_sub_16(a,b) _mm_sub_epi16(a,b)
  #define vec_mul_16(a,b) _mm_mullo_epi16(a,b)
  #define vec_zero() _mm_setzero_si128()
  #define vec_set_16(a) _mm_set1_epi16(a)
  #define vec_max_16(a,b) _mm_max_epi16(a,b)
  #define vec_min_16(a,b) _mm_min_epi16(a,b)
  #define vec_msb_pack_16(a,b) _mm_packs_epi16(_mm_srli_epi16(a,7),_mm_srli_epi16(b,7))
  #define vec_load_psqt(a) (*(a))
  #define vec_store_psqt(a,b) *(a)=(b)
  #define vec_add_psqt_32(a,b) _mm_add_epi32(a,b)
  #define vec_sub_psqt_32(a,b) _mm_sub_epi32(a,b)
  #define vec_zero_psqt() _mm_setzero_si128()
  #define NumRegistersSIMD (Is64Bit ? 16 : 8)
  #define MaxChunkSize 16

  #elif USE_MMX
  typedef __m64 vec_t;
  typedef __m64 psqt_vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) _mm_add_pi16(a,b)
  #define vec_sub_16(a,b) _mm_sub_pi16(a,b)
  #define vec_mul_16(a,b) _mm_mullo_pi16(a,b)
  #define vec_zero() _mm_setzero_si64()
  #define vec_set_16(a) _mm_set1_pi16(a)
  inline vec_t vec_max_16(vec_t a,vec_t b){
    vec_t comparison = _mm_cmpgt_pi16(a,b);
    return _mm_or_si64(_mm_and_si64(comparison, a), _mm_andnot_si64(comparison, b));
  }
  inline vec_t vec_min_16(vec_t a,vec_t b){
    vec_t comparison = _mm_cmpgt_pi16(a,b);
    return _mm_or_si64(_mm_and_si64(comparison, b), _mm_andnot_si64(comparison, a));
  }
  #define vec_msb_pack_16(a,b) _mm_packs_pi16(_mm_srli_pi16(a,7),_mm_srli_pi16(b,7))
  #define vec_load_psqt(a) (*(a))
  #define vec_store_psqt(a,b) *(a)=(b)
  #define vec_add_psqt_32(a,b) _mm_add_pi32(a,b)
  #define vec_sub_psqt_32(a,b) _mm_sub_pi32(a,b)
  #define vec_zero_psqt() _mm_setzero_si64()
  #define vec_cleanup() _mm_empty()
  #define NumRegistersSIMD 8
  #define MaxChunkSize 8

  #elif USE_NEON
  typedef int16x8_t vec_t;
  typedef int32x4_t psqt_vec_t;
  #define vec_load(a) (*(a))
  #define vec_store(a,b) *(a)=(b)
  #define vec_add_16(a,b) vaddq_s16(a,b)
  #define vec_sub_16(a,b) vsubq_s16(a,b)
  #define vec_mul_16(a,b) vmulq_s16(a,b)
  #define vec_zero() vec_t{0}
  #define vec_set_16(a) vdupq_n_s16(a)
  #define vec_max_16(a,b) vmaxq_s16(a,b)
  #define vec_min_16(a,b) vminq_s16(a,b)
  inline vec_t vec_msb_pack_16(vec_t a, vec_t b){
    const int8x8_t shifta = vshrn_n_s16(a, 7);
    const int8x8_t shiftb = vshrn_n_s16(b, 7);
    const int8x16_t compacted = vcombine_s8(shifta,shiftb);
    return *reinterpret_cast<const vec_t*> (&compacted);
  }
  #define vec_load_psqt(a) (*(a))
  #define vec_store_psqt(a,b) *(a)=(b)
  #define vec_add_psqt_32(a,b) vaddq_s32(a,b)
  #define vec_sub_psqt_32(a,b) vsubq_s32(a,b)
  #define vec_zero_psqt() psqt_vec_t{0}
  #define NumRegistersSIMD 16
  #define MaxChunkSize 16

  #else
  #undef VECTOR

  #endif


  #ifdef VECTOR

      // Compute optimal SIMD register count for feature transformer accumulation.

      // We use __m* types as template arguments, which causes GCC to emit warnings
      // about losing some attribute information. This is irrelevant to us as we
      // only take their size, so the following pragma are harmless.
      #if defined(__GNUC__)
      #pragma GCC diagnostic push
      #pragma GCC diagnostic ignored "-Wignored-attributes"
      #endif

      template <typename SIMDRegisterType,
                typename LaneType,
                int      NumLanes,
                int      MaxRegisters>
      static constexpr int BestRegisterCount()
      {
          #define RegisterSize  sizeof(SIMDRegisterType)
          #define LaneSize      sizeof(LaneType)

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

      static constexpr int NumRegs     = BestRegisterCount<vec_t, WeightType, TransformedFeatureDimensions, NumRegistersSIMD>();
      static constexpr int NumPsqtRegs = BestRegisterCount<psqt_vec_t, PSQTWeightType, PSQTBuckets, NumRegistersSIMD>();
      #if defined(__GNUC__)
      #pragma GCC diagnostic pop
      #endif
  #endif



  // Input feature converter
  class FeatureTransformer {

   private:
    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

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
    static constexpr IndexType OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr std::size_t BufferSize =
        OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() {
      return FeatureSet::HashValue ^ (OutputDimensions * 2);
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {

      read_little_endian<BiasType      >(stream, biases     , HalfDimensions                  );
      read_little_endian<WeightType    >(stream, weights    , HalfDimensions * InputDimensions);
      read_little_endian<PSQTWeightType>(stream, psqtWeights, PSQTBuckets    * InputDimensions);

      return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const {

      write_little_endian<BiasType      >(stream, biases     , HalfDimensions                  );
      write_little_endian<WeightType    >(stream, weights    , HalfDimensions * InputDimensions);
      write_little_endian<PSQTWeightType>(stream, psqtWeights, PSQTBuckets    * InputDimensions);

      return !stream.fail();
    }

    // Convert input features
    std::int32_t transform(const Position& pos, OutputType* output, int bucket) const {
      update_accumulator<WHITE>(pos);
      update_accumulator<BLACK>(pos);

      const Color perspectives[2] = {pos.side_to_move(), ~pos.side_to_move()};
      const auto& accumulation = pos.state()->accumulator.accumulation;
      const auto& psqtAccumulation = pos.state()->accumulator.psqtAccumulation;

      const auto psqt = (
            psqtAccumulation[perspectives[0]][bucket]
          - psqtAccumulation[perspectives[1]][bucket]
        ) / 2;


      for (IndexType p = 0; p < 2; ++p)
      {
          const IndexType offset = (HalfDimensions / 2) * p;

#if defined(VECTOR)

          constexpr IndexType OutputChunkSize = MaxChunkSize;
          static_assert((HalfDimensions / 2) % OutputChunkSize == 0);
          constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

          vec_t Zero = vec_zero();
          vec_t One = vec_set_16(127);

          const vec_t* in0 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][0]));
          const vec_t* in1 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
                vec_t* out = reinterpret_cast<      vec_t*>(output + offset);

          for (IndexType j = 0; j < NumOutputChunks; j += 1)
          {
              const vec_t sum0a = vec_max_16(vec_min_16(in0[j * 2 + 0], One), Zero);
              const vec_t sum0b = vec_max_16(vec_min_16(in0[j * 2 + 1], One), Zero);
              const vec_t sum1a = vec_max_16(vec_min_16(in1[j * 2 + 0], One), Zero);
              const vec_t sum1b = vec_max_16(vec_min_16(in1[j * 2 + 1], One), Zero);

              const vec_t pa = vec_mul_16(sum0a, sum1a);
              const vec_t pb = vec_mul_16(sum0b, sum1b);

              out[j] = vec_msb_pack_16(pa, pb);
          }

#else

          for (IndexType j = 0; j < HalfDimensions / 2; ++j) {
              BiasType sum0 = accumulation[static_cast<int>(perspectives[p])][j + 0];
              BiasType sum1 = accumulation[static_cast<int>(perspectives[p])][j + HalfDimensions / 2];
              sum0 = std::max<int>(0, std::min<int>(127, sum0));
              sum1 = std::max<int>(0, std::min<int>(127, sum1));
              output[offset + j] = static_cast<OutputType>(sum0 * sum1 / 128);
          }

#endif
      }

#if defined(vec_cleanup)
      vec_cleanup();
#endif

      return psqt;

   } // end of function transform()



   private:
    template<Color Perspective>
    void update_accumulator(const Position& pos) const {

      // The size must be enough to contain the largest possible update.
      // That might depend on the feature set and generally relies on the
      // feature set's update cost calculation to be correct and never
      // allow updates with more added/removed features than MaxActiveDimensions.

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
      while (st->previous && !st->accumulator.computed[Perspective])
      {
        // This governs when a full feature refresh is needed and how many
        // updates are better than just one full refresh.
        if (   FeatureSet::requires_refresh(st, Perspective)
            || (gain -= FeatureSet::update_cost(st) + 1) < 0)
          break;
        next = st;
        st = st->previous;
      }

      if (st->accumulator.computed[Perspective])
      {
        if (next == nullptr)
          return;

        // Update incrementally in two steps. First, we update the "next"
        // accumulator. Then, we update the current accumulator (pos.state()).

        // Gather all features to be updated.
        const Square ksq = pos.square<KING>(Perspective);
        FeatureSet::IndexList removed[2], added[2];
        FeatureSet::append_changed_indices<Perspective>(
          ksq, next->dirtyPiece, removed[0], added[0]);
        for (StateInfo *st2 = pos.state(); st2 != next; st2 = st2->previous)
          FeatureSet::append_changed_indices<Perspective>(
            ksq, st2->dirtyPiece, removed[1], added[1]);

        // Mark the accumulators as computed.
        next->accumulator.computed[Perspective] = true;
        pos.state()->accumulator.computed[Perspective] = true;

        // Now update the accumulators listed in states_to_update[], where the last element is a sentinel.
        StateInfo *states_to_update[3] =
          { next, next == pos.state() ? nullptr : pos.state(), nullptr };
  #ifdef VECTOR
        for (IndexType j = 0; j < HalfDimensions / TileHeight; ++j)
        {
          // Load accumulator
          auto accTile = reinterpret_cast<vec_t*>(
            &st->accumulator.accumulation[Perspective][j * TileHeight]);
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
              &states_to_update[i]->accumulator.accumulation[Perspective][j * TileHeight]);
            for (IndexType k = 0; k < NumRegs; ++k)
              vec_store(&accTile[k], acc[k]);
          }
        }

        for (IndexType j = 0; j < PSQTBuckets / PsqtTileHeight; ++j)
        {
          // Load accumulator
          auto accTilePsqt = reinterpret_cast<psqt_vec_t*>(
            &st->accumulator.psqtAccumulation[Perspective][j * PsqtTileHeight]);
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
              &states_to_update[i]->accumulator.psqtAccumulation[Perspective][j * PsqtTileHeight]);
            for (std::size_t k = 0; k < NumPsqtRegs; ++k)
              vec_store_psqt(&accTilePsqt[k], psqt[k]);
          }
        }

  #else
        for (IndexType i = 0; states_to_update[i]; ++i)
        {
          std::memcpy(states_to_update[i]->accumulator.accumulation[Perspective],
              st->accumulator.accumulation[Perspective],
              HalfDimensions * sizeof(BiasType));

          for (std::size_t k = 0; k < PSQTBuckets; ++k)
            states_to_update[i]->accumulator.psqtAccumulation[Perspective][k] = st->accumulator.psqtAccumulation[Perspective][k];

          st = states_to_update[i];

          // Difference calculation for the deactivated features
          for (const auto index : removed[i])
          {
            const IndexType offset = HalfDimensions * index;

            for (IndexType j = 0; j < HalfDimensions; ++j)
              st->accumulator.accumulation[Perspective][j] -= weights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
              st->accumulator.psqtAccumulation[Perspective][k] -= psqtWeights[index * PSQTBuckets + k];
          }

          // Difference calculation for the activated features
          for (const auto index : added[i])
          {
            const IndexType offset = HalfDimensions * index;

            for (IndexType j = 0; j < HalfDimensions; ++j)
              st->accumulator.accumulation[Perspective][j] += weights[offset + j];

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
              st->accumulator.psqtAccumulation[Perspective][k] += psqtWeights[index * PSQTBuckets + k];
          }
        }
  #endif
      }
      else
      {
        // Refresh the accumulator
        auto& accumulator = pos.state()->accumulator;
        accumulator.computed[Perspective] = true;
        FeatureSet::IndexList active;
        FeatureSet::append_active_indices<Perspective>(pos, active);

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
              &accumulator.accumulation[Perspective][j * TileHeight]);
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
            &accumulator.psqtAccumulation[Perspective][j * PsqtTileHeight]);
          for (std::size_t k = 0; k < NumPsqtRegs; ++k)
            vec_store_psqt(&accTilePsqt[k], psqt[k]);
        }

  #else
        std::memcpy(accumulator.accumulation[Perspective], biases,
            HalfDimensions * sizeof(BiasType));

        for (std::size_t k = 0; k < PSQTBuckets; ++k)
          accumulator.psqtAccumulation[Perspective][k] = 0;

        for (const auto index : active)
        {
          const IndexType offset = HalfDimensions * index;

          for (IndexType j = 0; j < HalfDimensions; ++j)
            accumulator.accumulation[Perspective][j] += weights[offset + j];

          for (std::size_t k = 0; k < PSQTBuckets; ++k)
            accumulator.psqtAccumulation[Perspective][k] += psqtWeights[index * PSQTBuckets + k];
        }
  #endif
      }

  #if defined(USE_MMX)
      _mm_empty();
  #endif
    }

    alignas(CacheLineSize) BiasType biases[HalfDimensions];
    alignas(CacheLineSize) WeightType weights[HalfDimensions * InputDimensions];
    alignas(CacheLineSize) PSQTWeightType psqtWeights[InputDimensions * PSQTBuckets];
  };

}  // namespace Stockfish::Eval::NNUE

#endif // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
