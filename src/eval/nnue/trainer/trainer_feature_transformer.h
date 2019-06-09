// NNUE評価関数の学習クラステンプレートのFeatureTransformer用特殊化

#ifndef _NNUE_TRAINER_FEATURE_TRANSFORMER_H_
#define _NNUE_TRAINER_FEATURE_TRANSFORMER_H_

#include "../../../config.h"

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include "../../../learn/learn.h"
#include "../nnue_feature_transformer.h"
#include "trainer.h"
#include "features/factorizer_feature_set.h"

#include <array>
#include <bitset>
#include <numeric>
#include <random>
#include <set>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace Eval {

namespace NNUE {

// 学習：入力特徴量変換器
template <>
class Trainer<FeatureTransformer> {
 private:
  // 学習対象の層の型
  using LayerType = FeatureTransformer;

 public:
  template <typename T>
  friend struct AlignedDeleter;
  template <typename T, typename... ArgumentTypes>
  friend std::shared_ptr<T> MakeAlignedSharedPtr(ArgumentTypes&&... arguments);

  // ファクトリ関数
  static std::shared_ptr<Trainer> Create(LayerType* target_layer) {
    return MakeAlignedSharedPtr<Trainer>(target_layer);
  }

  // ハイパーパラメータなどのオプションを設定する
  void SendMessage(Message* message) {
    if (ReceiveMessage("momentum", message)) {
      momentum_ = static_cast<LearnFloatType>(std::stod(message->value));
    }
    if (ReceiveMessage("learning_rate_scale", message)) {
      learning_rate_scale_ =
          static_cast<LearnFloatType>(std::stod(message->value));
    }
    if (ReceiveMessage("reset", message)) {
      DequantizeParameters();
    }
    if (ReceiveMessage("quantize_parameters", message)) {
      QuantizeParameters();
    }
    if (ReceiveMessage("clear_unobserved_feature_weights", message)) {
      ClearUnobservedFeatureWeights();
    }
    if (ReceiveMessage("check_health", message)) {
      CheckHealth();
    }
  }

  // パラメータを乱数で初期化する
  template <typename RNG>
  void Initialize(RNG& rng) {
    std::fill(std::begin(weights_), std::end(weights_), +kZero);
    const double kSigma = 0.1 / std::sqrt(RawFeatures::kMaxActiveDimensions);
    auto distribution = std::normal_distribution<double>(0.0, kSigma);
    for (IndexType i = 0; i < kHalfDimensions * RawFeatures::kDimensions; ++i) {
      const auto weight = static_cast<LearnFloatType>(distribution(rng));
      weights_[i] = weight;
    }
    for (IndexType i = 0; i < kHalfDimensions; ++i) {
      biases_[i] = static_cast<LearnFloatType>(0.5);
    }
    QuantizeParameters();
  }

  // 順伝播
  const LearnFloatType* Propagate(const std::vector<Example>& batch) {
    if (output_.size() < kOutputDimensions * batch.size()) {
      output_.resize(kOutputDimensions * batch.size());
      gradients_.resize(kOutputDimensions * batch.size());
    }
    batch_ = &batch;
    // affine transform
#pragma omp parallel for
    for (IndexType b = 0; b < batch.size(); ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType c = 0; c < 2; ++c) {
        const IndexType output_offset = batch_offset + kHalfDimensions * c;
#if defined(USE_BLAS)
        cblas_scopy(kHalfDimensions, biases_, 1, &output_[output_offset], 1);
        for (const auto& feature : batch[b].training_features[c]) {
          const IndexType weights_offset = kHalfDimensions * feature.GetIndex();
          cblas_saxpy(kHalfDimensions, (float)feature.GetCount(),
                      &weights_[weights_offset], 1, &output_[output_offset], 1);
        }
#else
        for (IndexType i = 0; i < kHalfDimensions; ++i) {
          output_[output_offset + i] = biases_[i];
        }
        for (const auto& feature : batch[b].training_features[c]) {
          const IndexType weights_offset = kHalfDimensions * feature.GetIndex();
          for (IndexType i = 0; i < kHalfDimensions; ++i) {
            output_[output_offset + i] +=
                feature.GetCount() * weights_[weights_offset + i];
          }
        }
#endif
      }
    }
    // clipped ReLU
    for (IndexType b = 0; b < batch.size(); ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        const IndexType index = batch_offset + i;
        min_pre_activation_ = std::min(min_pre_activation_, output_[index]);
        max_pre_activation_ = std::max(max_pre_activation_, output_[index]);
        output_[index] = std::max(+kZero, std::min(+kOne, output_[index]));
        const IndexType t = i % kHalfDimensions;
        min_activations_[t] = std::min(min_activations_[t], output_[index]);
        max_activations_[t] = std::max(max_activations_[t], output_[index]);
      }
    }
    return output_.data();
  }

  // 逆伝播
  void Backpropagate(const LearnFloatType* gradients,
                     LearnFloatType learning_rate) {
    const LearnFloatType local_learning_rate =
        learning_rate * learning_rate_scale_;
    for (IndexType b = 0; b < batch_->size(); ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        const IndexType index = batch_offset + i;
        gradients_[index] = gradients[index] *
            ((output_[index] > kZero) * (output_[index] < kOne));
      }
    }
    // 重み行列は入力に出現した特徴量に対応する列のみを更新するため、
    // momentumを使用せず、学習率を補正してスケールを合わせる
    const LearnFloatType effective_learning_rate =
        static_cast<LearnFloatType>(local_learning_rate / (1.0 - momentum_));
#if defined(USE_BLAS)
    cblas_sscal(kHalfDimensions, momentum_, biases_diff_, 1);
    for (IndexType b = 0; b < batch_->size(); ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType c = 0; c < 2; ++c) {
        const IndexType output_offset = batch_offset + kHalfDimensions * c;
        cblas_saxpy(kHalfDimensions, 1.0,
                    &gradients_[output_offset], 1, biases_diff_, 1);
      }
    }
    cblas_saxpy(kHalfDimensions, -local_learning_rate,
                biases_diff_, 1, biases_, 1);
#pragma omp parallel
    {
#if defined(_OPENMP)
      const IndexType num_threads = omp_get_num_threads();
      const IndexType thread_index = omp_get_thread_num();
#endif
      for (IndexType b = 0; b < batch_->size(); ++b) {
        const IndexType batch_offset = kOutputDimensions * b;
        for (IndexType c = 0; c < 2; ++c) {
          const IndexType output_offset = batch_offset + kHalfDimensions * c;
          for (const auto& feature : (*batch_)[b].training_features[c]) {
#if defined(_OPENMP)
            if (feature.GetIndex() % num_threads != thread_index) continue;
#endif
            const IndexType weights_offset =
                kHalfDimensions * feature.GetIndex();
            const auto scale = static_cast<LearnFloatType>(
                effective_learning_rate / feature.GetCount());
            cblas_saxpy(kHalfDimensions, -scale,
                        &gradients_[output_offset], 1,
                        &weights_[weights_offset], 1);
          }
        }
      }
    }
#else
    for (IndexType i = 0; i < kHalfDimensions; ++i) {
      biases_diff_[i] *= momentum_;
    }
    for (IndexType b = 0; b < batch_->size(); ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType c = 0; c < 2; ++c) {
        const IndexType output_offset = batch_offset + kHalfDimensions * c;
        for (IndexType i = 0; i < kHalfDimensions; ++i) {
          biases_diff_[i] += gradients_[output_offset + i];
        }
      }
    }
    for (IndexType i = 0; i < kHalfDimensions; ++i) {
      biases_[i] -= local_learning_rate * biases_diff_[i];
    }
    for (IndexType b = 0; b < batch_->size(); ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType c = 0; c < 2; ++c) {
        const IndexType output_offset = batch_offset + kHalfDimensions * c;
        for (const auto& feature : (*batch_)[b].training_features[c]) {
          const IndexType weights_offset = kHalfDimensions * feature.GetIndex();
          const auto scale = static_cast<LearnFloatType>(
              effective_learning_rate / feature.GetCount());
          for (IndexType i = 0; i < kHalfDimensions; ++i) {
            weights_[weights_offset + i] -=
                scale * gradients_[output_offset + i];
          }
        }
      }
    }
#endif
    for (IndexType b = 0; b < batch_->size(); ++b) {
      for (IndexType c = 0; c < 2; ++c) {
        for (const auto& feature : (*batch_)[b].training_features[c]) {
          observed_features.set(feature.GetIndex());
        }
      }
    }
  }

 private:
  // コンストラクタ
  Trainer(LayerType* target_layer) :
      batch_(nullptr),
      target_layer_(target_layer),
      biases_(),
      weights_(),
      biases_diff_(),
      momentum_(0.0),
      learning_rate_scale_(1.0) {
    min_pre_activation_ = std::numeric_limits<LearnFloatType>::max();
    max_pre_activation_ = std::numeric_limits<LearnFloatType>::lowest();
    std::fill(std::begin(min_activations_), std::end(min_activations_),
              std::numeric_limits<LearnFloatType>::max());
    std::fill(std::begin(max_activations_), std::end(max_activations_),
              std::numeric_limits<LearnFloatType>::lowest());
    DequantizeParameters();
  }

  // 重みの飽和とパラメータの整数化
  void QuantizeParameters() {
    for (IndexType i = 0; i < kHalfDimensions; ++i) {
      target_layer_->biases_[i] =
          Round<typename LayerType::BiasType>(biases_[i] * kBiasScale);
    }
    std::vector<TrainingFeature> training_features;
#pragma omp parallel for private(training_features)
    for (IndexType j = 0; j < RawFeatures::kDimensions; ++j) {
      training_features.clear();
      Features::Factorizer<RawFeatures>::AppendTrainingFeatures(
          j, &training_features);
      for (IndexType i = 0; i < kHalfDimensions; ++i) {
        double sum = 0.0;
        for (const auto& feature : training_features) {
          sum += weights_[kHalfDimensions * feature.GetIndex() + i];
        }
        target_layer_->weights_[kHalfDimensions * j + i] =
            Round<typename LayerType::WeightType>(sum * kWeightScale);
      }
    }
  }

  // 整数化されたパラメータの読み込み
  void DequantizeParameters() {
    for (IndexType i = 0; i < kHalfDimensions; ++i) {
      biases_[i] = static_cast<LearnFloatType>(
          target_layer_->biases_[i] / kBiasScale);
    }
    std::fill(std::begin(weights_), std::end(weights_), +kZero);
    for (IndexType i = 0; i < kHalfDimensions * RawFeatures::kDimensions; ++i) {
      weights_[i] = static_cast<LearnFloatType>(
          target_layer_->weights_[i] / kWeightScale);
    }
    std::fill(std::begin(biases_diff_), std::end(biases_diff_), +kZero);
  }

  // 学習データに出現していない特徴量に対応する重みを0にする
  void ClearUnobservedFeatureWeights() {
    for (IndexType i = 0; i < kInputDimensions; ++i) {
      if (!observed_features.test(i)) {
        std::fill(std::begin(weights_) + kHalfDimensions * i,
                  std::begin(weights_) + kHalfDimensions * (i + 1), +kZero);
      }
    }
    QuantizeParameters();
  }

  // 学習に問題が生じていないかチェックする
  void CheckHealth() {
    std::cout << "INFO: observed " << observed_features.count()
              << " (out of " << kInputDimensions << ") features" << std::endl;

    constexpr LearnFloatType kPreActivationLimit =
        std::numeric_limits<typename LayerType::WeightType>::max() /
        kWeightScale;
    std::cout << "INFO: (min, max) of pre-activations = "
              << min_pre_activation_ << ", "
              << max_pre_activation_ << " (limit = "
              << kPreActivationLimit << ")" << std::endl;

    const auto largest_min_activation = *std::max_element(
        std::begin(min_activations_), std::end(min_activations_));
    const auto smallest_max_activation = *std::min_element(
        std::begin(max_activations_), std::end(max_activations_));
    std::cout << "INFO: largest min activation = " << largest_min_activation
              << ", smallest max activation = " << smallest_max_activation
              << std::endl;

    std::fill(std::begin(min_activations_), std::end(min_activations_),
              std::numeric_limits<LearnFloatType>::max());
    std::fill(std::begin(max_activations_), std::end(max_activations_),
              std::numeric_limits<LearnFloatType>::lowest());
  }

  // 入出力の次元数
  static constexpr IndexType kInputDimensions =
      Features::Factorizer<RawFeatures>::GetDimensions();
  static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;
  static constexpr IndexType kHalfDimensions = LayerType::kHalfDimensions;

  // パラメータの整数化で用いる係数
  static constexpr LearnFloatType kActivationScale =
      std::numeric_limits<std::int8_t>::max();
  static constexpr LearnFloatType kBiasScale = kActivationScale;
  static constexpr LearnFloatType kWeightScale = kActivationScale;

  // LearnFloatTypeの定数
  static constexpr LearnFloatType kZero = static_cast<LearnFloatType>(0.0);
  static constexpr LearnFloatType kOne = static_cast<LearnFloatType>(1.0);

  // ミニバッチ
  const std::vector<Example>* batch_;

  // 学習対象の層
  LayerType* const target_layer_;

  // パラメータ
  alignas(kCacheLineSize) LearnFloatType biases_[kHalfDimensions];
  alignas(kCacheLineSize)
      LearnFloatType weights_[kHalfDimensions * kInputDimensions];

  // パラメータの更新で用いるバッファ
  LearnFloatType biases_diff_[kHalfDimensions];
  std::vector<LearnFloatType> gradients_;

  // 順伝播用バッファ
  std::vector<LearnFloatType> output_;

  // 学習データに出現した特徴量
  std::bitset<kInputDimensions> observed_features;

  // ハイパーパラメータ
  LearnFloatType momentum_;
  LearnFloatType learning_rate_scale_;

  // ヘルスチェック用統計値
  LearnFloatType min_pre_activation_;
  LearnFloatType max_pre_activation_;
  LearnFloatType min_activations_[kHalfDimensions];
  LearnFloatType max_activations_[kHalfDimensions];
};

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
