// NNUE評価関数の学習クラステンプレートのClippedReLU用特殊化

#ifndef _NNUE_TRAINER_CLIPPED_RELU_H_
#define _NNUE_TRAINER_CLIPPED_RELU_H_

#include "../../../config.h"

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include "../../../learn/learn.h"
#include "../layers/clipped_relu.h"
#include "trainer.h"

namespace Eval {

namespace NNUE {

// 学習：アフィン変換層
template <typename PreviousLayer>
class Trainer<Layers::ClippedReLU<PreviousLayer>> {
 private:
  // 学習対象の層の型
  using LayerType = Layers::ClippedReLU<PreviousLayer>;

 public:
  // ファクトリ関数
  static std::shared_ptr<Trainer> Create(
      LayerType* target_layer, FeatureTransformer* feature_transformer) {
    return std::shared_ptr<Trainer>(
        new Trainer(target_layer, feature_transformer));
  }

  // ハイパーパラメータなどのオプションを設定する
  void SendMessage(Message* message) {
    previous_layer_trainer_->SendMessage(message);
    if (ReceiveMessage("check_health", message)) {
      CheckHealth();
    }
  }

  // パラメータを乱数で初期化する
  template <typename RNG>
  void Initialize(RNG& rng) {
    previous_layer_trainer_->Initialize(rng);
  }

  // 順伝播
  const LearnFloatType* Propagate(const std::vector<Example>& batch) {
    if (output_.size() < kOutputDimensions * batch.size()) {
      output_.resize(kOutputDimensions * batch.size());
      gradients_.resize(kInputDimensions * batch.size());
    }
    const auto input = previous_layer_trainer_->Propagate(batch);
    batch_size_ = static_cast<IndexType>(batch.size());
    for (IndexType b = 0; b < batch_size_; ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        const IndexType index = batch_offset + i;
        output_[index] = std::max(+kZero, std::min(+kOne, input[index]));
        min_activations_[i] = std::min(min_activations_[i], output_[index]);
        max_activations_[i] = std::max(max_activations_[i], output_[index]);
      }
    }
    return output_.data();
  }

  // 逆伝播
  void Backpropagate(const LearnFloatType* gradients,
                     LearnFloatType learning_rate) {
    for (IndexType b = 0; b < batch_size_; ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        const IndexType index = batch_offset + i;
        gradients_[index] = gradients[index] *
            (output_[index] > kZero) * (output_[index] < kOne);
      }
    }
    previous_layer_trainer_->Backpropagate(gradients_.data(), learning_rate);
  }

 private:
  // コンストラクタ
  Trainer(LayerType* target_layer, FeatureTransformer* feature_transformer) :
      batch_size_(0),
      previous_layer_trainer_(Trainer<PreviousLayer>::Create(
          &target_layer->previous_layer_, feature_transformer)),
      target_layer_(target_layer) {
    std::fill(std::begin(min_activations_), std::end(min_activations_),
              std::numeric_limits<LearnFloatType>::max());
    std::fill(std::begin(max_activations_), std::end(max_activations_),
              std::numeric_limits<LearnFloatType>::lowest());
  }

  // 学習に問題が生じていないかチェックする
  void CheckHealth() {
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
  static constexpr IndexType kInputDimensions = LayerType::kOutputDimensions;
  static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;

  // LearnFloatTypeの定数
  static constexpr LearnFloatType kZero = static_cast<LearnFloatType>(0.0);
  static constexpr LearnFloatType kOne = static_cast<LearnFloatType>(1.0);

  // ミニバッチのサンプル数
  IndexType batch_size_;

  // 直前の層のTrainer
  const std::shared_ptr<Trainer<PreviousLayer>> previous_layer_trainer_;

  // 学習対象の層
  LayerType* const target_layer_;

  // 順伝播用バッファ
  std::vector<LearnFloatType> output_;

  // 逆伝播用バッファ
  std::vector<LearnFloatType> gradients_;

  // ヘルスチェック用統計値
  LearnFloatType min_activations_[kOutputDimensions];
  LearnFloatType max_activations_[kOutputDimensions];
};

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
