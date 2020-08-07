// Specialization for feature set of feature conversion class template of NNUE evaluation function

#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_FEATURE_SET_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_FEATURE_SET_H_

#if defined(EVAL_NNUE)

#include "../../features/feature_set.h"
#include "factorizer.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Class template that converts input features into learning features
// Specialization for FeatureSet
template <typename FirstFeatureType, typename... RemainingFeatureTypes>
class Factorizer<FeatureSet<FirstFeatureType, RemainingFeatureTypes...>> {
 private:
  using Head = Factorizer<FeatureSet<FirstFeatureType>>;
  using Tail = Factorizer<FeatureSet<RemainingFeatureTypes...>>;

 public:
  // number of dimensions of original input features
  static constexpr IndexType kBaseDimensions =
      FeatureSet<FirstFeatureType, RemainingFeatureTypes...>::kDimensions;

  // Get the dimensionality of the learning feature
  static constexpr IndexType GetDimensions() {
    return Head::GetDimensions() + Tail::GetDimensions();
  }

  // Get index of learning feature and scale of learning rate
  static void AppendTrainingFeatures(
      IndexType base_index, std::vector<TrainingFeature>* training_features,
      IndexType base_dimensions = kBaseDimensions) {
    assert(base_index < kBaseDimensions);
    constexpr auto boundary = FeatureSet<RemainingFeatureTypes...>::kDimensions;
    if (base_index < boundary) {
      Tail::AppendTrainingFeatures(
          base_index, training_features, base_dimensions);
    } else {
      const auto start = training_features->size();
      Head::AppendTrainingFeatures(
          base_index - boundary, training_features, base_dimensions);
      for (auto i = start; i < training_features->size(); ++i) {
        auto& feature = (*training_features)[i];
        const auto index = feature.GetIndex();
        assert(index < Head::GetDimensions() ||
                   (index >= base_dimensions &&
                    index < base_dimensions +
                            Head::GetDimensions() - Head::kBaseDimensions));
        if (index < Head::kBaseDimensions) {
          feature.ShiftIndex(Tail::kBaseDimensions);
        } else {
          feature.ShiftIndex(Tail::GetDimensions() - Tail::kBaseDimensions);
        }
      }
    }
  }
};

// Class template that converts input features into learning features
// Specialization when FeatureSet has one template argument
template <typename FeatureType>
class Factorizer<FeatureSet<FeatureType>> {
public:
  // number of dimensions of original input features
  static constexpr IndexType kBaseDimensions = FeatureType::kDimensions;

  // Get the dimensionality of the learning feature
  static constexpr IndexType GetDimensions() {
    return Factorizer<FeatureType>::GetDimensions();
  }

  // Get index of learning feature and scale of learning rate
  static void AppendTrainingFeatures(
      IndexType base_index, std::vector<TrainingFeature>* training_features,
      IndexType base_dimensions = kBaseDimensions) {
    assert(base_index < kBaseDimensions);
    const auto start = training_features->size();
    Factorizer<FeatureType>::AppendTrainingFeatures(
        base_index, training_features);
    for (auto i = start; i < training_features->size(); ++i) {
      auto& feature = (*training_features)[i];
      assert(feature.GetIndex() < Factorizer<FeatureType>::GetDimensions());
      if (feature.GetIndex() >= kBaseDimensions) {
        feature.ShiftIndex(base_dimensions - kBaseDimensions);
      }
    }
  }
};

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
