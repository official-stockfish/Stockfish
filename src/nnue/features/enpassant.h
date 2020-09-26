//Definition of input feature quantity EnPassant of NNUE evaluation function

#ifndef _NNUE_FEATURES_ENPASSANT_H_
#define _NNUE_FEATURES_ENPASSANT_H_

#include "../../evaluate.h"
#include "features_common.h"

namespace Eval::NNUE::Features {

  class EnPassant {
  public:
    // feature quantity name
    static constexpr const char* kName = "EnPassant";
    // Hash value embedded in the evaluation function file
    static constexpr std::uint32_t kHashValue = 0x02924F91u;
    // number of feature dimensions
    static constexpr IndexType kDimensions = 8;
    // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
    static constexpr IndexType kMaxActiveDimensions = 1;
    // Timing of full calculation instead of difference calculation
    static constexpr TriggerEvent kRefreshTrigger = TriggerEvent::kNone;

    // Get a list of indices with a value of 1 among the features
    static void AppendActiveIndices(const Position& pos, Color perspective,
      IndexList* active);

    // Get a list of indices whose values have changed from the previous one in the feature quantity
    static void AppendChangedIndices(const Position& pos, Color perspective,
      IndexList* removed, IndexList* added);
  };

}  // namespace Eval::NNUE::Features

#endif
