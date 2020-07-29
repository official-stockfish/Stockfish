// NNUE evaluation function layer InputSlice definition

#ifndef NNUE_LAYERS_INPUT_SLICE_H_INCLUDED
#define NNUE_LAYERS_INPUT_SLICE_H_INCLUDED

#include "../nnue_common.h"

namespace Eval::NNUE::Layers {

// input layer
template <IndexType OutputDimensions, IndexType Offset = 0>
class InputSlice {
 public:
  // need to maintain alignment
  static_assert(Offset % kMaxSimdWidth == 0, "");

  // output type
  using OutputType = TransformedFeatureType;

  // output dimensionality
  static constexpr IndexType kOutputDimensions = OutputDimensions;

  // Size of the forward propagation buffer used from the input layer to this layer
  static constexpr std::size_t kBufferSize = 0;

  // Hash value embedded in the evaluation function file
  static constexpr std::uint32_t GetHashValue() {
    std::uint32_t hash_value = 0xEC42E90Du;
    hash_value ^= kOutputDimensions ^ (Offset << 10);
    return hash_value;
  }

  // A string that represents the structure from the input layer to this layer
  static std::string GetStructureString() {
    return "InputSlice[" + std::to_string(kOutputDimensions) + "(" +
        std::to_string(Offset) + ":" +
        std::to_string(Offset + kOutputDimensions) + ")]";
  }

  // read parameters
  bool ReadParameters(std::istream& /*stream*/) {
    return true;
  }

  // forward propagation
  const OutputType* Propagate(
      const TransformedFeatureType* transformed_features,
      char* /*buffer*/) const {
    return transformed_features + Offset;
  }

 private:
};

}  // namespace Layers

#endif // #ifndef NNUE_LAYERS_INPUT_SLICE_H_INCLUDED
