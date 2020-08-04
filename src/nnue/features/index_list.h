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

// Definition of index list of input features

#ifndef NNUE_FEATURES_INDEX_LIST_H_INCLUDED
#define NNUE_FEATURES_INDEX_LIST_H_INCLUDED

#include "../../position.h"
#include "../nnue_architecture.h"

namespace Eval::NNUE::Features {

  // Class template used for feature index list
  template <typename T, std::size_t MaxSize>
  class ValueList {

   public:
    std::size_t size() const { return size_; }
    void resize(std::size_t size) { size_ = size; }
    void push_back(const T& value) { values_[size_++] = value; }
    T& operator[](std::size_t index) { return values_[index]; }
    T* begin() { return values_; }
    T* end() { return values_ + size_; }
    const T& operator[](std::size_t index) const { return values_[index]; }
    const T* begin() const { return values_; }
    const T* end() const { return values_ + size_; }

    void swap(ValueList& other) {
      const std::size_t max_size = std::max(size_, other.size_);
      for (std::size_t i = 0; i < max_size; ++i) {
        std::swap(values_[i], other.values_[i]);
      }
      std::swap(size_, other.size_);
    }

   private:
    T values_[MaxSize];
    std::size_t size_ = 0;
  };

  //Type of feature index list
  class IndexList
      : public ValueList<IndexType, RawFeatures::kMaxActiveDimensions> {
  };

}  // namespace Eval::NNUE::Features

#endif // NNUE_FEATURES_INDEX_LIST_H_INCLUDED
