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

#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>
#include <iostream>

#include <cstdint>
#include <cmath>
#include <cctype>
#include <sstream>
#include <deque>

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

/// Debug macro to write to std::err if NDEBUG flag is set, and do nothing otherwise
#if defined(NDEBUG)
#define debug 1 && std::cerr
#else
#define debug 0 && std::cerr
#endif

inline void hit_any_key() {
#ifndef NDEBUG
  debug << "Hit any key to continue..." << std::endl << std::flush;
  system("read");   // on Windows, should be system("pause");
#endif
}

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
  void resize(std::size_t newSize) { size_ = newSize; }
  void push_back(const T& value) { values_[size_++] = value; }
  T& operator[](std::size_t index) { return values_[index]; }
  T* begin() { return values_; }
  T* end() { return values_ + size_; }
  const T& operator[](std::size_t index) const { return values_[index]; }
  const T* begin() const { return values_; }
  const T* end() const { return values_ + size_; }

  void swap(ValueList& other) {
    const std::size_t maxSize = std::max(size_, other.size_);
    for (std::size_t i = 0; i < maxSize; ++i) {
      std::swap(values_[i], other.values_[i]);
    }
    std::swap(size_, other.size_);
  }

private:
  T values_[MaxSize];
  std::size_t size_ = 0;
};

// This logger allows printing many parts in a region atomically
// but doesn't block the threads trying to append to other regions.
// Instead if some region tries to pring while other region holds
// the lock the messages are queued to be printed as soon as the
// current region releases the lock.
struct SynchronizedRegionLogger
{
  using RegionId = std::uint64_t;

  struct Region
  {
    friend struct SynchronizedRegionLogger;

    Region() :
      logger(nullptr), region_id(0), is_held(false)
    {
    }

    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;

    Region(Region&& other) :
      logger(other.logger), region_id(other.region_id), is_held(other.is_held)
    {
      other.logger = nullptr;
      other.is_held = false;
    }

    Region& operator=(Region&& other) {
      if (is_held && logger != nullptr)
      {
        logger->release_region(region_id);
      }

      logger = other.logger;
      region_id = other.region_id;
      is_held = other.is_held;

      other.is_held = false;

      return *this;
    }

    ~Region() { unlock(); }

    void unlock() {
      if (is_held) {
        is_held = false;

        if (logger != nullptr)
          logger->release_region(region_id);
      }
    }

    Region& operator << (std::ostream&(*pManip)(std::ostream&)) {
      if (logger != nullptr)
        logger->write(region_id, pManip);

      return *this;
    }

    template <typename T>
    Region& operator << (const T& value) {
      if (logger != nullptr)
        logger->write(region_id, value);

      return *this;
    }

  private:
    SynchronizedRegionLogger* logger;
    RegionId region_id;
    bool is_held;

    Region(SynchronizedRegionLogger& log, RegionId id) :
      logger(&log), region_id(id), is_held(true)
    {
    }
  };

private:
  struct RegionBookkeeping
  {
    RegionBookkeeping(RegionId rid) : id(rid), is_held(true) {}

    std::vector<std::string> pending_parts;
    RegionId id;
    bool is_held;
  };

  RegionId init_next_region()
  {
    static RegionId next_id = 0;

    std::lock_guard lock(mutex);

    const auto id = next_id++;
    regions.emplace_back(id);

    return id;
  }

  void write(RegionId id, std::ostream&(*pManip)(std::ostream&)) {
    std::lock_guard lock(mutex);

    if (regions.empty())
      return;

    if (id == regions.front().id) {
      // We can just directly print to the output because
      // we are at the front of the region queue.
      out << *pManip;
    } else {
      // We have to schedule the print until previous regions are
      // processed
      auto* region = find_region_nolock(id);
      if (region == nullptr)
        return;

      std::stringstream ss;
      ss << *pManip;
      region->pending_parts.emplace_back(std::move(ss).str());
    }
  }

  template <typename T>
  void write(RegionId id, const T& value) {
    std::lock_guard lock(mutex);

    if (regions.empty())
      return;

    if (id == regions.front().id) {
      // We can just directly print to the output because
      // we are at the front of the region queue.
      out << value;
    } else {
      // We have to schedule the print until previous regions are
      // processed
      auto* region = find_region_nolock(id);
      if (region == nullptr)
        return;

      std::stringstream ss;
      ss << value;
      region->pending_parts.emplace_back(std::move(ss).str());
    }
  }

  std::ostream& out;

  std::deque<RegionBookkeeping> regions;

  std::mutex mutex;

  RegionBookkeeping* find_region_nolock(RegionId id) {
    // Linear search because the amount of concurrent regions should be small.
    auto it = std::find_if(
      regions.begin(),
      regions.end(),
      [id](const RegionBookkeeping& r) { return r.id == id; });

    if (it == regions.end())
      return nullptr;
    else
      return &*it;
  }

  void release_region(RegionId id) {
    std::lock_guard lock(mutex);

    auto* region = find_region_nolock(id);
    if (region == nullptr)
      return;

    region->is_held = false;

    process_backlog_nolock();
  }

  void process_backlog_nolock()
  {
    while(!regions.empty()) {
      auto& region = regions.front();

      for(auto& part : region.pending_parts) {
        out << part;
      }

      // If the region is still held then we don't
      // want to start printing stuff from the next region.
      if (region.is_held)
        break;

      regions.pop_front();
    }
  }

public:

  SynchronizedRegionLogger(std::ostream& s) :
    out(s)
  {
  }

  [[nodiscard]] Region new_region() {
    const auto id = init_next_region();
    return Region(*this, id);
  }

};

extern SynchronizedRegionLogger sync_region_cout;

/// sigmoid(t, x0, y0, C, P, Q) implements a sigmoid-like function using only integers,
/// with the following properties:
///
///  -  sigmoid is centered in (x0, y0)
///  -  sigmoid has amplitude [-P/Q , P/Q] instead of [-1 , +1]
///  -  limit is (y0 - P/Q) when t tends to -infinity
///  -  limit is (y0 + P/Q) when t tends to +infinity
///  -  the slope can be adjusted using C > 0, smaller C giving a steeper sigmoid
///  -  the slope of the sigmoid when t = x0 is P/(Q*C)
///  -  sigmoid is increasing with t when P > 0 and Q > 0
///  -  to get a decreasing sigmoid, change sign of P
///  -  mean value of the sigmoid is y0
///
/// Use <https://www.desmos.com/calculator/jhh83sqq92> to draw the sigmoid

inline int64_t sigmoid(int64_t t, int64_t x0,
                                  int64_t y0,
                                  int64_t  C,
                                  int64_t  P,
                                  int64_t  Q)
{
   assert(C > 0);
   assert(Q != 0);
   return y0 + P * (t-x0) / (Q * (std::abs(t-x0) + C)) ;
}


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

static uint64_t string_hash(const std::string& str)
{
  uint64_t h = 525201411107845655ull;

  for (auto c : str) {
    h ^= static_cast<uint64_t>(c);
    h *= 0x5bd1e9955bd1e995ull;
    h ^= h >> 47;
  }

  return h;
}

class PRNG {

  uint64_t s;

  uint64_t rand64() {

    s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
    return s * 2685821657736338717LL;
  }

public:
  PRNG() { set_seed_from_time(); }
  PRNG(uint64_t seed) : s(seed) { assert(seed); }
  PRNG(const std::string& seed) { set_seed(seed); }

  template<typename T> T rand() { return T(rand64()); }

  /// Special generator used to fast init magic numbers.
  /// Output values only have 1/8th of their bits set on average.
  template<typename T> T sparse_rand()
  { return T(rand64() & rand64() & rand64()); }
  // Returns a random number from 0 to n-1. (Not uniform distribution, but this is enough in reality)
  uint64_t rand(uint64_t n) { return rand<uint64_t>() % n; }

  // Return the random seed used internally.
  uint64_t get_seed() const { return s; }

  void set_seed(uint64_t seed) { s = seed; }

  uint64_t next_random_seed()
  {
    uint64_t seed = 0;
    for(int i = 0; i < 64; ++i)
    {
      const auto off = rand64() % 64;
      seed |= (rand64() & (uint64_t(1) << off)) >> off;
      seed <<= 1;
    }
    return seed;
  }

  void set_seed_from_time()
  {
      set_seed(std::chrono::system_clock::now().time_since_epoch().count());
  }

  void set_seed(const std::string& str)
  {
    if (str.empty())
    {
      set_seed_from_time();
    }
    else if (std::all_of(str.begin(), str.end(), [](char c) { return std::isdigit(c);} )) {
      set_seed(std::stoull(str));
    }
    else
    {
      set_seed(string_hash(str));
    }
  }
};

// Display a random seed. (For debugging)
inline std::ostream& operator<<(std::ostream& os, PRNG& prng)
{
  os << "PRNG::seed = " << std::hex << prng.get_seed() << std::dec;
  return os;
}

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

// This bitset can be accessed concurrently, provided
// the concurrent accesses are performed on distinct
// instances of underlying type. That means the cuncurrent
// accesses need to be spaced by at least
// bits_per_bucket bits.
// But at least best_concurrent_access_stride bits
// is recommended to prevent false sharing.
template <uint64_t N>
struct LargeBitset
{
private:
    constexpr static uint64_t cache_line_size = 64;

public:
    using UnderlyingType = uint64_t;

    constexpr static uint64_t num_bits = N;
    constexpr static uint64_t bits_per_bucket = 8 * sizeof(uint64_t);
    constexpr static uint64_t num_buckets = (num_bits + bits_per_bucket - 1) / bits_per_bucket;
    constexpr static uint64_t best_concurrent_access_stride = 8 * cache_line_size;

    LargeBitset()
    {
        std::fill(std::begin(bits), std::end(bits), 0);
    }

    void set(uint64_t idx)
    {
        const uint64_t bucket = idx / bits_per_bucket;
        const uint64_t bit = uint64_t(1) << (idx % bits_per_bucket);
        bits[bucket] |= bit;
    }

    bool test(uint64_t idx) const
    {
        const uint64_t bucket = idx / bits_per_bucket;
        const uint64_t bit = uint64_t(1) << (idx % bits_per_bucket);
        return bits[bucket] & bit;
    }

    uint64_t count() const
    {
        uint64_t c = 0;
        uint64_t i = 0;

        for (; i < num_buckets - 3; i += 4)
        {
            uint64_t c0 = popcount(bits[i+0]);
            uint64_t c1 = popcount(bits[i+1]);
            uint64_t c2 = popcount(bits[i+2]);
            uint64_t c3 = popcount(bits[i+3]);
            c0 += c1;
            c2 += c3;
            c += c0 + c2;
        }

        for (; i < num_buckets; ++i)
        {
            c += popcount(bits[i]);
        }

        return c;
    }

private:
    alignas(cache_line_size) UnderlyingType bits[num_buckets];
};

/// Under Windows it is not possible for a process to run on more than one
/// logical processor group. This usually means to be limited to use max 64
/// cores. To overcome this, some special platform specific API should be
/// called to set group affinity for each thread. Original code from Texel by
/// Peter Österlund.

namespace WinProcGroup {
  void bindThisThread(size_t idx);
}

// Returns a string that represents the current time. (Used for log output when learning evaluation function)
std::string now_string();
void sleep(int ms);

namespace Algo {
    // Fisher-Yates
    template <typename Rng, typename T>
    void shuffle(std::vector<T>& buf, Rng&& prng)
    {
        const auto size = buf.size();
        for (uint64_t i = 0; i < size; ++i)
            std::swap(buf[i], buf[prng.rand(size - i) + i]);
    }

    // split the string
    inline std::vector<std::string> split(const std::string& input, char delimiter) {
        std::istringstream stream(input);
        std::string field;
        std::vector<std::string> fields;

        while (std::getline(stream, field, delimiter)) {
            fields.push_back(field);
        }

        return fields;
    }
}

// --------------------
//       Path
// --------------------

// Something like Path class in C#. File name manipulation.
// Match with the C# method name.
struct Path
{
	// Combine the path name and file name and return it.
	// If the folder name is not an empty string, append it if there is no'/' or'\\' at the end.
	static std::string combine(const std::string& folder, const std::string& filename)
	{
		if (folder.length() >= 1 && *folder.rbegin() != '/' && *folder.rbegin() != '\\')
			return folder + "/" + filename;

		return folder + filename;
	}

	// Get the file name part (excluding the folder name) from the full path expression.
	static std::string get_file_name(const std::string& path)
	{
		// I don't know which "\" or "/" is used.
		auto path_index1 = path.find_last_of("\\") + 1;
		auto path_index2 = path.find_last_of("/") + 1;
		auto path_index = std::max(path_index1, path_index2);

		return path.substr(path_index);
	}
};

// It is ignored when new even though alignas is specified & because it is ignored when the STL container allocates memory,
// A custom allocator used for that.
template <typename T>
class AlignedAllocator {
public:
  using value_type = T;

  AlignedAllocator() {}
  AlignedAllocator(const AlignedAllocator&) {}
  AlignedAllocator(AlignedAllocator&&) {}

  template <typename U> AlignedAllocator(const AlignedAllocator<U>&) {}

  T* allocate(std::size_t n) { return (T*)std_aligned_alloc(alignof(T), n * sizeof(T)); }
  void deallocate(T* p, std::size_t ) { std_aligned_free(p); }
};

template <typename T>
class CacheLineAlignedAllocator {
public:
    using value_type = T;

    constexpr static uint64_t cache_line_size = 64;

    CacheLineAlignedAllocator() {}
    CacheLineAlignedAllocator(const CacheLineAlignedAllocator&) {}
    CacheLineAlignedAllocator(CacheLineAlignedAllocator&&) {}

    template <typename U> CacheLineAlignedAllocator(const CacheLineAlignedAllocator<U>&) {}

    T* allocate(std::size_t n) { return (T*)std_aligned_alloc(cache_line_size, n * sizeof(T)); }
    void deallocate(T* p, std::size_t) { std_aligned_free(p); }
};

// --------------------
//  Dependency Wrapper
// --------------------

namespace Dependency
{
  // In the Linux environment, if you getline() the text file is'\r\n'
  // Since'\r' remains at the end, write a wrapper to remove this'\r'.
  // So when calling getline() on fstream,
  // just write getline() instead of std::getline() and use this function.
  extern bool getline(std::ifstream& fs, std::string& s);
}

namespace CommandLine {
  void init(int argc, char* argv[]);

  extern std::string binaryDirectory;  // path of the executable directory
  extern std::string workingDirectory; // path of the working directory
}

} // namespace Stockfish

#endif // #ifndef MISC_H_INCLUDED
