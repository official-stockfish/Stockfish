//Definition of input feature P of NNUE evaluation function

#ifndef _NNUE_FEATURES_P_H_
#define _NNUE_FEATURES_P_H_

#if defined(EVAL_NNUE)

#include "../../../evaluate.h"
#include "features_common.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Feature P: BonaPiece of pieces other than balls
class P {
 public:
  // feature quantity name
  static constexpr const char* kName = "P";
  // Hash value embedded in the evaluation function file
  static constexpr std::uint32_t kHashValue = 0x764CFB4Bu;
  // number of feature dimensions
  static constexpr IndexType kDimensions = fe_end;
  // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
  static constexpr IndexType kMaxActiveDimensions = PIECE_NUMBER_KING;
  // Timing of full calculation instead of difference calculation
  static constexpr TriggerEvent kRefreshTrigger = TriggerEvent::kNone;

  // Get a list of indices with a value of 1 among the features
  static void AppendActiveIndices(const Position& pos, Color perspective,
                                  IndexList* active);

  // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
  static void AppendChangedIndices(const Position& pos, Color perspective,
                                   IndexList* removed, IndexList* added);
};

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
