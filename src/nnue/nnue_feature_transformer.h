// A class that converts the input features of the NNUE evaluation function

#ifndef _NNUE_FEATURE_TRANSFORMER_H_
#define _NNUE_FEATURE_TRANSFORMER_H_

#if defined(EVAL_NNUE)

#include "nnue_common.h"
#include "nnue_architecture.h"
#include "features/index_list.h"

#include <cstring> // std::memset()

namespace Eval {

namespace NNUE {

// Input feature converter
class FeatureTransformer {
 private:
  // number of output dimensions for one side
  static constexpr IndexType kHalfDimensions = kTransformedFeatureDimensions;

 public:
  // output type
  using OutputType = TransformedFeatureType;

  // number of input/output dimensions
  static constexpr IndexType kInputDimensions = RawFeatures::kDimensions;
  static constexpr IndexType kOutputDimensions = kHalfDimensions * 2;

  // size of forward propagation buffer
  static constexpr std::size_t kBufferSize =
      kOutputDimensions * sizeof(OutputType);

  // Hash value embedded in the evaluation function file
  static constexpr std::uint32_t GetHashValue() {
    return RawFeatures::kHashValue ^ kOutputDimensions;
  }

  // a string representing the structure
  static std::string GetStructureString() {
    return RawFeatures::GetName() + "[" +
        std::to_string(kInputDimensions) + "->" +
        std::to_string(kHalfDimensions) + "x2]";
  }

  // read parameters
  bool ReadParameters(std::istream& stream) {
    stream.read(reinterpret_cast<char*>(biases_),
                kHalfDimensions * sizeof(BiasType));
    stream.read(reinterpret_cast<char*>(weights_),
                kHalfDimensions * kInputDimensions * sizeof(WeightType));
    return !stream.fail();
  }

  // write parameters
  bool WriteParameters(std::ostream& stream) const {
    stream.write(reinterpret_cast<const char*>(biases_),
                 kHalfDimensions * sizeof(BiasType));
    stream.write(reinterpret_cast<const char*>(weights_),
                 kHalfDimensions * kInputDimensions * sizeof(WeightType));
    return !stream.fail();
  }

  // proceed with the difference calculation if possible
  bool UpdateAccumulatorIfPossible(const Position& pos) const {
    const auto now = pos.state();
    if (now->accumulator.computed_accumulation) {
      return true;
    }
    const auto prev = now->previous;
    if (prev && prev->accumulator.computed_accumulation) {
      UpdateAccumulator(pos);
      return true;
    }
    return false;
  }

  // convert input features
  void Transform(const Position& pos, OutputType* output, bool refresh) const {
    if (refresh || !UpdateAccumulatorIfPossible(pos)) {
      RefreshAccumulator(pos);
    }
    const auto& accumulation = pos.state()->accumulator.accumulation;
#if defined(USE_AVX2)
    constexpr IndexType kNumChunks = kHalfDimensions / kSimdWidth;
    constexpr int kControl = 0b11011000;
    const __m256i kZero = _mm256_setzero_si256();
#elif defined(USE_SSSE3)
    constexpr IndexType kNumChunks = kHalfDimensions / kSimdWidth;
    const __m128i kZero = _mm_setzero_si128();
#ifndef USE_SSE41
    const __m128i k0x80s = _mm_set1_epi8(-128);
#endif
#elif defined(IS_ARM)
    constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth / 2);
    const int8x8_t kZero = {0};
#endif
    const Color perspectives[2] = {pos.side_to_move(), ~pos.side_to_move()};
    for (IndexType p = 0; p < 2; ++p) {
      const IndexType offset = kHalfDimensions * p;
#if defined(USE_AVX2)
      auto out = reinterpret_cast<__m256i*>(&output[offset]);
      for (IndexType j = 0; j < kNumChunks; ++j) {
        __m256i sum0 =
#if defined(__MINGW32__) || defined(__MINGW64__)
          // HACK: Use _mm256_loadu_si256() instead of _mm256_load_si256. Because the binary
          //       compiled with g++ in MSYS2 crashes here because the output memory is not aligned
          //       even though alignas is specified.
          _mm256_loadu_si256
#else
          _mm256_load_si256
#endif
          (&reinterpret_cast<const __m256i*>(
            accumulation[perspectives[p]][0])[j * 2 + 0]);
        __m256i sum1 =
#if defined(__MINGW32__) || defined(__MINGW64__)
          _mm256_loadu_si256
#else
          _mm256_load_si256
#endif
          (&reinterpret_cast<const __m256i*>(
            accumulation[perspectives[p]][0])[j * 2 + 1]);
        for (IndexType i = 1; i < kRefreshTriggers.size(); ++i) {
          sum0 = _mm256_add_epi16(sum0, reinterpret_cast<const __m256i*>(
              accumulation[perspectives[p]][i])[j * 2 + 0]);
          sum1 = _mm256_add_epi16(sum1, reinterpret_cast<const __m256i*>(
              accumulation[perspectives[p]][i])[j * 2 + 1]);
        }
#if defined(__MINGW32__) || defined(__MINGW64__)
        _mm256_storeu_si256
#else
        _mm256_store_si256
#endif
        (&out[j], _mm256_permute4x64_epi64(_mm256_max_epi8(
            _mm256_packs_epi16(sum0, sum1), kZero), kControl));
      }
#elif defined(USE_SSSE3)
      auto out = reinterpret_cast<__m128i*>(&output[offset]);
      for (IndexType j = 0; j < kNumChunks; ++j) {
        __m128i sum0 = _mm_load_si128(&reinterpret_cast<const __m128i*>(
            accumulation[perspectives[p]][0])[j * 2 + 0]);
        __m128i sum1 = _mm_load_si128(&reinterpret_cast<const __m128i*>(
            accumulation[perspectives[p]][0])[j * 2 + 1]);
        for (IndexType i = 1; i < kRefreshTriggers.size(); ++i) {
          sum0 = _mm_add_epi16(sum0, reinterpret_cast<const __m128i*>(
              accumulation[perspectives[p]][i])[j * 2 + 0]);
          sum1 = _mm_add_epi16(sum1, reinterpret_cast<const __m128i*>(
              accumulation[perspectives[p]][i])[j * 2 + 1]);
        }
  	const __m128i packedbytes = _mm_packs_epi16(sum0, sum1);
 
        _mm_store_si128(&out[j],
#ifdef USE_SSE41
          _mm_max_epi8(packedbytes, kZero)
#else
          _mm_subs_epi8(_mm_adds_epi8(packedbytes, k0x80s), k0x80s)
#endif
        );
      }
#elif defined(IS_ARM)
      const auto out = reinterpret_cast<int8x8_t*>(&output[offset]);
      for (IndexType j = 0; j < kNumChunks; ++j) {
        int16x8_t sum = reinterpret_cast<const int16x8_t*>(
            accumulation[perspectives[p]][0])[j];
        for (IndexType i = 1; i < kRefreshTriggers.size(); ++i) {
          sum = vaddq_s16(sum, reinterpret_cast<const int16x8_t*>(
              accumulation[perspectives[p]][i])[j]);
        }
        out[j] = vmax_s8(vqmovn_s16(sum), kZero);
      }
#else
      for (IndexType j = 0; j < kHalfDimensions; ++j) {
        BiasType sum = accumulation[static_cast<int>(perspectives[p])][0][j];
        for (IndexType i = 1; i < kRefreshTriggers.size(); ++i) {
          sum += accumulation[static_cast<int>(perspectives[p])][i][j];
        }
        output[offset + j] = static_cast<OutputType>(
            std::max<int>(0, std::min<int>(127, sum)));
      }
#endif
    }
  }

 private:
  // Calculate cumulative value without using difference calculation
  void RefreshAccumulator(const Position& pos) const {
    auto& accumulator = pos.state()->accumulator;
    for (IndexType i = 0; i < kRefreshTriggers.size(); ++i) {
      Features::IndexList active_indices[2];
      RawFeatures::AppendActiveIndices(pos, kRefreshTriggers[i],
                                       active_indices);
      for (const auto perspective : Colors) {
        if (i == 0) {
          std::memcpy(accumulator.accumulation[perspective][i], biases_,
                      kHalfDimensions * sizeof(BiasType));
        } else {
          std::memset(accumulator.accumulation[perspective][i], 0,
                      kHalfDimensions * sizeof(BiasType));
        }
        for (const auto index : active_indices[perspective]) {
          const IndexType offset = kHalfDimensions * index;
#if defined(USE_AVX2)
          auto accumulation = reinterpret_cast<__m256i*>(
              &accumulator.accumulation[perspective][i][0]);
          auto column = reinterpret_cast<const __m256i*>(&weights_[offset]);
          constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth / 2);
          for (IndexType j = 0; j < kNumChunks; ++j) {
#if defined(__MINGW32__) || defined(__MINGW64__)
            _mm256_storeu_si256(&accumulation[j], _mm256_add_epi16(_mm256_loadu_si256(&accumulation[j]), column[j]));
#else
            accumulation[j] = _mm256_add_epi16(accumulation[j], column[j]);
#endif
          }
#elif defined(USE_SSE2)
          auto accumulation = reinterpret_cast<__m128i*>(
              &accumulator.accumulation[perspective][i][0]);
          auto column = reinterpret_cast<const __m128i*>(&weights_[offset]);
          constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth / 2);
          for (IndexType j = 0; j < kNumChunks; ++j) {
            accumulation[j] = _mm_add_epi16(accumulation[j], column[j]);
          }
#elif defined(IS_ARM)
          auto accumulation = reinterpret_cast<int16x8_t*>(
              &accumulator.accumulation[perspective][i][0]);
          auto column = reinterpret_cast<const int16x8_t*>(&weights_[offset]);
          constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth / 2);
          for (IndexType j = 0; j < kNumChunks; ++j) {
            accumulation[j] = vaddq_s16(accumulation[j], column[j]);
          }
#else
          for (IndexType j = 0; j < kHalfDimensions; ++j) {
            accumulator.accumulation[perspective][i][j] += weights_[offset + j];
          }
#endif
        }
      }
    }

    accumulator.computed_accumulation = true;
    accumulator.computed_score = false;
  }

  // Calculate cumulative value using difference calculation
  void UpdateAccumulator(const Position& pos) const {
    const auto prev_accumulator = pos.state()->previous->accumulator;
    auto& accumulator = pos.state()->accumulator;
    for (IndexType i = 0; i < kRefreshTriggers.size(); ++i) {
      Features::IndexList removed_indices[2], added_indices[2];
      bool reset[2];
      RawFeatures::AppendChangedIndices(pos, kRefreshTriggers[i],
                                        removed_indices, added_indices, reset);
      for (const auto perspective : Colors) {
#if defined(USE_AVX2)
        constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth / 2);
        auto accumulation = reinterpret_cast<__m256i*>(
            &accumulator.accumulation[perspective][i][0]);
#elif defined(USE_SSE2)
        constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth / 2);
        auto accumulation = reinterpret_cast<__m128i*>(
            &accumulator.accumulation[perspective][i][0]);
#elif defined(IS_ARM)
        constexpr IndexType kNumChunks = kHalfDimensions / (kSimdWidth / 2);
        auto accumulation = reinterpret_cast<int16x8_t*>(
            &accumulator.accumulation[perspective][i][0]);
#endif
        if (reset[perspective]) {
          if (i == 0) {
            std::memcpy(accumulator.accumulation[perspective][i], biases_,
                        kHalfDimensions * sizeof(BiasType));
          } else {
            std::memset(accumulator.accumulation[perspective][i], 0,
                        kHalfDimensions * sizeof(BiasType));
          }
        } else {// Difference calculation for the feature amount changed from 1 to 0
          std::memcpy(accumulator.accumulation[perspective][i],
                      prev_accumulator.accumulation[perspective][i],
                      kHalfDimensions * sizeof(BiasType));
          for (const auto index : removed_indices[perspective]) {
            const IndexType offset = kHalfDimensions * index;
#if defined(USE_AVX2)
            auto column = reinterpret_cast<const __m256i*>(&weights_[offset]);
            for (IndexType j = 0; j < kNumChunks; ++j) {
              accumulation[j] = _mm256_sub_epi16(accumulation[j], column[j]);
            }
#elif defined(USE_SSE2)
            auto column = reinterpret_cast<const __m128i*>(&weights_[offset]);
            for (IndexType j = 0; j < kNumChunks; ++j) {
              accumulation[j] = _mm_sub_epi16(accumulation[j], column[j]);
            }
#elif defined(IS_ARM)
            auto column = reinterpret_cast<const int16x8_t*>(&weights_[offset]);
            for (IndexType j = 0; j < kNumChunks; ++j) {
              accumulation[j] = vsubq_s16(accumulation[j], column[j]);
            }
#else
            for (IndexType j = 0; j < kHalfDimensions; ++j) {
              accumulator.accumulation[perspective][i][j] -=
                  weights_[offset + j];
            }
#endif
          }
        }
        {// Difference calculation for features that changed from 0 to 1
          for (const auto index : added_indices[perspective]) {
            const IndexType offset = kHalfDimensions * index;
#if defined(USE_AVX2)
            auto column = reinterpret_cast<const __m256i*>(&weights_[offset]);
            for (IndexType j = 0; j < kNumChunks; ++j) {
              accumulation[j] = _mm256_add_epi16(accumulation[j], column[j]);
            }
#elif defined(USE_SSE2)
            auto column = reinterpret_cast<const __m128i*>(&weights_[offset]);
            for (IndexType j = 0; j < kNumChunks; ++j) {
              accumulation[j] = _mm_add_epi16(accumulation[j], column[j]);
            }
#elif defined(IS_ARM)
            auto column = reinterpret_cast<const int16x8_t*>(&weights_[offset]);
            for (IndexType j = 0; j < kNumChunks; ++j) {
              accumulation[j] = vaddq_s16(accumulation[j], column[j]);
            }
#else
            for (IndexType j = 0; j < kHalfDimensions; ++j) {
              accumulator.accumulation[perspective][i][j] +=
                  weights_[offset + j];
            }
#endif
          }
        }
      }
    }

    accumulator.computed_accumulation = true;
    accumulator.computed_score = false;
  }

  // parameter type
  using BiasType = std::int16_t;
  using WeightType = std::int16_t;

  // Make the learning class a friend
  friend class Trainer<FeatureTransformer>;

  // parameter
  alignas(kCacheLineSize) BiasType biases_[kHalfDimensions];
  alignas(kCacheLineSize)
      WeightType weights_[kHalfDimensions * kInputDimensions];
};

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
