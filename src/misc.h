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

#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#define stringify2(x) #x
#define stringify(x) stringify2(x)

namespace Stockfish {

std::string engine_info(bool to_uci = false);
std::string compiler_info();

// Preloads the given address in L1/L2 cache. This is a non-blocking
// function that doesn't stall the CPU waiting for data to be loaded from memory,
// which can be quite slow.
void prefetch(const void* addr);

void start_logger(const std::string& fname);

size_t str_to_size_t(const std::string& s);

#if defined(__linux__)

struct PipeDeleter {
    void operator()(FILE* file) const {
        if (file != nullptr)
        {
            pclose(file);
        }
    }
};

#endif

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(const std::string& path);

void dbg_hit_on(bool cond, int slot = 0);
void dbg_mean_of(int64_t value, int slot = 0);
void dbg_stdev_of(int64_t value, int slot = 0);
void dbg_extremes_of(int64_t value, int slot = 0);
void dbg_correl_of(int64_t value1, int64_t value2, int slot = 0);
void dbg_print();

using TimePoint = std::chrono::milliseconds::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(int64_t), "TimePoint should be 64 bits");
inline TimePoint now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline std::vector<std::string> split(const std::string& s, const std::string& delimiter) {
    std::vector<std::string> res;

    if (s.empty())
        return res;

    size_t begin = 0;
    for (;;)
    {
        const size_t end = s.find(delimiter, begin);
        if (end == std::string::npos)
            break;

        res.emplace_back(s.substr(begin, end - begin));
        begin = end + delimiter.size();
    }

    res.emplace_back(s.substr(begin));

    return res;
}

void remove_whitespace(std::string& s);
bool is_whitespace(const std::string& s);

enum SyncCout {
    IO_LOCK,
    IO_UNLOCK
};
std::ostream& operator<<(std::ostream&, SyncCout);

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK

void sync_cout_start();
void sync_cout_end();

// True if and only if the binary is compiled on a little-endian machine
static inline const union {
    uint32_t i;
    char     c[4];
} Le                                    = {0x01020304};
static inline const bool IsLittleEndian = (Le.c[0] == 4);


template<typename T, std::size_t MaxSize>
class ValueList {

   public:
    std::size_t size() const { return size_; }
    void        push_back(const T& value) { values_[size_++] = value; }
    const T*    begin() const { return values_; }
    const T*    end() const { return values_ + size_; }
    const T&    operator[](int index) const { return values_[index]; }

   private:
    T           values_[MaxSize];
    std::size_t size_ = 0;
};


// xorshift64star Pseudo-Random Number Generator
// This class is based on original code written and dedicated
// to the public domain by Sebastiano Vigna (2014).
// It has the following characteristics:
//
//  -  Outputs 64-bit numbers
//  -  Passes Dieharder and SmallCrush test batteries
//  -  Does not require warm-up, no zeroland to escape
//  -  Internal state is a single 64-bit integer
//  -  Period is 2^64 - 1
//  -  Speed: 1.60 ns/call (Core i7 @3.40GHz)
//
// For further analysis see
//   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>

class PRNG {

    uint64_t s;

    uint64_t rand64() {

        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return s * 2685821657736338717LL;
    }

   public:
    PRNG(uint64_t seed) :
        s(seed) {
        assert(seed);
    }

    template<typename T>
    T rand() {
        return T(rand64());
    }

    // Special generator used to fast init magic numbers.
    // Output values only have 1/8th of their bits set on average.
    template<typename T>
    T sparse_rand() {
        return T(rand64() & rand64() & rand64());
    }
};

inline uint64_t mul_hi64(uint64_t a, uint64_t b) {
#if defined(__GNUC__) && defined(IS_64BIT)
    __extension__ using uint128 = unsigned __int128;
    return (uint128(a) * uint128(b)) >> 64;
#else
    uint64_t aL = uint32_t(a), aH = a >> 32;
    uint64_t bL = uint32_t(b), bH = b >> 32;
    uint64_t c1 = (aL * bL) >> 32;
    uint64_t c2 = aH * bL + c1;
    uint64_t c3 = aL * bH + uint32_t(c2);
    return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif
}


struct CommandLine {
   public:
    CommandLine(int _argc, char** _argv) :
        argc(_argc),
        argv(_argv) {}

    static std::string get_binary_directory(std::string argv0);
    static std::string get_working_directory();

    int    argc;
    char** argv;
};

namespace Utility {

template<typename T, typename Predicate>
void move_to_front(std::vector<T>& vec, Predicate pred) {
    auto it = std::find_if(vec.begin(), vec.end(), pred);

    if (it != vec.end())
    {
        std::rotate(vec.begin(), it, it + 1);
    }
}
}

}  // namespace Stockfish

#endif  // #ifndef MISC_H_INCLUDED
