// Constants used in NNUE evaluation function

#ifndef _NNUE_COMMON_H_
#define _NNUE_COMMON_H_

#if defined(EVAL_NNUE)

#if defined(USE_AVX2)
#include <immintrin.h>
#elif defined(USE_SSE41)
#include <smmintrin.h>
#elif defined(USE_SSSE3)
#include <tmmintrin.h>
#elif defined(USE_SSE2)
#include <emmintrin.h>
#endif

namespace Eval {

namespace NNUE {

// A constant that represents the version of the evaluation function file
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

// index type
using IndexType = std::uint32_t;

// Forward declaration of learning class template
template <typename Layer>
class Trainer;

// find the smallest multiple of n and above
template <typename IntType>
constexpr IntType CeilToMultiple(IntType n, IntType base) {
  return (n + base - 1) / base * base;
}

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
