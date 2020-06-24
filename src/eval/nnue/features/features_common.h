// NNUE評価関数の入力特徴量の共通ヘッダ

#ifndef _NNUE_FEATURES_COMMON_H_
#define _NNUE_FEATURES_COMMON_H_

#if defined(EVAL_NNUE)

#include "../../../evaluate.h"
#include "../nnue_common.h"

namespace Eval {

namespace NNUE {

namespace Features {

// インデックスリストの型
class IndexList;

// 特徴量セットを表すクラステンプレート
template <typename... FeatureTypes>
class FeatureSet;

// 差分計算の代わりに全計算を行うタイミングの種類
enum class TriggerEvent {
  kNone,             // 可能な場合は常に差分計算する
  kFriendKingMoved,  // 自玉が移動した場合に全計算する
  kEnemyKingMoved,   // 敵玉が移動した場合に全計算する
  kAnyKingMoved,     // どちらかの玉が移動した場合に全計算する
  kAnyPieceMoved,    // 常に全計算する
};

// 手番側or相手側
enum class Side {
  kFriend,  // 手番側
  kEnemy,   // 相手側
};

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
