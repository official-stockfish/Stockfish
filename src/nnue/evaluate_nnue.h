/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "../misc.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_feature_transformer.h"

namespace Stockfish {
class Position;

namespace Eval {
struct EvalFile;
}

}

namespace Stockfish::Eval::NNUE {

// Hash value of evaluation function structure
constexpr std::uint32_t HashValue[2] = {
  FeatureTransformer<TransformedFeatureDimensionsBig, nullptr>::get_hash_value()
    ^ Network<TransformedFeatureDimensionsBig, L2Big, L3Big>::get_hash_value(),
  FeatureTransformer<TransformedFeatureDimensionsSmall, nullptr>::get_hash_value()
    ^ Network<TransformedFeatureDimensionsSmall, L2Small, L3Small>::get_hash_value()};

// Deleter for automating release of memory area
template<typename T>
struct AlignedDeleter {
    void operator()(T* ptr) const {
        ptr->~T();
        std_aligned_free(ptr);
    }
};

template<typename T>
struct LargePageDeleter {
    void operator()(T* ptr) const {
        ptr->~T();
        aligned_large_pages_free(ptr);
    }
};

template<typename T>
using AlignedPtr = std::unique_ptr<T, AlignedDeleter<T>>;

template<typename T>
using LargePagePtr = std::unique_ptr<T, LargePageDeleter<T>>;

std::string trace(Position& pos);
template<NetSize Net_Size>
Value evaluate(const Position& pos, bool adjusted = false, int* complexity = nullptr);
void  hint_common_parent_position(const Position& pos);

std::optional<std::string> load_eval(std::istream& stream, NetSize netSize);
bool                       save_eval(std::ostream&      stream,
                                     NetSize            netSize,
                                     const std::string& name,
                                     const std::string& netDescription);
bool                       save_eval(const std::optional<std::string>& filename,
                                     NetSize                           netSize,
                                     const std::unordered_map<Eval::NNUE::NetSize, Eval::EvalFile>&);

}  // namespace Stockfish::Eval::NNUE

#endif  // #ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
