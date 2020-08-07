// Common header of class template for learning NNUE evaluation function

#ifndef _NNUE_TRAINER_H_
#define _NNUE_TRAINER_H_

#if defined(EVAL_LEARN) && defined(EVAL_NNUE)

#include "../nnue_common.h"
#include "../features/index_list.h"

#include <sstream>
#if defined(USE_BLAS)
static_assert(std::is_same<LearnFloatType, float>::value, "");
#include <cblas.h>
#endif

namespace Eval {

namespace NNUE {

// Ponanza constant used in the relation between evaluation value and winning percentage
constexpr double kPonanzaConstant = 600.0;

// Class that represents one index of learning feature
class TrainingFeature {
  using StorageType = std::uint32_t;
  static_assert(std::is_unsigned<StorageType>::value, "");

 public:
  static constexpr std::uint32_t kIndexBits = 24;
  static_assert(kIndexBits < std::numeric_limits<StorageType>::digits, "");
  static constexpr std::uint32_t kCountBits =
      std::numeric_limits<StorageType>::digits - kIndexBits;

  explicit TrainingFeature(IndexType index) :
      index_and_count_((index << kCountBits) | 1) {
    assert(index < (1 << kIndexBits));
  }
  TrainingFeature& operator+=(const TrainingFeature& other) {
    assert(other.GetIndex() == GetIndex());
    assert(other.GetCount() + GetCount() < (1 << kCountBits));
    index_and_count_ += other.GetCount();
    return *this;
  }
  IndexType GetIndex() const {
    return static_cast<IndexType>(index_and_count_ >> kCountBits);
  }
  void ShiftIndex(IndexType offset) {
    assert(GetIndex() + offset < (1 << kIndexBits));
    index_and_count_ += offset << kCountBits;
  }
  IndexType GetCount() const {
    return static_cast<IndexType>(index_and_count_ & ((1 << kCountBits) - 1));
  }
  bool operator<(const TrainingFeature& other) const {
    return index_and_count_ < other.index_and_count_;
  }

 private:
  StorageType index_and_count_;
};

// Structure that represents one sample of training data
struct Example {
  std::vector<TrainingFeature> training_features[2];
  Learner::PackedSfenValue psv;
  int sign;
  double weight;
};

// Message used for setting hyperparameters
struct Message {
  Message(const std::string& name, const std::string& value = ""):
      name(name), value(value), num_peekers(0), num_receivers(0) {}
  const std::string name;
  const std::string value;
  std::uint32_t num_peekers;
  std::uint32_t num_receivers;
};

// determine whether to accept the message
bool ReceiveMessage(const std::string& name, Message* message) {
  const auto subscript = "[" + std::to_string(message->num_peekers) + "]";
  if (message->name.substr(0, name.size() + 1) == name + "[") {
    ++message->num_peekers;
  }
  if (message->name == name || message->name == name + subscript) {
    ++message->num_receivers;
    return true;
  }
  return false;
}

// split the string
std::vector<std::string> Split(const std::string& input, char delimiter) {
  std::istringstream stream(input);
  std::string field;
  std::vector<std::string> fields;
  while (std::getline(stream, field, delimiter)) {
    fields.push_back(field);
  }
  return fields;
}

// round a floating point number to an integer
template <typename IntType>
IntType Round(double value) {
  return static_cast<IntType>(std::floor(value + 0.5));
}

// make_shared with alignment
template <typename T, typename... ArgumentTypes>
std::shared_ptr<T> MakeAlignedSharedPtr(ArgumentTypes&&... arguments) {
  const auto ptr = new(aligned_malloc(sizeof(T), alignof(T)))
      T(std::forward<ArgumentTypes>(arguments)...);
  return std::shared_ptr<T>(ptr, AlignedDeleter<T>());
}

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_LEARN) && defined(EVAL_NNUE)

#endif
