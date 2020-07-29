// Input features and network structure used in NNUE evaluation function

#ifndef NNUE_ARCHITECTURE_H_INCLUDED
#define NNUE_ARCHITECTURE_H_INCLUDED

// include a header that defines the input features and network structure
#include "architectures/halfkp_256x2-32-32.h"
//#include "architectures/halfkp_384x2-32-32.h"

namespace Eval::NNUE {

  static_assert(kTransformedFeatureDimensions % kMaxSimdWidth == 0, "");
  static_assert(Network::kOutputDimensions == 1, "");
  static_assert(std::is_same<Network::OutputType, std::int32_t>::value, "");

  // List of timings to perform all calculations instead of difference calculation
  constexpr auto kRefreshTriggers = RawFeatures::kRefreshTriggers;

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_ARCHITECTURE_H_INCLUDED
