// NNUE評価関数の学習クラステンプレートのSum用特殊化

#ifndef _NNUE_TRAINER_SUM_H_
#define _NNUE_TRAINER_SUM_H_

#include "../../../config.h"

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include "../../../learn/learn.h"
#include "../layers/sum.h"
#include "trainer.h"

namespace Eval {

namespace NNUE {

// 学習：複数の層の出力の和を取る層
template <typename FirstPreviousLayer, typename... RemainingPreviousLayers>
class Trainer<Layers::Sum<FirstPreviousLayer, RemainingPreviousLayers...>> :
      Trainer<Layers::Sum<RemainingPreviousLayers...>> {
 private:
  // 学習対象の層の型
  using LayerType = Layers::Sum<FirstPreviousLayer, RemainingPreviousLayers...>;
  using Tail = Trainer<Layers::Sum<RemainingPreviousLayers...>>;

 public:
  // ファクトリ関数
  static std::shared_ptr<Trainer> Create(
      LayerType* target_layer, FeatureTransformer* feature_transformer) {
    return std::shared_ptr<Trainer>(
        new Trainer(target_layer, feature_transformer));
  }

  // ハイパーパラメータなどのオプションを設定する
  void SendMessage(Message* message) {
    // 他のメンバ関数の結果は処理の順番に依存しないため、
    // 実装をシンプルにすることを目的としてTailを先に処理するが、
    // SendMessageは添字の対応を分かりやすくするためにHeadを先に処理する
    previous_layer_trainer_->SendMessage(message);
    Tail::SendMessage(message);
  }

  // パラメータを乱数で初期化する
  template <typename RNG>
  void Initialize(RNG& rng) {
    Tail::Initialize(rng);
    previous_layer_trainer_->Initialize(rng);
  }

  // 順伝播
  /*const*/ LearnFloatType* Propagate(const std::vector<Example>& batch) {
    batch_size_ = static_cast<IndexType>(batch.size());
    auto output = Tail::Propagate(batch);
    const auto head_output = previous_layer_trainer_->Propagate(batch);
#if defined(USE_BLAS)
    cblas_saxpy(kOutputDimensions * batch_size_, 1.0,
                head_output, 1, output, 1);
#else
    for (IndexType b = 0; b < batch_size_; ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        output[batch_offset + i] += head_output[batch_offset + i];
      }
    }
#endif
    return output;
  }

  // 逆伝播
  void Backpropagate(const LearnFloatType* gradients,
                     LearnFloatType learning_rate) {
    Tail::Backpropagate(gradients, learning_rate);
    previous_layer_trainer_->Backpropagate(gradients, learning_rate);
  }

 private:
  // コンストラクタ
  Trainer(LayerType* target_layer, FeatureTransformer* feature_transformer) :
      Tail(target_layer, feature_transformer),
      batch_size_(0),
      previous_layer_trainer_(Trainer<FirstPreviousLayer>::Create(
          &target_layer->previous_layer_, feature_transformer)),
      target_layer_(target_layer) {
  }

  // 入出力の次元数
  static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;

  // サブクラスをfriendにする
  template <typename SumLayer>
  friend class Trainer;

  // ミニバッチのサンプル数
  IndexType batch_size_;

  // 直前の層のTrainer
  const std::shared_ptr<Trainer<FirstPreviousLayer>> previous_layer_trainer_;

  // 学習対象の層
  LayerType* const target_layer_;
};


// 学習：複数の層の出力の和を取る層（テンプレート引数が1つの場合）
template <typename PreviousLayer>
class Trainer<Layers::Sum<PreviousLayer>> {
 private:
  // 学習対象の層の型
  using LayerType = Layers::Sum<PreviousLayer>;

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
  }

  // パラメータを乱数で初期化する
  template <typename RNG>
  void Initialize(RNG& rng) {
    previous_layer_trainer_->Initialize(rng);
  }

  // 順伝播
  /*const*/ LearnFloatType* Propagate(const std::vector<Example>& batch) {
    if (output_.size() < kOutputDimensions * batch.size()) {
      output_.resize(kOutputDimensions * batch.size());
    }
    batch_size_ = static_cast<IndexType>(batch.size());
    const auto output = previous_layer_trainer_->Propagate(batch);
#if defined(USE_BLAS)
    cblas_scopy(kOutputDimensions * batch_size_, output, 1, &output_[0], 1);
#else
    for (IndexType b = 0; b < batch_size_; ++b) {
      const IndexType batch_offset = kOutputDimensions * b;
      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        output_[batch_offset + i] = output[batch_offset + i];
      }
    }
#endif
    return output_.data();
  }

  // 逆伝播
  void Backpropagate(const LearnFloatType* gradients,
                     LearnFloatType learning_rate) {
    previous_layer_trainer_->Backpropagate(gradients, learning_rate);
  }

 private:
  // コンストラクタ
  Trainer(LayerType* target_layer, FeatureTransformer* feature_transformer) :
      batch_size_(0),
      previous_layer_trainer_(Trainer<PreviousLayer>::Create(
          &target_layer->previous_layer_, feature_transformer)),
      target_layer_(target_layer) {
  }

  // 入出力の次元数
  static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;

  // サブクラスをfriendにする
  template <typename SumLayer>
  friend class Trainer;

  // ミニバッチのサンプル数
  IndexType batch_size_;

  // 直前の層のTrainer
  const std::shared_ptr<Trainer<PreviousLayer>> previous_layer_trainer_;

  // 学習対象の層
  LayerType* const target_layer_;

  // 順伝播用バッファ
  std::vector<LearnFloatType> output_;
};

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
