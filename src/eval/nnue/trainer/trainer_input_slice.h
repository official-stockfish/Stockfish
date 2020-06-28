// Specialization of NNUE evaluation function learning class template for InputSlice

#ifndef _NNUE_TRAINER_INPUT_SLICE_H_
#define _NNUE_TRAINER_INPUT_SLICE_H_

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include "../../../learn/learn.h"
#include "../layers/input_slice.h"
#include "trainer.h"

namespace Eval {

namespace NNUE {

// Learning: Input layer
class SharedInputTrainer {
 public:
  // factory function
  static std::shared_ptr<SharedInputTrainer> Create(
      FeatureTransformer* feature_transformer) {
    static std::shared_ptr<SharedInputTrainer> instance;
    if (!instance) {
      instance.reset(new SharedInputTrainer(feature_transformer));
    }
    ++instance->num_referrers_;
    return instance;
  }

  // Set options such as hyperparameters
  void SendMessage(Message* message) {
    if (num_calls_ == 0) {
      current_operation_ = Operation::kSendMessage;
      feature_transformer_trainer_->SendMessage(message);
    }
    assert(current_operation_ == Operation::kSendMessage);
    if (++num_calls_ == num_referrers_) {
      num_calls_ = 0;
      current_operation_ = Operation::kNone;
    }
  }

  // Initialize the parameters with random numbers
  template <typename RNG>
  void Initialize(RNG& rng) {
    if (num_calls_ == 0) {
      current_operation_ = Operation::kInitialize;
      feature_transformer_trainer_->Initialize(rng);
    }
    assert(current_operation_ == Operation::kInitialize);
    if (++num_calls_ == num_referrers_) {
      num_calls_ = 0;
      current_operation_ = Operation::kNone;
    }
  }

  // forward propagation
  const LearnFloatType* Propagate(const std::vector<Example>& batch) {
    if (gradients_.size() < kInputDimensions * batch.size()) {
      gradients_.resize(kInputDimensions * batch.size());
    }
    batch_size_ = static_cast<IndexType>(batch.size());
    if (num_calls_ == 0) {
      current_operation_ = Operation::kPropagate;
      output_ = feature_transformer_trainer_->Propagate(batch);
    }
    assert(current_operation_ == Operation::kPropagate);
    if (++num_calls_ == num_referrers_) {
      num_calls_ = 0;
      current_operation_ = Operation::kNone;
    }
    return output_;
  }

  // backpropagation
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
    assert(current_operation_ == Operation::kBackPropagate);
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
  // constructor
  SharedInputTrainer(FeatureTransformer* feature_transformer) :
      batch_size_(0),
      num_referrers_(0),
      num_calls_(0),
      current_operation_(Operation::kNone),
      feature_transformer_trainer_(Trainer<FeatureTransformer>::Create(
          feature_transformer)),
      output_(nullptr) {
  }

  // number of input/output dimensions
  static constexpr IndexType kInputDimensions =
      FeatureTransformer::kOutputDimensions;

  // type of processing
  enum class Operation {
    kNone,
    kSendMessage,
    kInitialize,
    kPropagate,
    kBackPropagate,
  };

  // number of samples in mini-batch
  IndexType batch_size_;

  // number of layers sharing this layer as input
  std::uint32_t num_referrers_;

  // Number of times the current process has been called
  std::uint32_t num_calls_;

  // current processing type
  Operation current_operation_;

  // Trainer of input feature converter
  const std::shared_ptr<Trainer<FeatureTransformer>>
      feature_transformer_trainer_;

  // pointer to output shared for forward propagation
  const LearnFloatType* output_;

  // buffer for back propagation
  std::vector<LearnFloatType> gradients_;
};

// Learning: Input layer
template <IndexType OutputDimensions, IndexType Offset>
class Trainer<Layers::InputSlice<OutputDimensions, Offset>> {
 private:
  // Type of layer to learn
  using LayerType = Layers::InputSlice<OutputDimensions, Offset>;

 public:
  // factory function
  static std::shared_ptr<Trainer> Create(
      LayerType* /*target_layer*/, FeatureTransformer* feature_transformer) {
    return std::shared_ptr<Trainer>(new Trainer(feature_transformer));
  }

  // Set options such as hyperparameters
  void SendMessage(Message* message) {
    shared_input_trainer_->SendMessage(message);
  }

  // Initialize the parameters with random numbers
  template <typename RNG>
  void Initialize(RNG& rng) {
    shared_input_trainer_->Initialize(rng);
  }

  // forward propagation
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

  // backpropagation
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
  // constructor
  Trainer(FeatureTransformer* feature_transformer):
      batch_size_(0),
      shared_input_trainer_(SharedInputTrainer::Create(feature_transformer)) {
  }

  // number of input/output dimensions
  static constexpr IndexType kInputDimensions =
      FeatureTransformer::kOutputDimensions;
  static constexpr IndexType kOutputDimensions = OutputDimensions;
  static_assert(Offset + kOutputDimensions <= kInputDimensions, "");

  // number of samples in mini-batch
  IndexType batch_size_;

  // Trainer of shared input layer
  const std::shared_ptr<SharedInputTrainer> shared_input_trainer_;

  // Forward propagation buffer
  std::vector<LearnFloatType> output_;

  // buffer for back propagation
  std::vector<LearnFloatType> gradients_;
};

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
