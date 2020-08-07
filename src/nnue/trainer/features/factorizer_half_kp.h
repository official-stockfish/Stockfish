// Specialization of NNUE evaluation function feature conversion class template for HalfKP

#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KP_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KP_H_

#if defined(EVAL_NNUE)

#include "../../features/half_kp.h"
#include "../../features/p.h"
#include "../../features/half_relative_kp.h"
#include "factorizer.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Class template that converts input features into learning features
// Specialization for HalfKP
template <Side AssociatedKing>
class Factorizer<HalfKP<AssociatedKing>> {
 private:
  using FeatureType = HalfKP<AssociatedKing>;

  // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
  static constexpr IndexType kMaxActiveDimensions =
      FeatureType::kMaxActiveDimensions;

  // Type of learning feature
  enum TrainingFeatureType {
    kFeaturesHalfKP,
    kFeaturesHalfK,
    kFeaturesP,
    kFeaturesHalfRelativeKP,
    kNumTrainingFeatureTypes,
  };

  // Learning feature information
  static constexpr FeatureProperties kProperties[] = {
    // kFeaturesHalfKP
    {true, FeatureType::kDimensions},
    // kFeaturesHalfK
    {true, SQUARE_NB},
    // kFeaturesP
    {true, Factorizer<P>::GetDimensions()},
    // kFeaturesHalfRelativeKP
    {true, Factorizer<HalfRelativeKP<AssociatedKing>>::GetDimensions()},
  };
  static_assert(GetArrayLength(kProperties) == kNumTrainingFeatureTypes, "");

 public:
  // Get the dimensionality of the learning feature
  static constexpr IndexType GetDimensions() {
    return GetActiveDimensions(kProperties);
  }

  // Get index of learning feature and scale of learning rate
  static void AppendTrainingFeatures(
      IndexType base_index, std::vector<TrainingFeature>* training_features) {
    // kFeaturesHalfKP
    IndexType index_offset = AppendBaseFeature<FeatureType>(
        kProperties[kFeaturesHalfKP], base_index, training_features);

    const auto sq_k = static_cast<Square>(base_index / fe_end);
    const auto p = static_cast<BonaPiece>(base_index % fe_end);
    // kFeaturesHalfK
    {
      const auto& properties = kProperties[kFeaturesHalfK];
      if (properties.active) {
        training_features->emplace_back(index_offset + sq_k);
        index_offset += properties.dimensions;
      }
    }
    // kFeaturesP
    index_offset += InheritFeaturesIfRequired<P>(
        index_offset, kProperties[kFeaturesP], p, training_features);
    // kFeaturesHalfRelativeKP
    if (p >= fe_hand_end) {
      index_offset += InheritFeaturesIfRequired<HalfRelativeKP<AssociatedKing>>(
          index_offset, kProperties[kFeaturesHalfRelativeKP],
          HalfRelativeKP<AssociatedKing>::MakeIndex(sq_k, p),
          training_features);
    } else {
      index_offset += SkipFeatures(kProperties[kFeaturesHalfRelativeKP]);
    }

    assert(index_offset == GetDimensions());
  }
};

template <Side AssociatedKing>
constexpr FeatureProperties Factorizer<HalfKP<AssociatedKing>>::kProperties[];

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
