#include <random>
#include <fstream>

#include "evaluate_nnue.h"
#include "evaluate_nnue_learner.h"

#include "trainer/features/all_factorizers.h"

#include "trainer/trainer_feature_transformer.h"
#include "trainer/trainer_input_slice.h"
#include "trainer/trainer_affine_transform.h"
#include "trainer/trainer_clipped_relu.h"
#include "trainer/trainer_sum.h"

#include "position.h"
#include "uci.h"
#include "misc.h"
#include "thread_win32_osx.h"
#include "thread.h"

// Code for learning NNUE evaluation function
namespace Eval::NNUE {

    namespace {

        // learning data
        std::vector<Example> examples;

        // Mutex for exclusive control of examples
        std::mutex examples_mutex;

        // number of samples in mini-batch
        uint64_t batch_size;

        // random number generator
        std::mt19937 rng;

        // learner
        std::shared_ptr<Trainer<Network>> trainer;

        // Tell the learner options such as hyperparameters
        void send_messages(std::vector<Message> messages) {
            for (auto& message : messages) {
                trainer->send_message(&message);
                assert(message.num_receivers > 0);
            }
        }

    }  // namespace

    // Initialize learning
    void initialize_training(
        const std::string& seed,
        SynchronizedRegionLogger::Region& out) {

#if defined (OPENBLAS_VERSION)
        openblas_set_num_threads(1);
#elif defined (INTEL_MKL_VERSION)
        mkl_set_num_threads(1);
#endif

        out << "INFO (initialize_training): Initializing NN training for "
            << get_architecture_string() << std::endl;

        out << std::endl;

        out << "Layers:\n"
            << get_layers_info() << std::endl;

        out << std::endl;

        out << "Factorizers:\n"
            << Features::Factorizer<RawFeatures>::get_factorizers_string() << std::endl;

        out << std::endl;

        assert(feature_transformer);
        assert(network);

        trainer = Trainer<Network>::create(network.get(), feature_transformer.get());
        rng.seed(PRNG(seed).rand<uint64_t>());

        if (Options["SkipLoadingEval"]) {
            out << "INFO (initialize_training): Performing random net initialization.\n";
            trainer->initialize(rng);
        }
    }

    // set the number of samples in the mini-batch
    void set_batch_size(uint64_t size) {
        assert(size > 0);
        batch_size = size;
    }

    // Set options such as hyperparameters
    void set_options(const std::string& options) {
        std::vector<Message> messages;
        for (const auto& option : Algo::split(options, ',')) {
          const auto fields = Algo::split(option, '=');
          assert(fields.size() == 1 || fields.size() == 2);

          if (fields.size() == 1) {
              messages.emplace_back(fields[0]);
          } else {
              messages.emplace_back(fields[0], fields[1]);
          }
        }

        send_messages(std::move(messages));
    }

    // Reread the evaluation function parameters for learning from the file
    void restore_parameters(const std::string& dir_name) {
        const std::string file_name = Path::combine(dir_name, NNUE::savedfileName);
        std::ifstream stream(file_name, std::ios::binary);
#ifndef NDEBUG
        bool result =
#endif
        ReadParameters(stream);
#ifndef NDEBUG
        assert(result);
#endif

        send_messages({{"reset"}});
    }

    void finalize_net() {
        send_messages({{"clear_unobserved_feature_weights"}});
    }

    // Add 1 sample of learning data
    void add_example(
        Position& pos,
        Color rootColor,
        Value discrete_nn_eval,
        const Learner::PackedSfenValue& psv,
        double weight) {

        Example example;
        if (rootColor == pos.side_to_move()) {
            example.sign = 1;
        } else {
            example.sign = -1;
        }

        example.discrete_nn_eval = discrete_nn_eval;
        example.psv = psv;
        example.weight = weight;

        Features::IndexList active_indices[2];
        for (const auto trigger : kRefreshTriggers) {
            RawFeatures::append_active_indices(pos, trigger, active_indices);
        }

        if (pos.side_to_move() != WHITE) {
            active_indices[0].swap(active_indices[1]);
        }

        static thread_local std::vector<TrainingFeature> s_training_features;
        auto& training_features = s_training_features;

        for (const auto color : Colors) {
            training_features.clear();

            for (const auto base_index : active_indices[color]) {
                static_assert(Features::Factorizer<RawFeatures>::get_dimensions() <
                              (1 << TrainingFeature::kIndexBits), "");
                Features::Factorizer<RawFeatures>::append_training_features(
                    base_index, &training_features);
            }

            std::sort(training_features.begin(), training_features.end());

            auto& unique_features = example.training_features[color];
            unique_features.reserve(training_features.size());
            for (const auto& feature : training_features) {
                if (!unique_features.empty() &&
                    feature.get_index() == unique_features.back().get_index()) {

                    unique_features.back() += feature;
                } else {
                    unique_features.push_back(feature);
                }
            }
        }

        std::lock_guard<std::mutex> lock(examples_mutex);
        examples.push_back(std::move(example));
    }

    // update the evaluation function parameters
    Learner::Loss update_parameters(
        ThreadPool& thread_pool,
        uint64_t epoch,
        bool verbose,
        double learning_rate,
        double max_grad,
        Learner::CalcLossFunc calc_loss)
    {
        using namespace Learner::Autograd::UnivariateStatic;

        assert(batch_size > 0);

        learning_rate /= batch_size;

        std::lock_guard<std::mutex> lock(examples_mutex);

        double abs_eval_diff_sum = 0.0;
        double abs_discrete_eval_sum = 0.0;
        double gradient_norm = 0.0;

        bool collect_stats = verbose;

        Learner::Loss loss_sum{};

        std::vector<double> abs_eval_diff_sum_local(thread_pool.size(), 0.0);
        std::vector<double> abs_discrete_eval_sum_local(thread_pool.size(), 0.0);
        std::vector<double> gradient_norm_local(thread_pool.size(), 0.0);
        std::vector<Learner::Loss> loss_sum_local(thread_pool.size());

        auto prev_batch_begin = examples.end();
        while ((long)(prev_batch_begin - examples.begin()) >= (long)batch_size) {
            auto batch_begin = prev_batch_begin - batch_size;
            auto batch_end = prev_batch_begin;
            auto size = batch_end - batch_begin;
            const auto network_output = trainer->step_start(thread_pool, batch_begin, batch_end);
            std::vector<LearnFloatType> gradients(size);

            thread_pool.for_each_index_chunk_with_workers(
                std::size_t(0), size,
                [&](Thread& th, std::size_t offset, std::size_t count) {
                    const auto thread_id = th.thread_idx();

                    trainer->propagate(th, offset, count);

                    for (std::size_t b = offset; b < offset + count; ++b) {
                        const auto& e = *(batch_begin + b);
                        const auto shallow = static_cast<Value>(round<std::int32_t>(
                            e.sign * network_output[b] * kPonanzaConstant));
                        const auto discrete = e.sign * e.discrete_nn_eval;
                        const auto& psv = e.psv;
                        auto loss = calc_loss(shallow, (Value)psv.score, psv.game_result, psv.gamePly);
                        loss.grad = std::clamp(
                            loss.grad * e.sign * kPonanzaConstant * e.weight, -max_grad, max_grad);
                        gradients[b] = static_cast<LearnFloatType>(loss.grad);
                        loss_sum_local[thread_id] += loss;

                        // The discrete eval will only be valid before first backpropagation,
                        // that is only for the first batch.
                        // Similarily we want only gradients from one batch.
                        if (collect_stats)
                        {
                            abs_eval_diff_sum_local[thread_id] += std::abs(discrete - shallow);
                            abs_discrete_eval_sum_local[thread_id] += std::abs(discrete);
                            gradient_norm_local[thread_id] += std::abs(loss.grad);
                        }
                    }

                    trainer->backpropagate(th, gradients.data(), offset, count);
                }
            );

            // We can asyncronously erase the examples that we used in the previous
            // step. This can be done safely because we're no longer using these
            // examples and erase won't invalidate iterators.
            examples.erase(prev_batch_begin, examples.end());
            prev_batch_begin = batch_begin;

            thread_pool.wait_for_workers_finished();

            trainer->step_end(thread_pool, learning_rate);

            collect_stats = false;
        }
        examples.erase(prev_batch_begin, examples.end());

        if (verbose)
        {
            abs_eval_diff_sum = std::accumulate(abs_eval_diff_sum_local.begin(), abs_eval_diff_sum_local.end(), 0.0);
            abs_discrete_eval_sum = std::accumulate(abs_discrete_eval_sum_local.begin(), abs_discrete_eval_sum_local.end(), 0.0);
            gradient_norm = std::accumulate(gradient_norm_local.begin(), gradient_norm_local.end(), 0.0);

            const double avg_abs_eval_diff = abs_eval_diff_sum / batch_size;
            const double avg_abs_discrete_eval = abs_discrete_eval_sum / batch_size;

            auto out = sync_region_cout.new_region();

            out << "INFO (update_parameters):"
                << " epoch = " << epoch
                << " , avg_abs(trainer_eval-nnue_eval) = " << avg_abs_eval_diff
                << " , avg_abs(nnue_eval) = " << avg_abs_discrete_eval
                << " , avg_relative_error = " << avg_abs_eval_diff / avg_abs_discrete_eval
                << " , batch_size = " << batch_size
                << " , grad_norm = " << gradient_norm
                << std::endl;
        } else {
            // Display some progress but don't synchronize as
            // we can't really decide when to release the output lock here
            std::cout << '.';
        }

        send_messages({{"quantize_parameters"}});

        for(auto& loss : loss_sum_local)
        {
            loss_sum += loss;
        }

        return loss_sum;
    }

    // Check if there are any problems with learning
    void check_health() {
        send_messages({{"check_health"}});
    }

    // save merit function parameters to a file
    void save_eval(std::string dir_name) {
        auto eval_dir = Path::combine(Options["EvalSaveDir"], dir_name);

        auto out = sync_region_cout.new_region();

        out << "INFO (save_eval): Saving current evaluation file in " << eval_dir << std::endl;

        // mkdir() will fail if this folder already exists, but
        // Apart from that. If not, I just want you to make it.
        // Also, assume that the folders up to EvalSaveDir have been dug.
        sys::create_directories(eval_dir);

        const std::string file_name = Path::combine(eval_dir, NNUE::savedfileName);
        std::ofstream stream(file_name, std::ios::binary);
#ifndef NDEBUG
        bool result =
#endif
        WriteParameters(stream);
#ifndef NDEBUG
        assert(result);
#endif
        out << "INFO (save_eval): Finished saving evaluation file in " << eval_dir << std::endl;
    }
}  // namespace Eval::NNUE
