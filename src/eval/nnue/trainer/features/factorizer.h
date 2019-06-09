// NNUE評価関数の特徴量変換クラステンプレート

#ifndef _NNUE_TRAINER_FEATURES_FACTORIZER_H_
#define _NNUE_TRAINER_FEATURES_FACTORIZER_H_

#include "../../../../config.h"

#if defined(EVAL_NNUE)

#include "../../nnue_common.h"
#include "../trainer.h"

namespace Eval {

namespace NNUE {

namespace Features {

// 入力特徴量を学習用特徴量に変換するクラステンプレート
// デフォルトでは学習用特徴量は元の入力特徴量と同じとし、必要に応じて特殊化する
template <typename FeatureType>
class Factorizer {
 public:
  // 学習用特徴量の次元数を取得する
  static constexpr IndexType GetDimensions() {
    return FeatureType::kDimensions;
  }

  // 学習用特徴量のインデックスと学習率のスケールを取得する
  static void AppendTrainingFeatures(
      IndexType base_index, std::vector<TrainingFeature>* training_features) {
    ASSERT_LV5(base_index < FeatureType::kDimensions);
    training_features->emplace_back(base_index);
  }
};

// 学習用特徴量の情報
struct FeatureProperties {
  bool active;
  IndexType dimensions;
};

// 元の入力特徴量を学習用特徴量に追加する
template <typename FeatureType>
IndexType AppendBaseFeature(
    FeatureProperties properties, IndexType base_index,
    std::vector<TrainingFeature>* training_features) {
  ASSERT_LV5(properties.dimensions == FeatureType::kDimensions);
  ASSERT_LV5(base_index < FeatureType::kDimensions);
  training_features->emplace_back(base_index);
  return properties.dimensions;
}

// 学習率のスケールが0でなければ他の種類の学習用特徴量を引き継ぐ
template <typename FeatureType>
IndexType InheritFeaturesIfRequired(
    IndexType index_offset, FeatureProperties properties, IndexType base_index,
    std::vector<TrainingFeature>* training_features) {
  if (!properties.active) {
    return 0;
  }
  ASSERT_LV5(properties.dimensions == Factorizer<FeatureType>::GetDimensions());
  ASSERT_LV5(base_index < FeatureType::kDimensions);
  const auto start = training_features->size();
  Factorizer<FeatureType>::AppendTrainingFeatures(
      base_index, training_features);
  for (auto i = start; i < training_features->size(); ++i) {
    auto& feature = (*training_features)[i];
    ASSERT_LV5(feature.GetIndex() < Factorizer<FeatureType>::GetDimensions());
    feature.ShiftIndex(index_offset);
  }
  return properties.dimensions;
}

// 学習用特徴量を追加せず、必要に応じてインデックスの差分を返す
// 対応する特徴量がない場合にInheritFeaturesIfRequired()の代わりに呼ぶ
IndexType SkipFeatures(FeatureProperties properties) {
  if (!properties.active) {
    return 0;
  }
  return properties.dimensions;
}

// 学習用特徴量の次元数を取得する
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

// 配列の要素数を取得する
template <typename T, std::size_t N>
constexpr std::size_t GetArrayLength(const T (&/*array*/)[N]) {
  return N;
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
