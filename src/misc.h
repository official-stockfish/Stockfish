/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

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

#include <cassert>
#include <chrono>
#include <ostream>
#include <string>
#include <vector>
#include <cstdint>

#include "types.h"

namespace Stockfish {

std::string engine_info(bool to_uci = false);
std::string compiler_info();
void prefetch(void* addr);
void start_logger(const std::string& fname);
void* std_aligned_alloc(size_t alignment, size_t size);
void std_aligned_free(void* ptr);
void* aligned_large_pages_alloc(size_t size); // memory aligned by page size, min alignment: 4096 bytes
void aligned_large_pages_free(void* mem); // nop if mem == nullptr

void dbg_hit_on(bool b);
void dbg_hit_on(bool c, bool b);
void dbg_mean_of(int v);
void dbg_print();

typedef std::chrono::milliseconds::rep TimePoint; // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(int64_t), "TimePoint should be 64 bits");
inline TimePoint now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>
        (std::chrono::steady_clock::now().time_since_epoch()).count();
}

template<class Entry, int Size>
struct HashTable {
  Entry* operator[](Key key) { return &table[(uint32_t)key & (Size - 1)]; }

private:
  std::vector<Entry> table = std::vector<Entry>(Size); // Allocate on the heap
};


enum SyncCout { IO_LOCK, IO_UNLOCK };
std::ostream& operator<<(std::ostream&, SyncCout);

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK


// align_ptr_up() : get the first aligned element of an array.
// ptr must point to an array of size at least `sizeof(T) * N + alignment` bytes,
// where N is the number of elements in the array.
template <uintptr_t Alignment, typename T>
T* align_ptr_up(T* ptr)
{
  static_assert(alignof(T) < Alignment);

  const uintptr_t ptrint = reinterpret_cast<uintptr_t>(reinterpret_cast<char*>(ptr));
  return reinterpret_cast<T*>(reinterpret_cast<char*>((ptrint + (Alignment - 1)) / Alignment * Alignment));
}


// IsLittleEndian : true if and only if the binary is compiled on a little endian machine
static inline const union { uint32_t i; char c[4]; } Le = { 0x01020304 };
static inline const bool IsLittleEndian = (Le.c[0] == 4);


// RunningAverage : a class to calculate a running average of a series of values.
// For efficiency, all computations are done with integers.
class RunningAverage {
  public:

      // Reset the running average to rational value p / q
      void set(int64_t p, int64_t q)
        { average = p * PERIOD * RESOLUTION / q; }

      // Update average with value v
      void update(int64_t v)
        { average = RESOLUTION * v + (PERIOD - 1) * average / PERIOD; }

      // Test if average is strictly greater than rational a / b
      bool is_greater(int64_t a, int64_t b) const
        { return b * average > a * (PERIOD * RESOLUTION); }

      int64_t value() const
        { return average / (PERIOD * RESOLUTION); }

  private :
      static constexpr int64_t PERIOD     = 4096;
      static constexpr int64_t RESOLUTION = 1024;
      int64_t average;
};

template <typename T, std::size_t MaxSize>
class ValueList {

public:
  std::size_t size() const { return size_; }
  void push_back(const T& value) { values_[size_++] = value; }
  const T* begin() const { return values_; }
  const T* end() const { return values_ + size_; }

private:
  T values_[MaxSize];
  std::size_t size_ = 0;
};


/// xorshift64star Pseudo-Random Number Generator
/// This class is based on original code written and dedicated
/// to the public domain by Sebastiano Vigna (2014).
/// It has the following characteristics:
///
///  -  Outputs 64-bit numbers
///  -  Passes Dieharder and SmallCrush test batteries
///  -  Does not require warm-up, no zeroland to escape
///  -  Internal state is a single 64-bit integer
///  -  Period is 2^64 - 1
///  -  Speed: 1.60 ns/call (Core i7 @3.40GHz)
///
/// For further analysis see
///   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>

class PRNG {

  uint64_t s;

  uint64_t rand64() {

    s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
    return s * 2685821657736338717LL;
  }

public:
  PRNG(uint64_t seed) : s(seed) { assert(seed); }

  template<typename T> T rand() { return T(rand64()); }

  /// Special generator used to fast init magic numbers.
  /// Output values only have 1/8th of their bits set on average.
  template<typename T> T sparse_rand()
  { return T(rand64() & rand64() & rand64()); }
};

inline uint64_t mul_hi64(uint64_t a, uint64_t b) {
#if defined(__GNUC__) && defined(IS_64BIT)
    __extension__ typedef unsigned __int128 uint128;
    return ((uint128)a * (uint128)b) >> 64;
#else
    uint64_t aL = (uint32_t)a, aH = a >> 32;
    uint64_t bL = (uint32_t)b, bH = b >> 32;
    uint64_t c1 = (aL * bL) >> 32;
    uint64_t c2 = aH * bL + c1;
    uint64_t c3 = aL * bH + (uint32_t)c2;
    return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif
}

/// Under Windows it is not possible for a process to run on more than one
/// logical processor group. This usually means to be limited to use max 64
/// cores. To overcome this, some special platform specific API should be
/// called to set group affinity for each thread. Original code from Texel by
/// Peter Österlund.

namespace WinProcGroup {
  void bindThisThread(size_t idx);
}

namespace CommandLine {
  void init(int argc, char* argv[]);

  extern std::string binaryDirectory;  // path of the executable directory
  extern std::string workingDirectory; // path of the working directory
}

} // namespace Stockfish

#endif // #ifndef MISC_H_INCLUDED
