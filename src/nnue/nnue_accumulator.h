// Class for difference calculation of NNUE evaluation function

#ifndef NNUE_ACCUMULATOR_H_INCLUDED
#define NNUE_ACCUMULATOR_H_INCLUDED

#include "nnue_architecture.h"

namespace Eval::NNUE {

  // Class that holds the result of affine transformation of input features
  // Keep the evaluation value that is the final output together
  struct alignas(32) Accumulator {
    std::int16_t
        accumulation[2][kRefreshTriggers.size()][kTransformedFeatureDimensions];
    Value score = VALUE_ZERO;
    bool computed_accumulation = false;
    bool computed_score = false;
  };

}  // namespace Eval::NNUE

#endif // NNUE_ACCUMULATOR_H_INCLUDED
