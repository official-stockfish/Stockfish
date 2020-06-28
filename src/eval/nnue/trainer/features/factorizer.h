// NNUE evaluation function feature conversion class template

#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_H_

#if defined(EVAL_NNUE)

#include "../../nnue_common.h"
#include "../trainer.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Class template that converts input features into learning features
// By default, the learning feature is the same as the original input feature, and specialized as necessary
template <typename FeatureType>
class Factorizer {
 public:
  // Get the dimensionality of the learning feature
  static constexpr IndexType GetDimensions() {
    return FeatureType::kDimensions;
  }

  // Get index of learning feature and scale of learning rate
  static void AppendTrainingFeatures(
      IndexType base_index, std::vector<TrainingFeature>* training_features) {
    assert(base_index <FeatureType::kDimensions);
    training_features->emplace_back(base_index);
  }
};

// Learning feature information
struct FeatureProperties {
  bool active;
  IndexType dimensions;
};

// Add the original input features to the learning features
template <typename FeatureType>
IndexType AppendBaseFeature(
    FeatureProperties properties, IndexType base_index,
    std::vector<TrainingFeature>* training_features) {
  assert(properties.dimensions == FeatureType::kDimensions);
  assert(base_index < FeatureType::kDimensions);
  training_features->emplace_back(base_index);
  return properties.dimensions;
}

// If the learning rate scale is not 0, inherit other types of learning features
template <typename FeatureType>
IndexType InheritFeaturesIfRequired(
    IndexType index_offset, FeatureProperties properties, IndexType base_index,
    std::vector<TrainingFeature>* training_features) {
  if (!properties.active) {
    return 0;
  }
  assert(properties.dimensions == Factorizer<FeatureType>::GetDimensions());
  assert(base_index < FeatureType::kDimensions);
  const auto start = training_features->size();
  Factorizer<FeatureType>::AppendTrainingFeatures(
      base_index, training_features);
  for (auto i = start; i < training_features->size(); ++i) {
    auto& feature = (*training_features)[i];
    assert(feature.GetIndex() < Factorizer<FeatureType>::GetDimensions());
    feature.ShiftIndex(index_offset);
  }
  return properties.dimensions;
}

// Return the index difference as needed, without adding learning features
// Call instead of InheritFeaturesIfRequired() if there are no corresponding features
IndexType SkipFeatures(FeatureProperties properties) {
  if (!properties.active) {
    return 0;
  }
  return properties.dimensions;
}

// Get the dimensionality of the learning feature
template <std::size_t N>
constexpr IndexType GetActiveDimensions(
    const FeatureProperties (&properties)[N]) {
  static_assert(N > 0, "");
  IndexType dimensions = properties[0].dimensions;
  for (std::size_t i = 1; i < N; ++i) {
    if (properties[i].active) {
      dimensions += properties[i].dimensions;
    }
  }
  return dimensions;
}

// get the number of elements in the array
template <typename T, std::size_t N>
constexpr std::size_t GetArrayLength(const T (&/*array*/)[N]) {
  return N;
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
