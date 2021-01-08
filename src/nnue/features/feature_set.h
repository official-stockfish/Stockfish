/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

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

// A class template that represents the input feature set of the NNUE evaluation function

#ifndef NNUE_FEATURE_SET_H_INCLUDED
#define NNUE_FEATURE_SET_H_INCLUDED

#include "features_common.h"
#include <array>

namespace Eval::NNUE::Features {

  // Class template that represents a list of values
  template <typename T, T... Values>
  struct CompileTimeList;

  template <typename T, T First, T... Remaining>
  struct CompileTimeList<T, First, Remaining...> {
    static constexpr bool Contains(T value) {
      return value == First || CompileTimeList<T, Remaining...>::Contains(value);
    }
    static constexpr std::array<T, sizeof...(Remaining) + 1>
        kValues = {{First, Remaining...}};
  };

  // Base class of feature set
  template <typename Derived>
  class FeatureSetBase {

  };

  // Class template that represents the feature set
  template <typename FeatureType>
  class FeatureSet<FeatureType> : public FeatureSetBase<FeatureSet<FeatureType>> {

   public:
    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t kHashValue = FeatureType::kHashValue;
    // Number of feature dimensions
    static constexpr IndexType kDimensions = FeatureType::kDimensions;
    // Maximum number of simultaneously active features
    static constexpr IndexType kMaxActiveDimensions =
        FeatureType::kMaxActiveDimensions;
    // Trigger for full calculation instead of difference calculation
    using SortedTriggerSet =
        CompileTimeList<TriggerEvent, FeatureType::kRefreshTrigger>;
    static constexpr auto kRefreshTriggers = SortedTriggerSet::kValues;

  };

}  // namespace Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURE_SET_H_INCLUDED
