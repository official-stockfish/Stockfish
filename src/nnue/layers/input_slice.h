// NNUE evaluation function layer InputSlice definition

#ifndef NNUE_LAYERS_INPUT_SLICE_H_INCLUDED
#define NNUE_LAYERS_INPUT_SLICE_H_INCLUDED

#include "../nnue_common.h"

namespace Eval::NNUE::Layers {

// Input layer
template <IndexType OutputDimensions, IndexType Offset = 0>
class InputSlice {
 public:
  // Need to maintain alignment
  static_assert(Offset % kMaxSimdWidth == 0, "");

  // Output type
  using OutputType = TransformedFeatureType;

  // Output dimensionality
  static constexpr IndexType kOutputDimensions = OutputDimensions;

  // Size of forward propagation buffer used from the input layer to this layer
  static constexpr std::size_t kBufferSize = 0;

  // Hash value embedded in the evaluation file
  static constexpr std::uint32_t GetHashValue() {
    std::uint32_t hash_value = 0xEC42E90Du;
    hash_value ^= kOutputDimensions ^ (Offset << 10);
    return hash_value;
  }

  // Read network parameters
  bool ReadParameters(std::istream& /*stream*/) {
    return true;
  }

  // Forward propagation
  const OutputType* Propagate(
      const TransformedFeatureType* transformed_features,
      char* /*buffer*/) const {
    return transformed_features + Offset;
  }

 private:
};

}  // namespace Layers

#endif // #ifndef NNUE_LAYERS_INPUT_SLICE_H_INCLUDED
