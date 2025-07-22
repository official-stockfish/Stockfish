/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

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

#ifndef NNUE_MISC_H_INCLUDED
#define NNUE_MISC_H_INCLUDED

#include <cstddef>
#include <string>

#include "../types.h"
#include "nnue_architecture.h"

// chatgpt xdd
template <std::size_t Capacity>
class FixedString {
public:
    FixedString() : length_(0) {
        data_[0] = '\0';
    }

    FixedString(const char* str) {
        size_t len = std::strlen(str);
        if (len > Capacity)
            std::terminate();
        std::memcpy(data_, str, len);
        length_ = len;
        data_[length_] = '\0';
    }

    FixedString(const std::string& str) {
        if (str.size() > Capacity)
            std::terminate();
        std::memcpy(data_, str.data(), str.size());
        length_ = str.size();
        data_[length_] = '\0';
    }

    std::size_t size() const { return length_; }
    std::size_t capacity() const { return Capacity; }

    const char* c_str() const { return data_; }
    const char* data() const { return data_; }

    char& operator[](std::size_t i) {
        if (i >= length_)
            std::terminate();
        return data_[i];
    }

    const char& operator[](std::size_t i) const {
        if (i >= length_)
            std::terminate();
        return data_[i];
    }

    FixedString& operator+=(const char* str) {
        size_t len = std::strlen(str);
        if (length_ + len > Capacity)
            std::terminate();
        std::memcpy(data_ + length_, str, len);
        length_ += len;
        data_[length_] = '\0';
        return *this;
    }

    FixedString& operator+=(const FixedString& other) {
        return (*this += other.c_str());
    }

    operator std::string() const {
      return std::string(data_, length_);
    }

    void clear() {
        length_ = 0;
        data_[0] = '\0';
    }

private:
    char data_[Capacity + 1]; // +1 for null terminator
    std::size_t length_;
};

namespace Stockfish {

class Position;

namespace Eval::NNUE {

struct EvalFile {
    // Default net name, will use one of the EvalFileDefaultName* macros defined
    // in evaluate.h
    FixedString<256> defaultName;
    // Selected net name, either via uci option or default
    FixedString<256> current;
    // Net description extracted from the net file
    FixedString<256> netDescription;
};


struct NnueEvalTrace {
    static_assert(LayerStacks == PSQTBuckets);

    Value       psqt[LayerStacks];
    Value       positional[LayerStacks];
    std::size_t correctBucket;
};

struct Networks;
struct AccumulatorCaches;

std::string trace(Position& pos, const Networks& networks, AccumulatorCaches& caches);

}  // namespace Stockfish::Eval::NNUE
}  // namespace Stockfish

#endif  // #ifndef NNUE_MISC_H_INCLUDED
