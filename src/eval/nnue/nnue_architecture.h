// Input features and network structure used in NNUE evaluation function

#ifndef _NNUE_ARCHITECTURE_H_
#define _NNUE_ARCHITECTURE_H_

#if defined(EVAL_NNUE)

// include a header that defines the input features and network structure
//#include "architectures/k-p_256x2-32-32.h"
//#include "architectures/k-p-cr_256x2-32-32.h"
//#include "architectures/k-p-cr-ep_256x2-32-32.h"
#include "architectures/halfkp_256x2-32-32.h"
//#include "architectures/halfkp-cr-ep_256x2-32-32.h"

namespace Eval {

namespace NNUE {

static_assert(kTransformedFeatureDimensions % kMaxSimdWidth == 0, "");
static_assert(Network::kOutputDimensions == 1, "");
static_assert(std::is_same<Network::OutputType, std::int32_t>::value, "");

// List of timings to perform all calculations instead of difference calculation
constexpr auto kRefreshTriggers = RawFeatures::kRefreshTriggers;

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
