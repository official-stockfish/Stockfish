//Definition of input features HalfKP of NNUE evaluation function

#ifndef _NNUE_FEATURES_HALF_KP_H_
#define _NNUE_FEATURES_HALF_KP_H_

#if defined(EVAL_NNUE)

#include "../../../evaluate.h"
#include "features_common.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Feature HalfKP: Combination of the position of own ball or enemy ball and the position of pieces other than balls
template <Side AssociatedKing>
class HalfKP {
 public:
  // feature quantity name
  static constexpr const char* kName =
      (AssociatedKing == Side::kFriend) ? "HalfKP(Friend)" : "HalfKP(Enemy)";
  // Hash value embedded in the evaluation function file
  static constexpr std::uint32_t kHashValue =
      0x5D69D5B9u ^ (AssociatedKing == Side::kFriend);
  // number of feature dimensions
  static constexpr IndexType kDimensions =
      static_cast<IndexType>(SQUARE_NB) * static_cast<IndexType>(fe_end);
  // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
  static constexpr IndexType kMaxActiveDimensions = PIECE_NUMBER_KING;
  // Timing of full calculation instead of difference calculation
  static constexpr TriggerEvent kRefreshTrigger =
      (AssociatedKing == Side::kFriend) ?
      TriggerEvent::kFriendKingMoved : TriggerEvent::kEnemyKingMoved;

  // Get a list of indices with a value of 1 among the features
  static void AppendActiveIndices(const Position& pos, Color perspective,
                                  IndexList* active);

  // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
  static void AppendChangedIndices(const Position& pos, Color perspective,
                                   IndexList* removed, IndexList* added);

  // Find the index of the feature quantity from the ball position and BonaPiece
  static IndexType MakeIndex(Square sq_k, BonaPiece p);

 private:
  // Get the piece information
  static void GetPieces(const Position& pos, Color perspective,
                        BonaPiece** pieces, Square* sq_target_k);
};

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
