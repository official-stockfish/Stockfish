// header used in NNUE evaluation function

#ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
#define NNUE_EVALUATE_NNUE_H_INCLUDED

#include "nnue_feature_transformer.h"
#include "nnue_architecture.h"
#include "../misc.h"

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
      std::free(ptr);
    }
  };

  template <typename T>
  using AlignedPtr = std::unique_ptr<T, AlignedDeleter<T>>;

  // Input feature converter
  extern AlignedPtr<FeatureTransformer> feature_transformer;

  // Evaluation function
  extern AlignedPtr<Network> network;

  // Evaluation function file name
  extern std::string fileName;

  // Get a string that represents the structure of the evaluation function
  std::string GetArchitectureString();

  // read the header
  bool ReadHeader(std::istream& stream,
      std::uint32_t* hash_value, std::string* architecture);

  // read evaluation function parameters
  bool ReadParameters(std::istream& stream);

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
