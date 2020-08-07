//Common header of input features of NNUE evaluation function

#ifndef _NNUE_FEATURES_COMMON_H_
#define _NNUE_FEATURES_COMMON_H_

#if defined(EVAL_NNUE)

#include "../../../evaluate.h"
#include "../nnue_common.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Index list type
class IndexList;

// Class template that represents the feature set
template <typename... FeatureTypes>
class FeatureSet;

// Type of timing to perform all calculations instead of difference calculation
enum class TriggerEvent {
  kNone, // Calculate the difference whenever possible
  kFriendKingMoved, // calculate all when own ball moves
  kEnemyKingMoved, // do all calculations when enemy balls move
  kAnyKingMoved, // do all calculations if either ball moves
  kAnyPieceMoved, // always do all calculations
};

// turn side or other side
enum class Side {
  kFriend, // turn side
  kEnemy, // opponent
};

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
