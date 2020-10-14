/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// header used in NNUE evaluation function

#ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
#define NNUE_EVALUATE_NNUE_H_INCLUDED

#include "nnue_feature_transformer.h"

#include <memory>

namespace Eval::NNUE {

  enum struct UseNNUEMode
  {
    False,
    True,
    Pure
  };

  // Hash value of evaluation function structure
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
  struct LargePageDeleter {
    void operator()(T* ptr) const {
      ptr->~T();
      aligned_large_pages_free(ptr);
    }
  };

  template <typename T>
  using AlignedPtr = std::unique_ptr<T, AlignedDeleter<T>>;

  template <typename T>
  using LargePagePtr = std::unique_ptr<T, LargePageDeleter<T>>;

  // Input feature converter
  extern LargePagePtr<FeatureTransformer> feature_transformer;

  // Evaluation function
  extern AlignedPtr<Network> network;

  // Evaluation function file name
  extern std::string fileName;

  // Saved evaluation function file name
  extern std::string savedfileName;

  extern UseNNUEMode useNNUE;
  extern std::string eval_file_loaded;

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

  Value evaluate(const Position& pos);
  bool load_eval(std::string name, std::istream& stream);
  void init();
  void verify();

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
