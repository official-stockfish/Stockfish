// NNUE評価関数で用いる入力特徴量とネットワーク構造

#ifndef _NNUE_ARCHITECTURE_H_
#define _NNUE_ARCHITECTURE_H_

#if defined(EVAL_NNUE)

// 入力特徴量とネットワーク構造が定義されたヘッダをincludeする
//#include "architectures/k-p_256x2-32-32.h"
#include "architectures/k-p-cr_256x2-32-32.h"
//#include "architectures/halfkp_256x2-32-32.h"

namespace Eval {

namespace NNUE {

static_assert(kTransformedFeatureDimensions % kMaxSimdWidth == 0, "");
static_assert(Network::kOutputDimensions == 1, "");
static_assert(std::is_same<Network::OutputType, std::int32_t>::value, "");

// 差分計算の代わりに全計算を行うタイミングのリスト
constexpr auto kRefreshTriggers = RawFeatures::kRefreshTriggers;

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
