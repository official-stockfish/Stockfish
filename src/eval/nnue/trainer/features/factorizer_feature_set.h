// NNUE評価関数の特徴量変換クラステンプレートのFeatureSet用特殊化

#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_FEATURE_SET_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_FEATURE_SET_H_

#include "../../../../config.h"

#if defined(EVAL_NNUE)

#include "../../features/feature_set.h"
#include "factorizer.h"

namespace Eval {

namespace NNUE {

namespace Features {

// 入力特徴量を学習用特徴量に変換するクラステンプレート
// FeatureSet用特殊化
template <typename FirstFeatureType, typename... RemainingFeatureTypes>
class Factorizer<FeatureSet<FirstFeatureType, RemainingFeatureTypes...>> {
 private:
  using Head = Factorizer<FeatureSet<FirstFeatureType>>;
  using Tail = Factorizer<FeatureSet<RemainingFeatureTypes...>>;

 public:
  // 元の入力特徴量の次元数
  static constexpr IndexType kBaseDimensions =
      FeatureSet<FirstFeatureType, RemainingFeatureTypes...>::kDimensions;

  // 学習用特徴量の次元数を取得する
  static constexpr IndexType GetDimensions() {
    return Head::GetDimensions() + Tail::GetDimensions();
  }

  // 学習用特徴量のインデックスと学習率のスケールを取得する
  static void AppendTrainingFeatures(
      IndexType base_index, std::vector<TrainingFeature>* training_features,
      IndexType base_dimensions = kBaseDimensions) {
    ASSERT_LV5(base_index < kBaseDimensions);
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
        ASSERT_LV5(index < Head::GetDimensions() ||
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

// 入力特徴量を学習用特徴量に変換するクラステンプレート
// FeatureSetのテンプレート引数が1つの場合の特殊化
template <typename FeatureType>
class Factorizer<FeatureSet<FeatureType>> {
public:
  // 元の入力特徴量の次元数
  static constexpr IndexType kBaseDimensions = FeatureType::kDimensions;

  // 学習用特徴量の次元数を取得する
  static constexpr IndexType GetDimensions() {
    return Factorizer<FeatureType>::GetDimensions();
  }

  // 学習用特徴量のインデックスと学習率のスケールを取得する
  static void AppendTrainingFeatures(
      IndexType base_index, std::vector<TrainingFeature>* training_features,
      IndexType base_dimensions = kBaseDimensions) {
    ASSERT_LV5(base_index < kBaseDimensions);
    const auto start = training_features->size();
    Factorizer<FeatureType>::AppendTrainingFeatures(
        base_index, training_features);
    for (auto i = start; i < training_features->size(); ++i) {
      auto& feature = (*training_features)[i];
      ASSERT_LV5(feature.GetIndex() < Factorizer<FeatureType>::GetDimensions());
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
