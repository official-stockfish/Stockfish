// Specialization of NNUE evaluation function learning class template for Sum

#ifndef _NNUE_TRAINER_SUM_H_
#define _NNUE_TRAINER_SUM_H_

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include "../../../learn/learn.h"
#include "../layers/sum.h"
#include "trainer.h"

namespace Eval {

namespace NNUE {

// Learning: A layer that sums the outputs of multiple layers
template <typename FirstPreviousLayer, typename... RemainingPreviousLayers>
class Trainer<Layers::Sum<FirstPreviousLayer, RemainingPreviousLayers...>> :
      Trainer<Layers::Sum<RemainingPreviousLayers...>> {
 private:
  // Type of layer to learn
  using LayerType = Layers::Sum<FirstPreviousLayer, RemainingPreviousLayers...>;
  using Tail = Trainer<Layers::Sum<RemainingPreviousLayers...>>;

 public:
  // factory function
  static std::shared_ptr<Trainer> Create(
      LayerType* target_layer, FeatureTransformer* feature_transformer) {
    return std::shared_ptr<Trainer>(
        new Trainer(target_layer, feature_transformer));
  }

  // Set options such as hyperparameters
  void SendMessage(Message* message) {
    // The results of other member functions do not depend on the processing order, so
    // Tail is processed first for the purpose of simplifying the implementation, but
    // SendMessage processes Head first to make it easier to understand subscript correspondence
    previous_layer_trainer_->SendMessage(message);
    Tail::SendMessage(message);
  }

  // Initialize the parameters with random numbers
  template <typename RNG>
  void Initialize(RNG& rng) {
    Tail::Initialize(rng);
    previous_layer_trainer_->Initialize(rng);
  }

  // forward propagation
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

  // backpropagation
  void Backpropagate(const LearnFloatType* gradients,
                     LearnFloatType learning_rate) {
    Tail::Backpropagate(gradients, learning_rate);
    previous_layer_trainer_->Backpropagate(gradients, learning_rate);
  }

 private:
  // constructor
  Trainer(LayerType* target_layer, FeatureTransformer* feature_transformer):
      Tail(target_layer, feature_transformer),
      batch_size_(0),
      previous_layer_trainer_(Trainer<FirstPreviousLayer>::Create(
          &target_layer->previous_layer_, feature_transformer)),
      target_layer_(target_layer) {
  }

  // number of input/output dimensions
  static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;

  // make subclass friend
  template <typename SumLayer>
  friend class Trainer;

  // number of samples in mini-batch
  IndexType batch_size_;

  // Trainer of the previous layer
  const std::shared_ptr<Trainer<FirstPreviousLayer>> previous_layer_trainer_;

  // layer to learn
  LayerType* const target_layer_;
};


// Learning: Layer that takes the sum of the outputs of multiple layers (when there is one template argument)
template <typename PreviousLayer>
class Trainer<Layers::Sum<PreviousLayer>> {
 private:
  // Type of layer to learn
  using LayerType = Layers::Sum<PreviousLayer>;

 public:
  // factory function
  static std::shared_ptr<Trainer> Create(
      LayerType* target_layer, FeatureTransformer* feature_transformer) {
    return std::shared_ptr<Trainer>(
        new Trainer(target_layer, feature_transformer));
  }

  // Set options such as hyperparameters
  void SendMessage(Message* message) {
    previous_layer_trainer_->SendMessage(message);
  }

  // Initialize the parameters with random numbers
  template <typename RNG>
  void Initialize(RNG& rng) {
    previous_layer_trainer_->Initialize(rng);
  }

  // forward propagation
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

  // backpropagation
  void Backpropagate(const LearnFloatType* gradients,
                     LearnFloatType learning_rate) {
    previous_layer_trainer_->Backpropagate(gradients, learning_rate);
  }

 private:
  // constructor
  Trainer(LayerType* target_layer, FeatureTransformer* feature_transformer) :
      batch_size_(0),
      previous_layer_trainer_(Trainer<PreviousLayer>::Create(
          &target_layer->previous_layer_, feature_transformer)),
      target_layer_(target_layer) {
  }

  // number of input/output dimensions
  static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;

  // make subclass friend
  template <typename SumLayer>
  friend class Trainer;

  // number of samples in mini-batch
  IndexType batch_size_;

  // Trainer of the previous layer
  const std::shared_ptr<Trainer<PreviousLayer>> previous_layer_trainer_;

  // layer to learn
  LayerType* const target_layer_;

  // Forward propagation buffer
  std::vector<LearnFloatType> output_;
};

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
