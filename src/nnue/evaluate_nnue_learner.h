#ifndef _EVALUATE_NNUE_LEARNER_H_
#define _EVALUATE_NNUE_LEARNER_H_

#include "learn/learn.h"

// Interface used for learning NNUE evaluation function
namespace Eval::NNUE {

    // Initialize learning
    void initialize_training(const std::string& seed);

    // set the number of samples in the mini-batch
    void set_batch_size(uint64_t size);

    // Set options such as hyperparameters
    void set_options(const std::string& options);

    // Reread the evaluation function parameters for learning from the file
    void restore_parameters(const std::string& dir_name);

    // Add 1 sample of learning data
    void add_example(
        Position& pos,
        Color rootColor,
    	const Learner::PackedSfenValue& psv,
        double weight);

    // update the evaluation function parameters
    void update_parameters();

    // Check if there are any problems with learning
    void check_health();

    void finalize_net();

    void save_eval(std::string suffix);
}  // namespace Eval::NNUE

#endif
