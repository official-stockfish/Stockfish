#ifndef _NNUE_TRAINER_INPUT_SLICE_H_
#define _NNUE_TRAINER_INPUT_SLICE_H_

#include "trainer.h"

#include "extra/stockfish_blas.h"

#include "learn/learn.h"

#include "nnue/layers/input_slice.h"

#include "thread.h"

// Specialization of NNUE evaluation function learning class template for InputSlice
namespace Eval::NNUE {

    // Learning: Input layer
    class SharedInputTrainer {
    public:
        // factory function
        static std::shared_ptr<SharedInputTrainer> create(
            FeatureTransformer* ft) {

            static std::shared_ptr<SharedInputTrainer> instance;

            if (!instance) {
                instance.reset(new SharedInputTrainer(ft));
            }

            ++instance->num_referrers_;

            return instance;
        }

        // Set options such as hyperparameters
        void send_message(Message* message) {
            if (num_calls_[0] == 0) {
                current_operation_ = Operation::kSendMessage;
                feature_transformer_trainer_->send_message(message);
            }

            assert(current_operation_ == Operation::kSendMessage);

            if (++num_calls_[0] == num_referrers_) {
                num_calls_[0] = 0;
                current_operation_ = Operation::kNone;
            }
        }

        // Initialize the parameters with random numbers
        template <typename RNG>
        void initialize(RNG& rng) {
            if (num_calls_[0] == 0) {
                current_operation_ = Operation::kInitialize;
                feature_transformer_trainer_->initialize(rng);
            }

            assert(current_operation_ == Operation::kInitialize);

            if (++num_calls_[0] == num_referrers_) {
                num_calls_[0] = 0;
                current_operation_ = Operation::kNone;
            }
        }

        const LearnFloatType* step_start(ThreadPool& thread_pool, std::vector<Example>::const_iterator batch_begin, std::vector<Example>::const_iterator batch_end)
        {
            const auto size = batch_end - batch_begin;
            
            if (gradients_.size() < kInputDimensions * size) {
                gradients_.resize(kInputDimensions * size);
            }

            if (num_calls_.size() < thread_pool.size())
            {
                num_calls_.resize(thread_pool.size(), 0);
            }

            batch_size_ = size;

            if (num_calls_[0] == 0) {
                current_operation_ = Operation::kStepStart;
                output_ = feature_transformer_trainer_->step_start(thread_pool, batch_begin, batch_end);
            }

            assert(current_operation_ == Operation::kStepStart);

            if (++num_calls_[0] == num_referrers_) {
                num_calls_[0] = 0;
                current_operation_ = Operation::kNone;
            }

            return output_;
        }

        // forward propagation
        void propagate(Thread& th, uint64_t offset, uint64_t count) {
            const auto thread_id = th.thread_idx();

            if (num_calls_[thread_id] == 0) {
                current_operation_ = Operation::kPropagate;
                feature_transformer_trainer_->propagate(th, offset, count);
            }

            assert(current_operation_ == Operation::kPropagate);

            if (++num_calls_[thread_id] == num_referrers_) {
                num_calls_[thread_id] = 0;
                current_operation_ = Operation::kNone;
            }
        }

        // backpropagation
        void backpropagate(Thread& th,
                           const LearnFloatType* gradients,
                           uint64_t offset,
                           uint64_t count) {

            const auto thread_id = th.thread_idx();

            if (num_referrers_ == 1) {
                feature_transformer_trainer_->backpropagate(th, gradients, offset, count);
                return;
            }

            if (num_calls_[thread_id] == 0) {
                current_operation_ = Operation::kBackPropagate;
                for (IndexType b = offset; b < offset + count; ++b) {
                    const IndexType batch_offset = kInputDimensions * b;
                    for (IndexType i = 0; i < kInputDimensions; ++i) {
                        gradients_[batch_offset + i] = static_cast<LearnFloatType>(0.0);
                    }
                }
            }

            assert(current_operation_ == Operation::kBackPropagate);

            for (IndexType b = offset; b < offset + count; ++b) {
                const IndexType batch_offset = kInputDimensions * b;
                for (IndexType i = 0; i < kInputDimensions; ++i) {
                    gradients_[batch_offset + i] += gradients[batch_offset + i];
                }
            }

            if (++num_calls_[thread_id] == num_referrers_) {
                feature_transformer_trainer_->backpropagate(
                    th, gradients_.data(), offset, count);
                num_calls_[thread_id] = 0;
                current_operation_ = Operation::kNone;
            }
        }

        void step_end(ThreadPool& thread_pool, LearnFloatType learning_rate) {
            if (num_calls_[0] == 0) {
                current_operation_ = Operation::kStepEnd;
                feature_transformer_trainer_->step_end(thread_pool, learning_rate);
            }

            assert(current_operation_ == Operation::kStepEnd);

            if (++num_calls_[0] == num_referrers_) {
                num_calls_[0] = 0;
                current_operation_ = Operation::kNone;
            }
        }

    private:
        // constructor
        SharedInputTrainer(FeatureTransformer* ft) :
            batch_size_(0),
            num_referrers_(0),
            num_calls_(1, 0),
            current_operation_(Operation::kNone),
            feature_transformer_trainer_(Trainer<FeatureTransformer>::create(
                ft)),
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
            kStepStart,
            kPropagate,
            kBackPropagate,
            kStepEnd,
        };

        // number of samples in mini-batch
        IndexType batch_size_;

        // number of layers sharing this layer as input
        std::uint32_t num_referrers_;

        // Number of times the current process has been called
        std::vector<std::uint32_t> num_calls_;

        // current processing type
        Operation current_operation_;

        // Trainer of input feature converter
        const std::shared_ptr<Trainer<FeatureTransformer>>
            feature_transformer_trainer_;

        // pointer to output shared for forward propagation
        const LearnFloatType* output_;

        // buffer for back propagation
        std::vector<LearnFloatType, CacheLineAlignedAllocator<LearnFloatType>> gradients_;
    };

    // Learning: Input layer
    template <IndexType OutputDimensions, IndexType Offset>
    class Trainer<Layers::InputSlice<OutputDimensions, Offset>> {
    private:
        // Type of layer to learn
        using LayerType = Layers::InputSlice<OutputDimensions, Offset>;

    public:
        // factory function
        static std::shared_ptr<Trainer> create(
            LayerType* /*target_layer*/, FeatureTransformer* ft) {

            return std::shared_ptr<Trainer>(new Trainer(ft));
        }

        // Set options such as hyperparameters
        void send_message(Message* message) {
            shared_input_trainer_->send_message(message);
        }

        // Initialize the parameters with random numbers
        template <typename RNG>
        void initialize(RNG& rng) {
            shared_input_trainer_->initialize(rng);
        }

        const LearnFloatType* step_start(ThreadPool& thread_pool, std::vector<Example>::const_iterator batch_begin, std::vector<Example>::const_iterator batch_end)
        {
            const auto size = batch_end - batch_begin;

            if (output_.size() < kOutputDimensions * size) {
              output_.resize(kOutputDimensions * size);
              gradients_.resize(kInputDimensions * size);
            }

            batch_size_ = size;

            input_ = shared_input_trainer_->step_start(thread_pool, batch_begin, batch_end);

            return output_.data();
        }

        // forward propagation
        void propagate(Thread& th, uint64_t offset, uint64_t count) {

            shared_input_trainer_->propagate(th, offset, count);

            for (IndexType b = offset; b < offset + count; ++b) {
                const IndexType input_offset = kInputDimensions * b;
                const IndexType output_offset = kOutputDimensions * b;

#if defined(USE_BLAS)

                cblas_scopy(
                    kOutputDimensions, &input_[input_offset + Offset], 1,
                    &output_[output_offset], 1
                );
#else

                Blas::scopy(
                    kOutputDimensions, &input_[input_offset + Offset], 1,
                    &output_[output_offset], 1
                );

#endif
            }
        }

        // backpropagation
        void backpropagate(Thread& th,
                           const LearnFloatType* gradients,
                           uint64_t offset,
                           uint64_t count) {

            for (IndexType b = offset; b < offset + count; ++b)
            {
                const IndexType input_offset = kInputDimensions * b;
                const IndexType output_offset = kOutputDimensions * b;

                IndexType i = 0;
                for (; i < Offset; ++i) {
                    gradients_[input_offset + i] = static_cast<LearnFloatType>(0.0);
                }

                for (; i < Offset + kOutputDimensions; ++i) {
                    gradients_[input_offset + i] = gradients[output_offset + i - Offset];
                }

                for (; i < kInputDimensions; ++i)
                {
                    gradients_[input_offset + i] = static_cast<LearnFloatType>(0.0);
                }
            }

            shared_input_trainer_->backpropagate(th, gradients_.data(), offset, count);
        }

        void step_end(ThreadPool& thread_pool, LearnFloatType learning_rate) {
            shared_input_trainer_->step_end(thread_pool, learning_rate);
        }

    private:
        // constructor
        Trainer(FeatureTransformer* ft) :
            batch_size_(0),
            shared_input_trainer_(SharedInputTrainer::create(ft)) {
        }

        // number of input/output dimensions
        static constexpr IndexType kInputDimensions =
            FeatureTransformer::kOutputDimensions;
        static constexpr IndexType kOutputDimensions = OutputDimensions;
        static_assert(Offset + kOutputDimensions <= kInputDimensions, "");

        // number of samples in mini-batch
        IndexType batch_size_;

        const LearnFloatType* input_;

        // Trainer of shared input layer
        const std::shared_ptr<SharedInputTrainer> shared_input_trainer_;

        // Forward propagation buffer
        std::vector<LearnFloatType, CacheLineAlignedAllocator<LearnFloatType>> output_;

        // buffer for back propagation
        std::vector<LearnFloatType, CacheLineAlignedAllocator<LearnFloatType>> gradients_;
    };

}  // namespace Eval::NNUE

#endif
