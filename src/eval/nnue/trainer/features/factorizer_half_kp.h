// NNUE評価関数の特徴量変換クラステンプレートのHalfKP用特殊化

#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KP_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_HALF_KP_H_

#include "../../../../config.h"

#if defined(EVAL_NNUE)

#include "../../features/half_kp.h"
#include "../../features/p.h"
#include "../../features/half_relative_kp.h"
#include "factorizer.h"

namespace Eval {

namespace NNUE {

namespace Features {

// 入力特徴量を学習用特徴量に変換するクラステンプレート
// HalfKP用特殊化
template <Side AssociatedKing>
class Factorizer<HalfKP<AssociatedKing>> {
 private:
  using FeatureType = HalfKP<AssociatedKing>;

  // 特徴量のうち、同時に値が1となるインデックスの数の最大値
  static constexpr IndexType kMaxActiveDimensions =
      FeatureType::kMaxActiveDimensions;

  // 学習用特徴量の種類
  enum TrainingFeatureType {
    kFeaturesHalfKP,
    kFeaturesHalfK,
    kFeaturesP,
    kFeaturesHalfRelativeKP,
    kNumTrainingFeatureTypes,
  };

  // 学習用特徴量の情報
  static constexpr FeatureProperties kProperties[] = {
    // kFeaturesHalfKP
    {true, FeatureType::kDimensions},
    // kFeaturesHalfK
    {true, SQ_NB},
    // kFeaturesP
    {true, Factorizer<P>::GetDimensions()},
    // kFeaturesHalfRelativeKP
    {true, Factorizer<HalfRelativeKP<AssociatedKing>>::GetDimensions()},
  };
  static_assert(GetArrayLength(kProperties) == kNumTrainingFeatureTypes, "");

 public:
  // 学習用特徴量の次元数を取得する
  static constexpr IndexType GetDimensions() {
    return GetActiveDimensions(kProperties);
  }

  // 学習用特徴量のインデックスと学習率のスケールを取得する
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

    ASSERT_LV5(index_offset == GetDimensions());
  }
};

template <Side AssociatedKing>
constexpr FeatureProperties Factorizer<HalfKP<AssociatedKing>>::kProperties[];

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
