#ifndef _EVALUATE_NNUE_LEARNER_H_
#define _EVALUATE_NNUE_LEARNER_H_

#include "learn/learn.h"

// Interface used for learning NNUE evaluation function
namespace Eval::NNUE {

    // Initialize learning
    void InitializeTraining(const std::string& seed);

    // set the number of samples in the mini-batch
    void SetBatchSize(uint64_t size);

    // Set options such as hyperparameters
    void SetOptions(const std::string& options);

    // Reread the evaluation function parameters for learning from the file
    void RestoreParameters(const std::string& dir_name);

    // Add 1 sample of learning data
    void AddExample(Position& pos, Color rootColor,
    	 const Learner::PackedSfenValue& psv, double weight);

    // update the evaluation function parameters
    void UpdateParameters();

    // Check if there are any problems with learning
    void CheckHealth();

    void FinalizeNet();

    void save_eval(std::string suffix);
}  // namespace Eval::NNUE

#endif
