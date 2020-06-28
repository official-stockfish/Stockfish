// Interface used for learning NNUE evaluation function

#ifndef _EVALUATE_NNUE_LEARNER_H_
#define _EVALUATE_NNUE_LEARNER_H_

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include "../../learn/learn.h"

namespace Eval {

namespace NNUE {

// Initialize learning
void InitializeTraining(double eta1, uint64_t eta1_epoch,
                        double eta2, uint64_t eta2_epoch, double eta3);

// set the number of samples in the mini-batch
void SetBatchSize(uint64_t size);

// set the learning rate scale
void SetGlobalLearningRateScale(double scale);

// Set options such as hyperparameters
void SetOptions(const std::string& options);

// Reread the evaluation function parameters for learning from the file
void RestoreParameters(const std::string& dir_name);

// Add 1 sample of learning data
void AddExample(Position& pos, Color rootColor,
                const Learner::PackedSfenValue& psv, double weight);

// update the evaluation function parameters
void UpdateParameters(uint64_t epoch);

// Check if there are any problems with learning
void CheckHealth();

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
