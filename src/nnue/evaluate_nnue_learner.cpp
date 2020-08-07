// Code for learning NNUE evaluation function

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include <random>
#include <fstream>

#include "../../learn/learn.h"
#include "../../learn/learning_tools.h"

#include "../../position.h"
#include "../../uci.h"
#include "../../misc.h"
#include "../../thread_win32_osx.h"

#include "../evaluate_common.h"

#include "evaluate_nnue.h"
#include "evaluate_nnue_learner.h"
#include "trainer/features/factorizer_feature_set.h"
#include "trainer/features/factorizer_half_kp.h"
#include "trainer/trainer_feature_transformer.h"
#include "trainer/trainer_input_slice.h"
#include "trainer/trainer_affine_transform.h"
#include "trainer/trainer_clipped_relu.h"
#include "trainer/trainer_sum.h"

namespace Eval {

namespace NNUE {

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

// Learning rate scale
double global_learning_rate_scale;

// Get the learning rate scale
double GetGlobalLearningRateScale() {
  return global_learning_rate_scale;
}

// Tell the learner options such as hyperparameters
void SendMessages(std::vector<Message> messages) {
  for (auto& message : messages) {
    trainer->SendMessage(&message);
    assert(message.num_receivers > 0);
  }
}

}  // namespace

// Initialize learning
void InitializeTraining(double eta1, uint64_t eta1_epoch,
                        double eta2, uint64_t eta2_epoch, double eta3) {
  std::cout << "Initializing NN training for "
            << GetArchitectureString() << std::endl;

  assert(feature_transformer);
  assert(network);
  trainer = Trainer<Network>::Create(network.get(), feature_transformer.get());

  if (Options["SkipLoadingEval"]) {
    trainer->Initialize(rng);
  }

  global_learning_rate_scale = 1.0;
  EvalLearningTools::Weight::init_eta(eta1, eta2, eta3, eta1_epoch, eta2_epoch);
}

// set the number of samples in the mini-batch
void SetBatchSize(uint64_t size) {
  assert(size > 0);
  batch_size = size;
}

// set the learning rate scale
void SetGlobalLearningRateScale(double scale) {
  global_learning_rate_scale = scale;
}

// Set options such as hyperparameters
void SetOptions(const std::string& options) {
  std::vector<Message> messages;
  for (const auto& option : Split(options, ',')) {
    const auto fields = Split(option, '=');
    assert(fields.size() == 1 || fields.size() == 2);
    if (fields.size() == 1) {
      messages.emplace_back(fields[0]);
    } else {
      messages.emplace_back(fields[0], fields[1]);
    }
  }
  SendMessages(std::move(messages));
}

// Reread the evaluation function parameters for learning from the file
void RestoreParameters(const std::string& dir_name) {
  const std::string file_name = Path::Combine(dir_name, NNUE::savedfileName);
  std::ifstream stream(file_name, std::ios::binary);
  bool result = ReadParameters(stream);
  assert(result);

  SendMessages({{"reset"}});
}

// Add 1 sample of learning data
void AddExample(Position& pos, Color rootColor,
                const Learner::PackedSfenValue& psv, double weight) {
  Example example;
  if (rootColor == pos.side_to_move()) {
    example.sign = 1;
  } else {
    example.sign = -1;
  }
  example.psv = psv;
  example.weight = weight;

  Features::IndexList active_indices[2];
  for (const auto trigger : kRefreshTriggers) {
    RawFeatures::AppendActiveIndices(pos, trigger, active_indices);
  }
  if (pos.side_to_move() != WHITE) {
    active_indices[0].swap(active_indices[1]);
  }
  for (const auto color : Colors) {
    std::vector<TrainingFeature> training_features;
    for (const auto base_index : active_indices[color]) {
      static_assert(Features::Factorizer<RawFeatures>::GetDimensions() <
                    (1 << TrainingFeature::kIndexBits), "");
      Features::Factorizer<RawFeatures>::AppendTrainingFeatures(
          base_index, &training_features);
    }
    std::sort(training_features.begin(), training_features.end());

    auto& unique_features = example.training_features[color];
    for (const auto& feature : training_features) {
      if (!unique_features.empty() &&
          feature.GetIndex() == unique_features.back().GetIndex()) {
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
void UpdateParameters(uint64_t epoch) {
  assert(batch_size > 0);

  EvalLearningTools::Weight::calc_eta(epoch);
  const auto learning_rate = static_cast<LearnFloatType>(
      get_eta() / batch_size);

  std::lock_guard<std::mutex> lock(examples_mutex);
  std::shuffle(examples.begin(), examples.end(), rng);
  while (examples.size() >= batch_size) {
    std::vector<Example> batch(examples.end() - batch_size, examples.end());
    examples.resize(examples.size() - batch_size);

    const auto network_output = trainer->Propagate(batch);

    std::vector<LearnFloatType> gradients(batch.size());
    for (std::size_t b = 0; b < batch.size(); ++b) {
      const auto shallow = static_cast<Value>(Round<std::int32_t>(
          batch[b].sign * network_output[b] * kPonanzaConstant));
      const auto& psv = batch[b].psv;
      const double gradient = batch[b].sign * Learner::calc_grad(shallow, psv);
      gradients[b] = static_cast<LearnFloatType>(gradient * batch[b].weight);
    }

    trainer->Backpropagate(gradients.data(), learning_rate);
  }
  SendMessages({{"quantize_parameters"}});
}

// Check if there are any problems with learning
void CheckHealth() {
  SendMessages({{"check_health"}});
}

}  // namespace NNUE

// save merit function parameters to a file
void save_eval(std::string dir_name) {
  auto eval_dir = Path::Combine(Options["EvalSaveDir"], dir_name);
  std::cout << "save_eval() start. folder = " << eval_dir << std::endl;

  // mkdir() will fail if this folder already exists, but
  // Apart from that. If not, I just want you to make it.
  // Also, assume that the folders up to EvalSaveDir have been dug.
  Dependency::mkdir(eval_dir);

  if (Options["SkipLoadingEval"] && NNUE::trainer) {
    NNUE::SendMessages({{"clear_unobserved_feature_weights"}});
  }

  const std::string file_name = Path::Combine(eval_dir, NNUE::savedfileName);
  std::ofstream stream(file_name, std::ios::binary);
  const bool result = NNUE::WriteParameters(stream);
  assert(result);

  std::cout << "save_eval() finished. folder = " << eval_dir << std::endl;
}

// get the current eta
double get_eta() {
  return NNUE::GetGlobalLearningRateScale() * EvalLearningTools::Weight::eta;
}

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)
