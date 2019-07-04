// NNUE評価関数の層AffineTransformの定義

#ifndef _NNUE_LAYERS_AFFINE_TRANSFORM_H_
#define _NNUE_LAYERS_AFFINE_TRANSFORM_H_

#if defined(EVAL_NNUE)

#include "../nnue_common.h"

namespace Eval {

namespace NNUE {

namespace Layers {

// アフィン変換層
template <typename PreviousLayer, IndexType OutputDimensions>
class AffineTransform {
 public:
  // 入出力の型
  using InputType = typename PreviousLayer::OutputType;
  using OutputType = std::int32_t;
  static_assert(std::is_same<InputType, std::uint8_t>::value, "");

  // 入出力の次元数
  static constexpr IndexType kInputDimensions =
      PreviousLayer::kOutputDimensions;
  static constexpr IndexType kOutputDimensions = OutputDimensions;
  static constexpr IndexType kPaddedInputDimensions =
      CeilToMultiple<IndexType>(kInputDimensions, kMaxSimdWidth);

  // この層で使用する順伝播用バッファのサイズ
  static constexpr std::size_t kSelfBufferSize =
      CeilToMultiple(kOutputDimensions * sizeof(OutputType), kCacheLineSize);

  // 入力層からこの層までで使用する順伝播用バッファのサイズ
  static constexpr std::size_t kBufferSize =
      PreviousLayer::kBufferSize + kSelfBufferSize;

  // 評価関数ファイルに埋め込むハッシュ値
  static constexpr std::uint32_t GetHashValue() {
    std::uint32_t hash_value = 0xCC03DAE4u;
    hash_value += kOutputDimensions;
    hash_value ^= PreviousLayer::GetHashValue() >> 1;
    hash_value ^= PreviousLayer::GetHashValue() << 31;
    return hash_value;
  }

  // 入力層からこの層までの構造を表す文字列
  static std::string GetStructureString() {
    return "AffineTransform[" +
        std::to_string(kOutputDimensions) + "<-" +
        std::to_string(kInputDimensions) + "](" +
        PreviousLayer::GetStructureString() + ")";
  }

  // パラメータを読み込む
  bool ReadParameters(std::istream& stream) {
    if (!previous_layer_.ReadParameters(stream)) return false;
    stream.read(reinterpret_cast<char*>(biases_),
                kOutputDimensions * sizeof(BiasType));
    stream.read(reinterpret_cast<char*>(weights_),
                kOutputDimensions * kPaddedInputDimensions *
                sizeof(WeightType));
    return !stream.fail();
  }

  // パラメータを書き込む
  bool WriteParameters(std::ostream& stream) const {
    if (!previous_layer_.WriteParameters(stream)) return false;
    stream.write(reinterpret_cast<const char*>(biases_),
                 kOutputDimensions * sizeof(BiasType));
    stream.write(reinterpret_cast<const char*>(weights_),
                 kOutputDimensions * kPaddedInputDimensions *
                 sizeof(WeightType));
    return !stream.fail();
  }

  // 順伝播
  const OutputType* Propagate(
      const TransformedFeatureType* transformed_features, char* buffer) const {
    const auto input = previous_layer_.Propagate(
        transformed_features, buffer + kSelfBufferSize);
    const auto output = reinterpret_cast<OutputType*>(buffer);
#if defined(USE_AVX2)
    constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
    const __m256i kOnes = _mm256_set1_epi16(1);
    const auto input_vector = reinterpret_cast<const __m256i*>(input);
#elif defined(USE_SSE41)
    constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
    const __m128i kOnes = _mm_set1_epi16(1);
    const auto input_vector = reinterpret_cast<const __m128i*>(input);
#elif defined(IS_ARM)
    constexpr IndexType kNumChunks = kPaddedInputDimensions / kSimdWidth;
    const auto input_vector = reinterpret_cast<const int8x8_t*>(input);
#endif
    for (IndexType i = 0; i < kOutputDimensions; ++i) {
      const IndexType offset = i * kPaddedInputDimensions;
#if defined(USE_AVX2)
      __m256i sum = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, biases_[i]);
      const auto row = reinterpret_cast<const __m256i*>(&weights_[offset]);
      for (IndexType j = 0; j < kNumChunks; ++j) {
        __m256i product = _mm256_maddubs_epi16(
#if defined(__MINGW32__) || defined(__MINGW64__)
          // HACK: Use _mm256_loadu_si256() instead of _mm256_load_si256. Because the binary
          //       compiled with g++ in MSYS2 crashes here because the output memory is not aligned
          //       even though alignas is specified.
          _mm256_loadu_si256
#else
          _mm256_load_si256
#endif
          (&input_vector[j]), _mm256_load_si256(&row[j]));
        product = _mm256_madd_epi16(product, kOnes);
        sum = _mm256_add_epi32(sum, product);
      }
      sum = _mm256_hadd_epi32(sum, sum);
      sum = _mm256_hadd_epi32(sum, sum);
      const __m128i lo = _mm256_extracti128_si256(sum, 0);
      const __m128i hi = _mm256_extracti128_si256(sum, 1);
      output[i] = _mm_cvtsi128_si32(lo) + _mm_cvtsi128_si32(hi);
#elif defined(USE_SSE41)
      __m128i sum = _mm_cvtsi32_si128(biases_[i]);
      const auto row = reinterpret_cast<const __m128i*>(&weights_[offset]);
      for (IndexType j = 0; j < kNumChunks; ++j) {
        __m128i product = _mm_maddubs_epi16(
            _mm_load_si128(&input_vector[j]), _mm_load_si128(&row[j]));
        product = _mm_madd_epi16(product, kOnes);
        sum = _mm_add_epi32(sum, product);
      }
      sum = _mm_hadd_epi32(sum, sum);
      sum = _mm_hadd_epi32(sum, sum);
      output[i] = _mm_cvtsi128_si32(sum);
#elif defined(IS_ARM)
      int32x4_t sum = {biases_[i]};
      const auto row = reinterpret_cast<const int8x8_t*>(&weights_[offset]);
      for (IndexType j = 0; j < kNumChunks; ++j) {
        int16x8_t product = vmull_s8(input_vector[j * 2], row[j * 2]);
        product = vmlal_s8(product, input_vector[j * 2 + 1], row[j * 2 + 1]);
        sum = vpadalq_s16(sum, product);
      }
      output[i] = sum[0] + sum[1] + sum[2] + sum[3];
#else
      OutputType sum = biases_[i];
      for (IndexType j = 0; j < kInputDimensions; ++j) {
        sum += weights_[offset + j] * input[j];
      }
      output[i] = sum;
#endif
    }
    return output;
  }

 private:
  // パラメータの型
  using BiasType = OutputType;
  using WeightType = std::int8_t;

  // 学習用クラスをfriendにする
  friend class Trainer<AffineTransform>;

  // この層の直前の層
  PreviousLayer previous_layer_;

  // パラメータ
  alignas(kCacheLineSize) BiasType biases_[kOutputDimensions];
  alignas(kCacheLineSize)
      WeightType weights_[kOutputDimensions * kPaddedInputDimensions];
};

}  // namespace Layers

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
