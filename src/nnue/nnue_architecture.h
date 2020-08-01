// Input features and network structure used in NNUE evaluation function

#ifndef NNUE_ARCHITECTURE_H_INCLUDED
#define NNUE_ARCHITECTURE_H_INCLUDED

// Defines the network structure
#include "architectures/halfkp_256x2-32-32.h"

namespace Eval::NNUE {

  static_assert(kTransformedFeatureDimensions % kMaxSimdWidth == 0, "");
  static_assert(Network::kOutputDimensions == 1, "");
  static_assert(std::is_same<Network::OutputType, std::int32_t>::value, "");

  // Trigger for full calculation instead of difference calculation
  constexpr auto kRefreshTriggers = RawFeatures::kRefreshTriggers;

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_ARCHITECTURE_H_INCLUDED
