#ifndef _NNUE_TRAINER_CLIPPED_RELU_H_
#define _NNUE_TRAINER_CLIPPED_RELU_H_

#include "trainer.h"

#include "learn/learn.h"

#include "nnue/layers/clipped_relu.h"

#include "thread.h"

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

        const LearnFloatType* step_start(ThreadPool& thread_pool, std::vector<Example>::const_iterator batch_begin, std::vector<Example>::const_iterator batch_end)
        {
            const auto size = batch_end - batch_begin;

            if ((long)output_.size() < (long)kOutputDimensions * size) {
              output_.resize(kOutputDimensions * size);
              gradients_.resize(kInputDimensions * size);
            }

            if (thread_states_.size() < thread_pool.size())
            {
                thread_states_.resize(thread_pool.size());
            }

            input_ = previous_layer_trainer_->step_start(thread_pool, batch_begin, batch_end);

            batch_size_ = size;

            return output_.data();
        }

        // forward propagation
        void propagate(Thread& th, const uint64_t offset, const uint64_t count) {

            auto& thread_state = thread_states_[th.thread_idx()];

            previous_layer_trainer_->propagate(th, offset, count);

#if defined (USE_SSE2)

            {
                static_assert(kOutputDimensions % 16 == 0, "This implementation assumes that it can process 16 floats at a time");

                const __m128 kZero4 = _mm_set1_ps(+kZero);
                const __m128 kOne4 = _mm_set1_ps(+kOne);

                for (IndexType b = offset; b < offset + count; ++b)
                {
                    const IndexType batch_offset = kOutputDimensions * b;

                    for (IndexType i = 0; i < kOutputDimensions; i += 16)
                    {
                        __m128 out0 = _mm_loadu_ps(&input_[i + 0 + batch_offset]);
                        __m128 out1 = _mm_loadu_ps(&input_[i + 4 + batch_offset]);
                        __m128 out2 = _mm_loadu_ps(&input_[i + 8 + batch_offset]);
                        __m128 out3 = _mm_loadu_ps(&input_[i + 12 + batch_offset]);

                        out0 = _mm_max_ps(kZero4, _mm_min_ps(kOne4, out0));
                        out1 = _mm_max_ps(kZero4, _mm_min_ps(kOne4, out1));
                        out2 = _mm_max_ps(kZero4, _mm_min_ps(kOne4, out2));
                        out3 = _mm_max_ps(kZero4, _mm_min_ps(kOne4, out3));

                        _mm_storeu_ps(&output_[i + 0 + batch_offset], out0);
                        _mm_storeu_ps(&output_[i + 4 + batch_offset], out1);
                        _mm_storeu_ps(&output_[i + 8 + batch_offset], out2);
                        _mm_storeu_ps(&output_[i + 12 + batch_offset], out3);

                        __m128 minact0 = _mm_loadu_ps(&thread_state.min_activations_[i + 0]);
                        __m128 minact1 = _mm_loadu_ps(&thread_state.min_activations_[i + 4]);
                        __m128 minact2 = _mm_loadu_ps(&thread_state.min_activations_[i + 8]);
                        __m128 minact3 = _mm_loadu_ps(&thread_state.min_activations_[i + 12]);

                        __m128 maxact0 = _mm_loadu_ps(&thread_state.max_activations_[i + 0]);
                        __m128 maxact1 = _mm_loadu_ps(&thread_state.max_activations_[i + 4]);
                        __m128 maxact2 = _mm_loadu_ps(&thread_state.max_activations_[i + 8]);
                        __m128 maxact3 = _mm_loadu_ps(&thread_state.max_activations_[i + 12]);

                        minact0 = _mm_min_ps(out0, minact0);
                        minact1 = _mm_min_ps(out1, minact1);
                        minact2 = _mm_min_ps(out2, minact2);
                        minact3 = _mm_min_ps(out3, minact3);

                        maxact0 = _mm_max_ps(out0, maxact0);
                        maxact1 = _mm_max_ps(out1, maxact1);
                        maxact2 = _mm_max_ps(out2, maxact2);
                        maxact3 = _mm_max_ps(out3, maxact3);

                        _mm_storeu_ps(&thread_state.min_activations_[i + 0], minact0);
                        _mm_storeu_ps(&thread_state.min_activations_[i + 4], minact1);
                        _mm_storeu_ps(&thread_state.min_activations_[i + 8], minact2);
                        _mm_storeu_ps(&thread_state.min_activations_[i + 12], minact3);

                        _mm_storeu_ps(&thread_state.max_activations_[i + 0], maxact0);
                        _mm_storeu_ps(&thread_state.max_activations_[i + 4], maxact1);
                        _mm_storeu_ps(&thread_state.max_activations_[i + 8], maxact2);
                        _mm_storeu_ps(&thread_state.max_activations_[i + 12], maxact3);
                    }
                }
            }

#else

            for (IndexType b = offset; b < offset + count; ++b) {
                const IndexType batch_offset = kOutputDimensions * b;
                for (IndexType i = 0; i < kOutputDimensions; ++i) {
                    const IndexType index = batch_offset + i;
                    output_[index] = std::max(+kZero, std::min(+kOne, input_[index]));
                    thread_state.min_activations_[i] = std::min(thread_state.min_activations_[i], output_[index]);
                    thread_state.max_activations_[i] = std::max(thread_state.max_activations_[i], output_[index]);
                }
            }

#endif
        }

        // backpropagation
        void backpropagate(Thread& th,
                           const LearnFloatType* gradients,
                           const uint64_t offset,
                           const uint64_t count) {

            auto& thread_state = thread_states_[th.thread_idx()];

#if defined (USE_SSE2)

            {
                static_assert(kOutputDimensions % 16 == 0, "This implementation assumes that it can process 16 floats at a time");

                const __m128 kZero4 = _mm_set1_ps(+kZero);
                const __m128 kOne4 = _mm_set1_ps(+kOne);

                for (IndexType b = offset; b < offset + count; ++b)
                {
                    const IndexType batch_offset = kOutputDimensions * b;

                    for (IndexType i = 0; i < kOutputDimensions; i += 16)
                    {
                        __m128 out0 = _mm_loadu_ps(&output_[batch_offset + i + 0]);
                        __m128 out1 = _mm_loadu_ps(&output_[batch_offset + i + 4]);
                        __m128 out2 = _mm_loadu_ps(&output_[batch_offset + i + 8]);
                        __m128 out3 = _mm_loadu_ps(&output_[batch_offset + i + 12]);

                        __m128 clipped0 = _mm_or_ps(_mm_cmple_ps(out0, kZero4), _mm_cmpge_ps(out0, kOne4));
                        __m128 clipped1 = _mm_or_ps(_mm_cmple_ps(out1, kZero4), _mm_cmpge_ps(out1, kOne4));
                        __m128 clipped2 = _mm_or_ps(_mm_cmple_ps(out2, kZero4), _mm_cmpge_ps(out2, kOne4));
                        __m128 clipped3 = _mm_or_ps(_mm_cmple_ps(out3, kZero4), _mm_cmpge_ps(out3, kOne4));

                        __m128 grad0 = _mm_loadu_ps(&gradients[batch_offset + i + 0]);
                        __m128 grad1 = _mm_loadu_ps(&gradients[batch_offset + i + 4]);
                        __m128 grad2 = _mm_loadu_ps(&gradients[batch_offset + i + 8]);
                        __m128 grad3 = _mm_loadu_ps(&gradients[batch_offset + i + 12]);

                        grad0 = _mm_andnot_ps(clipped0, grad0);
                        grad1 = _mm_andnot_ps(clipped1, grad1);
                        grad2 = _mm_andnot_ps(clipped2, grad2);
                        grad3 = _mm_andnot_ps(clipped3, grad3);

                        _mm_storeu_ps(&gradients_[batch_offset + i + 0], grad0);
                        _mm_storeu_ps(&gradients_[batch_offset + i + 4], grad1);
                        _mm_storeu_ps(&gradients_[batch_offset + i + 8], grad2);
                        _mm_storeu_ps(&gradients_[batch_offset + i + 12], grad3);

                        const int clipped_mask =
                            (_mm_movemask_ps(clipped0) << 0)
                            | (_mm_movemask_ps(clipped1) << 4)
                            | (_mm_movemask_ps(clipped2) << 8)
                            | (_mm_movemask_ps(clipped3) << 12);

                        thread_state.num_clipped_ += popcount(clipped_mask);
                    }
                }
            }

#else

            for (IndexType b = offset; b < offset + count; ++b) {
                const IndexType batch_offset = kOutputDimensions * b;
                for (IndexType i = 0; i < kOutputDimensions; ++i) {
                    const IndexType index = batch_offset + i;
                    const bool clipped = (output_[index] <= kZero) | (output_[index] >= kOne);
                    gradients_[index] = gradients[index] * !clipped;
                    thread_state.num_clipped_ += clipped;
                }
            }

#endif

            thread_state.num_total_ += count * kOutputDimensions;

            previous_layer_trainer_->backpropagate(th, gradients_.data(), offset, count);
        }

        void reduce_thread_state()
        {
            for (IndexType i = 1; i < thread_states_.size(); ++i)
            {
                thread_states_[0] += thread_states_[i];
            }
        }

        void step_end(ThreadPool& thread_pool, LearnFloatType learning_rate)
        {
            previous_layer_trainer_->step_end(thread_pool, learning_rate);
        }

    private:
        // constructor
        Trainer(LayerType* target_layer, FeatureTransformer* ft) :
            batch_size_(0),
            previous_layer_trainer_(Trainer<PreviousLayer>::create(
                &target_layer->previous_layer_, ft)),
            target_layer_(target_layer) {

            reset_stats();
        }

        void reset_stats() {
            for(auto& state : thread_states_)
                state.reset();
        }

        // Check if there are any problems with learning
        void check_health() {

            reduce_thread_state();

            auto& main_thread_state = thread_states_[0];

            const auto largest_min_activation = *std::max_element(
                std::begin(main_thread_state.min_activations_), std::end(main_thread_state.min_activations_));
            const auto smallest_max_activation = *std::min_element(
                std::begin(main_thread_state.max_activations_), std::end(main_thread_state.max_activations_));

            auto out = sync_region_cout.new_region();

            out << "INFO (check_health):"
                << " layer " << LayerType::kLayerIndex
                << " - " << LayerType::get_name()
                << std::endl;

            out << "  - largest min activation = " << largest_min_activation
                << " , smallest max activation = " << smallest_max_activation
                << std::endl;

            out << "  - clipped " << static_cast<double>(main_thread_state.num_clipped_) / main_thread_state.num_total_ * 100.0 << "% of outputs"
                << std::endl;

            out.unlock();

            reset_stats();
        }

        // number of input/output dimensions
        static constexpr IndexType kInputDimensions = LayerType::kOutputDimensions;
        static constexpr IndexType kOutputDimensions = LayerType::kOutputDimensions;

        // LearnFloatType constant
        static constexpr LearnFloatType kZero = static_cast<LearnFloatType>(0.0);
        static constexpr LearnFloatType kOne = static_cast<LearnFloatType>(1.0);

        // number of samples in mini-batch
        IndexType batch_size_;

        IndexType num_total_;

        const LearnFloatType* input_;

        // Trainer of the previous layer
        const std::shared_ptr<Trainer<PreviousLayer>> previous_layer_trainer_;

        // layer to learn
        LayerType* const target_layer_;

        // Forward propagation buffer
        std::vector<LearnFloatType, CacheLineAlignedAllocator<LearnFloatType>> output_;

        // buffer for back propagation
        std::vector<LearnFloatType, CacheLineAlignedAllocator<LearnFloatType>> gradients_;

        struct alignas(kCacheLineSize) ThreadState
        {
            // Health check statistics
            LearnFloatType min_activations_[kOutputDimensions];
            LearnFloatType max_activations_[kOutputDimensions];
            IndexType num_clipped_;
            IndexType num_total_;

            ThreadState() { reset(); }

            ThreadState& operator+=(const ThreadState& other)
            {
                for (IndexType i = 0; i < kOutputDimensions; ++i)
                {
                    min_activations_[i] = std::min(min_activations_[i], other.min_activations_[i]);
                }

                for (IndexType i = 0; i < kOutputDimensions; ++i)
                {
                    max_activations_[i] = std::max(max_activations_[i], other.max_activations_[i]);
                }

                num_clipped_ += other.num_clipped_;
                num_total_ += other.num_total_;

                return *this;
            }

            void reset()
            {
                std::fill(std::begin(min_activations_), std::end(min_activations_), std::numeric_limits<float>::max());
                std::fill(std::begin(max_activations_), std::end(max_activations_), std::numeric_limits<float>::lowest());
                num_clipped_ = 0;
                num_total_ = 0;
            }
        };

        std::vector<ThreadState, CacheLineAlignedAllocator<ThreadState>> thread_states_;
    };

}  // namespace Eval::NNUE

#endif
