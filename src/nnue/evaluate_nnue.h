// header used in NNUE evaluation function

#ifndef _EVALUATE_NNUE_H_
#define _EVALUATE_NNUE_H_

#if defined(EVAL_NNUE)

#include "nnue_feature_transformer.h"
#include "nnue_architecture.h"

#include <memory>

namespace Eval {

namespace NNUE {

// hash value of evaluation function structure
constexpr std::uint32_t kHashValue =
    FeatureTransformer::GetHashValue() ^ Network::GetHashValue();

// Deleter for automating release of memory area
template <typename T>
struct AlignedDeleter {
  void operator()(T* ptr) const {
    ptr->~T();
    aligned_free(ptr);
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

// Saved evaluation function file name
extern std::string savedfileName;

// Get a string that represents the structure of the evaluation function
std::string GetArchitectureString();

// read the header
bool ReadHeader(std::istream& stream,
    std::uint32_t* hash_value, std::string* architecture);

// write the header
bool WriteHeader(std::ostream& stream,
    std::uint32_t hash_value, const std::string& architecture);

// read evaluation function parameters
bool ReadParameters(std::istream& stream);

// write evaluation function parameters
bool WriteParameters(std::ostream& stream);

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
