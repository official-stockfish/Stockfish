// header used in NNUE evaluation function

#ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
#define NNUE_EVALUATE_NNUE_H_INCLUDED

#include "nnue_feature_transformer.h"

#include <memory>

namespace Eval::NNUE {

  // hash value of evaluation function structure
  constexpr std::uint32_t kHashValue =
      FeatureTransformer::GetHashValue() ^ Network::GetHashValue();

  // Deleter for automating release of memory area
  template <typename T>
  struct AlignedDeleter {
    void operator()(T* ptr) const {
      ptr->~T();
      std_aligned_free(ptr);
    }
  };

  template <typename T>
  using AlignedPtr = std::unique_ptr<T, AlignedDeleter<T>>;

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
