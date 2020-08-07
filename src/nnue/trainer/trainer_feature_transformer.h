// Specialization for feature transformer of learning class template of NNUE evaluation function

#ifndef _NNUE_TRAINER_FEATURE_TRANSFORMER_H_
#define _NNUE_TRAINER_FEATURE_TRANSFORMER_H_

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

// Learning: Input feature converter
template <>
class Trainer<FeatureTransformer> {
 private:
  // Type of layer to learn
  using LayerType = FeatureTransformer;

 public:
  template <typename T>
  friend struct AlignedDeleter;
  template <typename T, typename... ArgumentTypes>
  friend std::shared_ptr<T> MakeAlignedSharedPtr(ArgumentTypes&&... arguments);

  // factory function
  static std::shared_ptr<Trainer> Create(LayerType* target_layer) {
    return MakeAlignedSharedPtr<Trainer>(target_layer);
  }

  // Set options such as hyperparameters
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

  // Initialize the parameters with random numbers
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

  // forward propagation
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

  // backpropagation
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
    // Since the weight matrix updates only the columns corresponding to the features that appeared in the input,
    // Correct the learning rate and adjust the scale without using momentum
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
  // constructor
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

  // Weight saturation and parameterization
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

  // read parameterized integer
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

  // Set the weight corresponding to the feature that does not appear in the learning data to 0
  void ClearUnobservedFeatureWeights() {
    for (IndexType i = 0; i < kInputDimensions; ++i) {
      if (!observed_features.test(i)) {
        std::fill(std::begin(weights_) + kHalfDimensions * i,
                  std::begin(weights_) + kHalfDimensions * (i + 1), +kZero);
      }
    }
    QuantizeParameters();
  }

  // Check if there are any problems with learning
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

  // number of input/output dimensions
  static constexpr IndexType kInputDimensions =
      Features::Factorizer<RawFeatures>::GetDimensions();
  static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;
  static constexpr IndexType kHalfDimensions = LayerType::kHalfDimensions;

  // Coefficient used for parameterization
  static constexpr LearnFloatType kActivationScale =
      std::numeric_limits<std::int8_t>::max();
  static constexpr LearnFloatType kBiasScale = kActivationScale;
  static constexpr LearnFloatType kWeightScale = kActivationScale;

  // LearnFloatType constant
  static constexpr LearnFloatType kZero = static_cast<LearnFloatType>(0.0);
  static constexpr LearnFloatType kOne = static_cast<LearnFloatType>(1.0);

  // mini batch
  const std::vector<Example>* batch_;

  // layer to learn
  LayerType* const target_layer_;

  // parameter
  alignas(kCacheLineSize) LearnFloatType biases_[kHalfDimensions];
  alignas(kCacheLineSize)
      LearnFloatType weights_[kHalfDimensions * kInputDimensions];

  // Buffer used for updating parameters
  LearnFloatType biases_diff_[kHalfDimensions];
  std::vector<LearnFloatType> gradients_;

  // Forward propagation buffer
  std::vector<LearnFloatType> output_;

  // Features that appeared in the training data
  std::bitset<kInputDimensions> observed_features;

  // hyper parameter
  LearnFloatType momentum_;
  LearnFloatType learning_rate_scale_;

  // Health check statistics
  LearnFloatType min_pre_activation_;
  LearnFloatType max_pre_activation_;
  LearnFloatType min_activations_[kHalfDimensions];
  LearnFloatType max_activations_[kHalfDimensions];
};

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
