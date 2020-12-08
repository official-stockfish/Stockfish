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
    // This is tricky. It exists because when there's more than one trainer
    // on top of a single feature transformer we want to only call propagate/backpropagate
    // on the feature transformer once. This is straightforward in the old
    // multithreading case, because propagate/backpropagate is called just once from the
    // main thread. But with the current implementation of coarser multithreading
    // we end up calling each method from each thread. Therefore we have to keep
    // the num_calls and current_operation per thread basis, each thread must work
    // on its designated batch slice, and the only synchronization points are
    // step_start and step_end - for which we use state of the first thread.
    // Each thread requires their own bookkeeping because it's possible that
    // one thread is still in propagate of some batch slice while the other thread
    // is doing backpropagate of some other slice. We also ensure the thread state
    // isn't suspectible to false sharing by using a full cache line for the state.
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
            auto& thread_state = thread_states_[0];

            if (thread_state.num_calls == 0) {
                thread_state.current_operation = Operation::kSendMessage;
                feature_transformer_trainer_->send_message(message);
            }

            assert(thread_state.current_operation == Operation::kSendMessage);

            if (++thread_state.num_calls == num_referrers_) {
                thread_state.num_calls = 0;
                thread_state.current_operation = Operation::kNone;
            }
        }

        // Initialize the parameters with random numbers
        template <typename RNG>
        void initialize(RNG& rng) {
            auto& thread_state = thread_states_[0];

            if (thread_state.num_calls == 0) {
                thread_state.current_operation = Operation::kInitialize;
                feature_transformer_trainer_->initialize(rng);
            }

            assert(thread_state.current_operation == Operation::kInitialize);

            if (++thread_state.num_calls == num_referrers_) {
                thread_state.num_calls = 0;
                thread_state.current_operation = Operation::kNone;
            }
        }

        const LearnFloatType* step_start(ThreadPool& thread_pool, std::vector<Example>::const_iterator batch_begin, std::vector<Example>::const_iterator batch_end)
        {
            const auto size = batch_end - batch_begin;

            if ((long)gradients_.size() < (long)kInputDimensions * size) {
                gradients_.resize(kInputDimensions * size);
            }

            if (thread_states_.size() < thread_pool.size())
            {
                thread_states_.resize(thread_pool.size());
            }

            batch_size_ = size;

            auto& thread_state = thread_states_[0];

            if (thread_state.num_calls == 0) {
                thread_state.current_operation = Operation::kStepStart;
                output_ = feature_transformer_trainer_->step_start(thread_pool, batch_begin, batch_end);
            }

            assert(thread_state.current_operation == Operation::kStepStart);

            if (++thread_state.num_calls == num_referrers_) {
                thread_state.num_calls = 0;
                thread_state.current_operation = Operation::kNone;
            }

            return output_;
        }

        // forward propagation
        void propagate(Thread& th, uint64_t offset, uint64_t count) {
            const auto thread_id = th.thread_idx();

            auto& thread_state = thread_states_[thread_id];

            if (thread_state.num_calls == 0) {
                thread_state.current_operation = Operation::kPropagate;
                feature_transformer_trainer_->propagate(th, offset, count);
            }

            assert(thread_state.current_operation == Operation::kPropagate);

            if (++thread_state.num_calls == num_referrers_) {
                thread_state.num_calls = 0;
                thread_state.current_operation = Operation::kNone;
            }
        }

        // backpropagation
        void backpropagate(Thread& th,
                           const LearnFloatType* gradients,
                           uint64_t offset,
                           uint64_t count) {

            const auto thread_id = th.thread_idx();

            auto& thread_state = thread_states_[thread_id];

            if (num_referrers_ == 1) {
                feature_transformer_trainer_->backpropagate(th, gradients, offset, count);
                return;
            }

            if (thread_state.num_calls == 0) {
                thread_state.current_operation = Operation::kBackPropagate;
                for (IndexType b = offset; b < offset + count; ++b) {
                    const IndexType batch_offset = kInputDimensions * b;
                    for (IndexType i = 0; i < kInputDimensions; ++i) {
                        gradients_[batch_offset + i] = static_cast<LearnFloatType>(0.0);
                    }
                }
            }

            assert(thread_state.current_operation == Operation::kBackPropagate);

            for (IndexType b = offset; b < offset + count; ++b) {
                const IndexType batch_offset = kInputDimensions * b;
                for (IndexType i = 0; i < kInputDimensions; ++i) {
                    gradients_[batch_offset + i] += gradients[batch_offset + i];
                }
            }

            if (++thread_state.num_calls == num_referrers_) {
                feature_transformer_trainer_->backpropagate(
                    th, gradients_.data(), offset, count);
                thread_state.num_calls = 0;
                thread_state.current_operation = Operation::kNone;
            }
        }

        void step_end(ThreadPool& thread_pool, LearnFloatType learning_rate) {
            auto& thread_state = thread_states_[0];

            if (thread_state.num_calls == 0) {
                thread_state.current_operation = Operation::kStepEnd;
                feature_transformer_trainer_->step_end(thread_pool, learning_rate);
            }

            assert(thread_state.current_operation == Operation::kStepEnd);

            if (++thread_state.num_calls == num_referrers_) {
                thread_state.num_calls = 0;
                thread_state.current_operation = Operation::kNone;
            }
        }

    private:
        // constructor
        SharedInputTrainer(FeatureTransformer* ft) :
            batch_size_(0),
            num_referrers_(0),
            thread_states_(1),
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

        struct alignas(kCacheLineSize) ThreadState
        {
            std::uint32_t num_calls{0};

            // current processing type
            Operation current_operation = Operation::kNone;
        };

        // Number of times the current process has been called
        std::vector<ThreadState, CacheLineAlignedAllocator<ThreadState>> thread_states_;

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

            if ((long)output_.size() < (long)kOutputDimensions * size) {
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
