// NNUE評価関数の学習用クラステンプレートの共通ヘッダ

#ifndef _NNUE_TRAINER_H_
#define _NNUE_TRAINER_H_

#include "../../../config.h"

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

// 評価値と勝率の関係式で用いるPonanza定数
constexpr double kPonanzaConstant = 600.0;

// 学習用特徴量のインデックス1つを表すクラス
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
    ASSERT_LV3(index < (1 << kIndexBits));
  }
  TrainingFeature& operator+=(const TrainingFeature& other) {
    ASSERT_LV3(other.GetIndex() == GetIndex());
    ASSERT_LV3(other.GetCount() + GetCount() < (1 << kCountBits));
    index_and_count_ += other.GetCount();
    return *this;
  }
  IndexType GetIndex() const {
    return static_cast<IndexType>(index_and_count_ >> kCountBits);
  }
  void ShiftIndex(IndexType offset) {
    ASSERT_LV3(GetIndex() + offset < (1 << kIndexBits));
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

// 学習データ1サンプルを表す構造体
struct Example {
  std::vector<TrainingFeature> training_features[2];
  Learner::PackedSfenValue psv;
  int sign;
  double weight;
};

// ハイパーパラメータの設定などに使用するメッセージ
struct Message {
  Message(const std::string& name, const std::string& value = "") :
      name(name), value(value), num_peekers(0), num_receivers(0) {}
  const std::string name;
  const std::string value;
  std::uint32_t num_peekers;
  std::uint32_t num_receivers;
};

// メッセージを受理するかどうかを判定する
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

// 文字列を分割する
std::vector<std::string> Split(const std::string& input, char delimiter) {
  std::istringstream stream(input);
  std::string field;
  std::vector<std::string> fields;
  while (std::getline(stream, field, delimiter)) {
    fields.push_back(field);
  }
  return fields;
}

// 浮動小数点数を整数に丸める
template <typename IntType>
IntType Round(double value) {
  return static_cast<IntType>(std::floor(value + 0.5));
}

// アライメント付きmake_shared
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
