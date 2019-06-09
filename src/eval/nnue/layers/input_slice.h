// NNUE評価関数の層InputSliceの定義

#ifndef _NNUE_LAYERS_INPUT_SLICE_H_
#define _NNUE_LAYERS_INPUT_SLICE_H_

#if defined(EVAL_NNUE)

#include "../nnue_common.h"

namespace Eval {

namespace NNUE {

namespace Layers {

// 入力層
template <IndexType OutputDimensions, IndexType Offset = 0>
class InputSlice {
 public:
  // アライメントを維持する必要がある
  static_assert(Offset % kMaxSimdWidth == 0, "");

  // 出力の型
  using OutputType = TransformedFeatureType;

  // 出力の次元数
  static constexpr IndexType kOutputDimensions = OutputDimensions;

  // 入力層からこの層までで使用する順伝播用バッファのサイズ
  static constexpr std::size_t kBufferSize = 0;

  // 評価関数ファイルに埋め込むハッシュ値
  static constexpr std::uint32_t GetHashValue() {
    std::uint32_t hash_value = 0xEC42E90Du;
    hash_value ^= kOutputDimensions ^ (Offset << 10);
    return hash_value;
  }

  // 入力層からこの層までの構造を表す文字列
  static std::string GetStructureString() {
    return "InputSlice[" + std::to_string(kOutputDimensions) + "(" +
        std::to_string(Offset) + ":" +
        std::to_string(Offset + kOutputDimensions) + ")]";
  }

  // パラメータを読み込む
  bool ReadParameters(std::istream& /*stream*/) {
    return true;
  }

  // パラメータを書き込む
  bool WriteParameters(std::ostream& /*stream*/) const {
    return true;
  }

  // 順伝播
  const OutputType* Propagate(
      const TransformedFeatureType* transformed_features,
      char* /*buffer*/) const {
    return transformed_features + Offset;
  }

 private:
};

}  // namespace Layers

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
