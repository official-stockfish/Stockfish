// Constants used in NNUE evaluation function

#ifndef NNUE_COMMON_H_INCLUDED
#define NNUE_COMMON_H_INCLUDED

#if defined(USE_AVX2)
#include <immintrin.h>

#elif defined(USE_SSE41)
#include <smmintrin.h>

#elif defined(USE_SSSE3)
#include <tmmintrin.h>

#elif defined(USE_SSE2)
#include <emmintrin.h>
#endif

namespace Eval::NNUE {

  // Version of the evaluation file
  constexpr std::uint32_t kVersion = 0x7AF32F16u;

  // Constant used in evaluation value calculation
  constexpr int FV_SCALE = 16;
  constexpr int kWeightScaleBits = 6;

  // Size of cache line (in bytes)
  constexpr std::size_t kCacheLineSize = 64;

  // SIMD width (in bytes)
  #if defined(USE_AVX2)
  constexpr std::size_t kSimdWidth = 32;

  #elif defined(USE_SSE2)
  constexpr std::size_t kSimdWidth = 16;

  #elif defined(IS_ARM)
  constexpr std::size_t kSimdWidth = 16;
  #endif

  constexpr std::size_t kMaxSimdWidth = 32;

  // Type of input feature after conversion
  using TransformedFeatureType = std::uint8_t;
  using IndexType = std::uint32_t;

  // Round n up to be a multiple of base
  template <typename IntType>
  constexpr IntType CeilToMultiple(IntType n, IntType base) {
    return (n + base - 1) / base * base;
  }

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_COMMON_H_INCLUDED
