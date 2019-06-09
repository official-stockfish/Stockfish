// NNUE評価関数の層Sumの定義

#ifndef _NNUE_LAYERS_SUM_H_
#define _NNUE_LAYERS_SUM_H_

#include "../../../config.h"

#if defined(EVAL_NNUE)

#include "../nnue_common.h"

namespace Eval {

namespace NNUE {

namespace Layers {

// 複数の層の出力の和を取る層
template <typename FirstPreviousLayer, typename... RemainingPreviousLayers>
class Sum : public Sum<RemainingPreviousLayers...> {
 private:
  using Head = FirstPreviousLayer;
  using Tail = Sum<RemainingPreviousLayers...>;

 public:
  // 入出力の型
  using InputType = typename Head::OutputType;
  using OutputType = InputType;
  static_assert(std::is_same<InputType, typename Tail::InputType>::value, "");

  // 入出力の次元数
  static constexpr IndexType kInputDimensions = Head::kOutputDimensions;
  static constexpr IndexType kOutputDimensions = kInputDimensions;
  static_assert(kInputDimensions == Tail::kInputDimensions , "");

  // この層で使用する順伝播用バッファのサイズ
  static constexpr std::size_t kSelfBufferSize =
      CeilToMultiple(kOutputDimensions * sizeof(OutputType), kCacheLineSize);

  // 入力層からこの層までで使用する順伝播用バッファのサイズ
  static constexpr std::size_t kBufferSize =
      std::max(Head::kBufferSize + kSelfBufferSize, Tail::kBufferSize);

  // 評価関数ファイルに埋め込むハッシュ値
  static constexpr std::uint32_t GetHashValue() {
    std::uint32_t hash_value = 0xBCE400B4u;
    hash_value ^= Head::GetHashValue() >> 1;
    hash_value ^= Head::GetHashValue() << 31;
    hash_value ^= Tail::GetHashValue() >> 2;
    hash_value ^= Tail::GetHashValue() << 30;
    return hash_value;
  }

  // 入力層からこの層までの構造を表す文字列
  static std::string GetStructureString() {
    return "Sum[" +
        std::to_string(kOutputDimensions) + "](" + GetSummandsString() + ")";
  }

  // パラメータを読み込む
  bool ReadParameters(std::istream& stream) {
    if (!Tail::ReadParameters(stream)) return false;
    return previous_layer_.ReadParameters(stream);
  }

  // パラメータを書き込む
  bool WriteParameters(std::ostream& stream) const {
    if (!Tail::WriteParameters(stream)) return false;
    return previous_layer_.WriteParameters(stream);
  }

  // 順伝播
  const OutputType* Propagate(
      const TransformedFeatureType* transformed_features, char* buffer) const {
    Tail::Propagate(transformed_features, buffer);
    const auto head_output = previous_layer_.Propagate(
        transformed_features, buffer + kSelfBufferSize);
    const auto output = reinterpret_cast<OutputType*>(buffer);
    for (IndexType i = 0; i < kOutputDimensions; ++i) {
      output[i] += head_output[i];
    }
    return output;
  }

 protected:
  // 和を取る対象となる層のリストを表す文字列
  static std::string GetSummandsString() {
    return Head::GetStructureString() + "," + Tail::GetSummandsString();
  }

  // 学習用クラスをfriendにする
  friend class Trainer<Sum>;

  // この層の直前の層
  FirstPreviousLayer previous_layer_;
};

// 複数の層の出力の和を取る層（テンプレート引数が1つの場合）
template <typename PreviousLayer>
class Sum<PreviousLayer> {
 public:
  // 入出力の型
  using InputType = typename PreviousLayer::OutputType;
  using OutputType = InputType;

  // 入出力の次元数
  static constexpr IndexType kInputDimensions =
      PreviousLayer::kOutputDimensions;
  static constexpr IndexType kOutputDimensions = kInputDimensions;

  // 入力層からこの層までで使用する順伝播用バッファのサイズ
  static constexpr std::size_t kBufferSize = PreviousLayer::kBufferSize;

  // 評価関数ファイルに埋め込むハッシュ値
  static constexpr std::uint32_t GetHashValue() {
    std::uint32_t hash_value = 0xBCE400B4u;
    hash_value ^= PreviousLayer::GetHashValue() >> 1;
    hash_value ^= PreviousLayer::GetHashValue() << 31;
    return hash_value;
  }

  // 入力層からこの層までの構造を表す文字列
  static std::string GetStructureString() {
    return "Sum[" +
        std::to_string(kOutputDimensions) + "](" + GetSummandsString() + ")";
  }

  // パラメータを読み込む
  bool ReadParameters(std::istream& stream) {
    return previous_layer_.ReadParameters(stream);
  }

  // パラメータを書き込む
  bool WriteParameters(std::ostream& stream) const {
    return previous_layer_.WriteParameters(stream);
  }

  // 順伝播
  const OutputType* Propagate(
      const TransformedFeatureType* transformed_features, char* buffer) const {
    return previous_layer_.Propagate(transformed_features, buffer);
  }

 protected:
  // 和を取る対象となる層のリストを表す文字列
  static std::string GetSummandsString() {
    return PreviousLayer::GetStructureString();
  }

  // 学習用クラスをfriendにする
  friend class Trainer<Sum>;

  // この層の直前の層
  PreviousLayer previous_layer_;
};

}  // namespace Layers

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
