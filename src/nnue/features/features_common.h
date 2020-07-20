//Common header of input features of NNUE evaluation function

#ifndef NNUE_FEATURES_COMMON_H_INCLUDED
#define NNUE_FEATURES_COMMON_H_INCLUDED

#include "../../evaluate.h"
#include "../nnue_common.h"

namespace Eval::NNUE::Features {

  // Index list type
  class IndexList;

  // Class template that represents the feature set
  template <typename... FeatureTypes>
  class FeatureSet;

  // Type of timing to perform all calculations instead of difference calculation
  enum class TriggerEvent {

    kNone, // Calculate the difference whenever possible
    kFriendKingMoved, // calculate all when own king moves
    kEnemyKingMoved, // do all calculations when enemy king moves
    kAnyKingMoved, // do all calculations if either king moves
    kAnyPieceMoved, // always do all calculations
  };

  // turn side or other side
  enum class Side {

    kFriend, // turn side
    kEnemy, // opponent
  };

}  // namespace Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURES_COMMON_H_INCLUDED
