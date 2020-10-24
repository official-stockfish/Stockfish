#ifndef _NNUE_TRAINER_CLIPPED_RELU_H_
#define _NNUE_TRAINER_CLIPPED_RELU_H_

#include "trainer.h"

#include "learn/learn.h"

#include "nnue/layers/clipped_relu.h"

// Specialization of NNUE evaluation function learning class template for ClippedReLU
namespace Eval::NNUE {

    // Learning: Affine transformation layer
    template <typename PreviousLayer>
    class Trainer<Layers::ClippedReLU<PreviousLayer>> {
    private:
        // Type of layer to learn
        using LayerType = Layers::ClippedReLU<PreviousLayer>;

    public:
        // factory function
        static std::shared_ptr<Trainer> create(
            LayerType* target_layer, FeatureTransformer* ft) {

            return std::shared_ptr<Trainer>(
                new Trainer(target_layer, ft));
        }

        // Set options such as hyperparameters
        void send_message(Message* message) {
            previous_layer_trainer_->send_message(message);
            if (receive_message("check_health", message)) {
                check_health();
            }
        }

        // Initialize the parameters with random numbers
        template <typename RNG>
        void initialize(RNG& rng) {
            previous_layer_trainer_->initialize(rng);
        }

        // forward propagation
        const LearnFloatType* propagate(const std::vector<Example>& batch) {
            if (output_.size() < kOutputDimensions * batch.size()) {
              output_.resize(kOutputDimensions * batch.size());
              gradients_.resize(kInputDimensions * batch.size());
            }

            const auto input = previous_layer_trainer_->propagate(batch);
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

        // backpropagation
        void backpropagate(const LearnFloatType* gradients,
                           LearnFloatType learning_rate) {

            for (IndexType b = 0; b < batch_size_; ++b) {
                const IndexType batch_offset = kOutputDimensions * b;
                for (IndexType i = 0; i < kOutputDimensions; ++i) {
                    const IndexType index = batch_offset + i;
                    gradients_[index] = gradients[index] *
                        (output_[index] > kZero) * (output_[index] < kOne);
                }
            }

            previous_layer_trainer_->backpropagate(gradients_.data(), learning_rate);
        }

    private:
        // constructor
        Trainer(LayerType* target_layer, FeatureTransformer* ft) :
            batch_size_(0),
            previous_layer_trainer_(Trainer<PreviousLayer>::create(
                &target_layer->previous_layer_, ft)),
            target_layer_(target_layer) {

            std::fill(std::begin(min_activations_), std::end(min_activations_),
                      std::numeric_limits<LearnFloatType>::max());
            std::fill(std::begin(max_activations_), std::end(max_activations_),
                      std::numeric_limits<LearnFloatType>::lowest());
        }

        // Check if there are any problems with learning
        void check_health() {

            const auto largest_min_activation = *std::max_element(
                std::begin(min_activations_), std::end(min_activations_));
            const auto smallest_max_activation = *std::min_element(
                std::begin(max_activations_), std::end(max_activations_));

            auto out = sync_region_cout.new_region();

            out << "INFO (check_health):"
                << " layer " << LayerType::kLayerIndex
                << " - " << LayerType::get_name()
                << std::endl;

            out << "  - largest min activation = " << largest_min_activation
                << " , smallest max activation = " << smallest_max_activation
                << std::endl;

            out.unlock();

            std::fill(std::begin(min_activations_), std::end(min_activations_),
                      std::numeric_limits<LearnFloatType>::max());
            std::fill(std::begin(max_activations_), std::end(max_activations_),
                      std::numeric_limits<LearnFloatType>::lowest());
        }

        // number of input/output dimensions
        static constexpr IndexType kInputDimensions = LayerType::kOutputDimensions;
        static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;

        // LearnFloatType constant
        static constexpr LearnFloatType kZero = static_cast<LearnFloatType>(0.0);
        static constexpr LearnFloatType kOne = static_cast<LearnFloatType>(1.0);

        // number of samples in mini-batch
        IndexType batch_size_;

        // Trainer of the previous layer
        const std::shared_ptr<Trainer<PreviousLayer>> previous_layer_trainer_;

        // layer to learn
        LayerType* const target_layer_;

        // Forward propagation buffer
        std::vector<LearnFloatType> output_;

        // buffer for back propagation
        std::vector<LearnFloatType> gradients_;

        // Health check statistics
        LearnFloatType min_activations_[kOutputDimensions];
        LearnFloatType max_activations_[kOutputDimensions];
    };

}  // namespace Eval::NNUE

#endif
