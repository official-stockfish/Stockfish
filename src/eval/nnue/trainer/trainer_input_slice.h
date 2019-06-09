// NNUE評価関数の学習クラステンプレートのInputSlice用特殊化

#ifndef _NNUE_TRAINER_INPUT_SLICE_H_
#define _NNUE_TRAINER_INPUT_SLICE_H_

#include "../../../config.h"

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include "../../../learn/learn.h"
#include "../layers/input_slice.h"
#include "trainer.h"

namespace Eval {

namespace NNUE {

// 学習：入力層
class SharedInputTrainer {
 public:
  // ファクトリ関数
  static std::shared_ptr<SharedInputTrainer> Create(
      FeatureTransformer* feature_transformer) {
    static std::shared_ptr<SharedInputTrainer> instance;
    if (!instance) {
      instance.reset(new SharedInputTrainer(feature_transformer));
    }
    ++instance->num_referrers_;
    return instance;
  }

  // ハイパーパラメータなどのオプションを設定する
  void SendMessage(Message* message) {
    if (num_calls_ == 0) {
      current_operation_ = Operation::kSendMessage;
      feature_transformer_trainer_->SendMessage(message);
    }
    ASSERT_LV3(current_operation_ == Operation::kSendMessage);
    if (++num_calls_ == num_referrers_) {
      num_calls_ = 0;
      current_operation_ = Operation::kNone;
    }
  }

  // パラメータを乱数で初期化する
  template <typename RNG>
  void Initialize(RNG& rng) {
    if (num_calls_ == 0) {
      current_operation_ = Operation::kInitialize;
      feature_transformer_trainer_->Initialize(rng);
    }
    ASSERT_LV3(current_operation_ == Operation::kInitialize);
    if (++num_calls_ == num_referrers_) {
      num_calls_ = 0;
      current_operation_ = Operation::kNone;
    }
  }

  // 順伝播
  const LearnFloatType* Propagate(const std::vector<Example>& batch) {
    if (gradients_.size() < kInputDimensions * batch.size()) {
      gradients_.resize(kInputDimensions * batch.size());
    }
    batch_size_ = static_cast<IndexType>(batch.size());
    if (num_calls_ == 0) {
      current_operation_ = Operation::kPropagate;
      output_ = feature_transformer_trainer_->Propagate(batch);
    }
    ASSERT_LV3(current_operation_ == Operation::kPropagate);
    if (++num_calls_ == num_referrers_) {
      num_calls_ = 0;
      current_operation_ = Operation::kNone;
    }
    return output_;
  }

  // 逆伝播
  void Backpropagate(const LearnFloatType* gradients,
                     LearnFloatType learning_rate) {
    if (num_referrers_ == 1) {
      feature_transformer_trainer_->Backpropagate(gradients, learning_rate);
      return;
    }
    if (num_calls_ == 0) {
      current_operation_ = Operation::kBackPropagate;
      for (IndexType b = 0; b < batch_size_; ++b) {
        const IndexType batch_offset = kInputDimensions * b;
        for (IndexType i = 0; i < kInputDimensions; ++i) {
          gradients_[batch_offset + i] = static_cast<LearnFloatType>(0.0);
        }
      }
    }
    ASSERT_LV3(current_operation_ == Operation::kBackPropagate);
    for (IndexType b = 0; b < batch_size_; ++b) {
      const IndexType batch_offset = kInputDimensions * b;
      for (IndexType i = 0; i < kInputDimensions; ++i) {
        gradients_[batch_offset + i] += gradients[batch_offset + i];
      }
    }
    if (++num_calls_ == num_referrers_) {
      feature_transformer_trainer_->Backpropagate(
          gradients_.data(), learning_rate);
      num_calls_ = 0;
      current_operation_ = Operation::kNone;
    }
  }

 private:
  // コンストラクタ
  SharedInputTrainer(FeatureTransformer* feature_transformer) :
      batch_size_(0),
      num_referrers_(0),
      num_calls_(0),
      current_operation_(Operation::kNone),
      feature_transformer_trainer_(Trainer<FeatureTransformer>::Create(
          feature_transformer)),
      output_(nullptr) {
  }

  // 入出力の次元数
  static constexpr IndexType kInputDimensions =
      FeatureTransformer::kOutputDimensions;

  // 処理の種類
  enum class Operation {
    kNone,
    kSendMessage,
    kInitialize,
    kPropagate,
    kBackPropagate,
  };

  // ミニバッチのサンプル数
  IndexType batch_size_;

  // この層を入力として共有する層の数
  std::uint32_t num_referrers_;

  // 現在の処理が呼び出された回数
  std::uint32_t num_calls_;

  // 現在の処理の種類
  Operation current_operation_;

  // 入力特徴量変換器のTrainer
  const std::shared_ptr<Trainer<FeatureTransformer>>
      feature_transformer_trainer_;

  // 順伝播用に共有する出力のポインタ
  const LearnFloatType* output_;

  // 逆伝播用バッファ
  std::vector<LearnFloatType> gradients_;
};

// 学習：入力層
template <IndexType OutputDimensions, IndexType Offset>
class Trainer<Layers::InputSlice<OutputDimensions, Offset>> {
 private:
  // 学習対象の層の型
  using LayerType = Layers::InputSlice<OutputDimensions, Offset>;

 public:
  // ファクトリ関数
  static std::shared_ptr<Trainer> Create(
      LayerType* /*target_layer*/, FeatureTransformer* feature_transformer) {
    return std::shared_ptr<Trainer>(new Trainer(feature_transformer));
  }

  // ハイパーパラメータなどのオプションを設定する
  void SendMessage(Message* message) {
    shared_input_trainer_->SendMessage(message);
  }

  // パラメータを乱数で初期化する
  template <typename RNG>
  void Initialize(RNG& rng) {
    shared_input_trainer_->Initialize(rng);
  }

  // 順伝播
  const LearnFloatType* Propagate(const std::vector<Example>& batch) {
    if (output_.size() < kOutputDimensions * batch.size()) {
      output_.resize(kOutputDimensions * batch.size());
      gradients_.resize(kInputDimensions * batch.size());
    }
    batch_size_ = static_cast<IndexType>(batch.size());
    const auto input = shared_input_trainer_->Propagate(batch);
    for (IndexType b = 0; b < batch_size_; ++b) {
      const IndexType input_offset = kInputDimensions * b;
      const IndexType output_offset = kOutputDimensions * b;
#if defined(USE_BLAS)
      cblas_scopy(kOutputDimensions, &input[input_offset + Offset], 1,
                  &output_[output_offset], 1);
#else
      for (IndexType i = 0; i < kOutputDimensions; ++i) {
        output_[output_offset + i] = input[input_offset + Offset + i];
      }
#endif
    }
    return output_.data();
  }

  // 逆伝播
  void Backpropagate(const LearnFloatType* gradients,
                     LearnFloatType learning_rate) {
    for (IndexType b = 0; b < batch_size_; ++b) {
      const IndexType input_offset = kInputDimensions * b;
      const IndexType output_offset = kOutputDimensions * b;
      for (IndexType i = 0; i < kInputDimensions; ++i) {
        if (i < Offset || i >= Offset + kOutputDimensions) {
          gradients_[input_offset + i] = static_cast<LearnFloatType>(0.0);
        } else {
          gradients_[input_offset + i] = gradients[output_offset + i - Offset];
        }
      }
    }
    shared_input_trainer_->Backpropagate(gradients_.data(), learning_rate);
  }

 private:
  // コンストラクタ
  Trainer(FeatureTransformer* feature_transformer) :
      batch_size_(0),
      shared_input_trainer_(SharedInputTrainer::Create(feature_transformer)) {
  }

  // 入出力の次元数
  static constexpr IndexType kInputDimensions =
      FeatureTransformer::kOutputDimensions;
  static constexpr IndexType kOutputDimensions = OutputDimensions;
  static_assert(Offset + kOutputDimensions <= kInputDimensions, "");

  // ミニバッチのサンプル数
  IndexType batch_size_;

  // 共有入力層のTrainer
  const std::shared_ptr<SharedInputTrainer> shared_input_trainer_;

  // 順伝播用バッファ
  std::vector<LearnFloatType> output_;

  // 逆伝播用バッファ
  std::vector<LearnFloatType> gradients_;
};

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
