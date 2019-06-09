// NNUE評価関数で用いる定数など

#ifndef _NNUE_COMMON_H_
#define _NNUE_COMMON_H_

#if defined(EVAL_NNUE)

namespace Eval {

namespace NNUE {

// 評価関数ファイルのバージョンを表す定数
constexpr std::uint32_t kVersion = 0x7AF32F16u;

// 評価値の計算で利用する定数
constexpr int FV_SCALE = 16;
constexpr int kWeightScaleBits = 6;

// キャッシュラインのサイズ（バイト単位）
constexpr std::size_t kCacheLineSize = 64;

// SIMD幅（バイト単位）
#if defined(USE_AVX2)
constexpr std::size_t kSimdWidth = 32;
#elif defined(USE_SSE2)
constexpr std::size_t kSimdWidth = 16;
#elif defined(IS_ARM)
constexpr std::size_t kSimdWidth = 16;
#endif
constexpr std::size_t kMaxSimdWidth = 32;

// 変換後の入力特徴量の型
using TransformedFeatureType = std::uint8_t;

// インデックスの型
using IndexType = std::uint32_t;

// 学習用クラステンプレートの前方宣言
template <typename Layer>
class Trainer;

// n以上で最小のbaseの倍数を求める
template <typename IntType>
constexpr IntType CeilToMultiple(IntType n, IntType base) {
  return (n + base - 1) / base * base;
}

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
