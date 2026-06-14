/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>  // IWYU pragma: keep
// IWYU pragma: no_include <__exception/terminate.h>
#include <functional>
#include <iosfwd>
#include <optional>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if !defined(NO_PREFETCH) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))
    #include <immintrin.h>
#endif

#define stringify2(x) #x
#define stringify(x) stringify2(x)

namespace Stockfish {

using u64 = std::uint64_t;
using u32 = std::uint32_t;
using u16 = std::uint16_t;
using u8  = std::uint8_t;

using i64 = std::int64_t;
using i32 = std::int32_t;
using i16 = std::int16_t;
using i8  = std::int8_t;

using usize = std::size_t;
using isize = std::ptrdiff_t;

#if defined(__GNUC__) && defined(IS_64BIT)
__extension__ using u128 = unsigned __int128;
__extension__ using i128 = signed __int128;
#endif

std::string engine_version_info();
std::string engine_info(bool to_uci = false);
std::string compiler_info();

// Prefetch hint enums for explicit call-site control.
enum class PrefetchRw {
    READ,
    WRITE
};

// NOTE: PrefetchLoc controls locality / cache level, not whether a prefetch
//       is issued. In particular, PrefetchLoc::NONE maps to a non-temporal /
//       lowest-locality prefetch (Intel: _MM_HINT_NTA, GCC/Clang: locality = 0)
//       and therefore still performs a prefetch. To completely disable
//       prefetching, define NO_PREFETCH so that prefetch() becomes a no-op.
enum class PrefetchLoc {
    NONE,      // Non-temporal / no cache locality (still issues a prefetch)
    LOW,       // Low locality (e.g. T2 / L2)
    MODERATE,  // Moderate locality (e.g. T1 / L1)
    HIGH       // High locality (e.g. T0 / closest cache)
};

// Preloads the given address into cache. This is a non-blocking
// function that doesn't stall the CPU waiting for data to be loaded from memory,
// which can be quite slow.
#ifdef NO_PREFETCH
template<PrefetchRw RW = PrefetchRw::READ, PrefetchLoc LOC = PrefetchLoc::HIGH>
void prefetch(const void*) {}
#elif defined(_MSC_VER) || defined(__INTEL_COMPILER)

constexpr int get_intel_hint(PrefetchRw rw, PrefetchLoc loc) {
    if (rw == PrefetchRw::WRITE)
    {
    #ifdef _MM_HINT_ET0
        return _MM_HINT_ET0;
    #else
        // Fallback when write-prefetch hint is not available: use T0
        return _MM_HINT_T0;
    #endif
    }
    switch (loc)
    {
    case PrefetchLoc::NONE :
        return _MM_HINT_NTA;
    case PrefetchLoc::LOW :
        return _MM_HINT_T2;
    case PrefetchLoc::MODERATE :
        return _MM_HINT_T1;
    case PrefetchLoc::HIGH :
        return _MM_HINT_T0;
    default :
        return _MM_HINT_T0;
    }
}

template<PrefetchRw RW = PrefetchRw::READ, PrefetchLoc LOC = PrefetchLoc::HIGH>
void prefetch(const void* addr) {
    _mm_prefetch(static_cast<const char*>(addr), get_intel_hint(RW, LOC));
}
#else
template<PrefetchRw RW = PrefetchRw::READ, PrefetchLoc LOC = PrefetchLoc::HIGH>
void prefetch(const void* addr) {
    __builtin_prefetch(addr, static_cast<int>(RW), static_cast<int>(LOC));
}
#endif

void start_logger(const std::string& fname);

usize str_to_size_t(const std::string& s);

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
void dbg_mean_of(i64 value, int slot = 0);
void dbg_stdev_of(i64 value, int slot = 0);
void dbg_extremes_of(i64 value, int slot = 0);
void dbg_correl_of(i64 value1, i64 value2, int slot = 0);
void dbg_print();
void dbg_clear();

using TimePoint = std::chrono::milliseconds::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(i64), "TimePoint should be 64 bits");
inline TimePoint now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline std::vector<std::string_view> split(std::string_view s, std::string_view delimiter) {
    std::vector<std::string_view> res;

    if (s.empty())
        return res;

    usize begin = 0;
    for (;;)
    {
        const usize end = s.find(delimiter, begin);
        if (end == std::string::npos)
            break;

        res.emplace_back(s.substr(begin, end - begin));
        begin = end + delimiter.size();
    }

    res.emplace_back(s.substr(begin));

    return res;
}

void remove_whitespace(std::string& s);
bool is_whitespace(std::string_view s);

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
static inline const u16  Le             = 1;
static inline const bool IsLittleEndian = *reinterpret_cast<const char*>(&Le) == 1;


template<typename T, usize MaxSize>
class ValueList {

   public:
    usize size() const { return size_; }
    int   ssize() const { return int(size_); }
    void  push_back(const T& value) {
        assert(size_ < MaxSize);
        values_[size_++] = value;
    }
    // pushes back value if value < max
    void push_back_if_lt(const T& value, const T& max) {
        assert(size_ < MaxSize);
        values_[size_] = value;
        size_ += (value < max);
    }
    const T* begin() const { return values_; }
    const T* end() const { return values_ + size_; }
    const T& operator[](int index) const { return values_[index]; }

    T* make_space(usize count) {
        T* result = &values_[size_];
        size_ += count;
        assert(size_ <= MaxSize);
        return result;
    }

   private:
    T     values_[MaxSize];
    usize size_ = 0;
};


template<typename T, usize Size, usize... Sizes>
class MultiArray;

namespace Detail {

template<typename T, usize Size, usize... Sizes>
struct MultiArrayHelper {
    using ChildType = MultiArray<T, Sizes...>;
};

template<typename T, usize Size>
struct MultiArrayHelper<T, Size> {
    using ChildType = T;
};

template<typename To, typename From>
constexpr bool is_strictly_assignable_v =
  std::is_assignable_v<To&, From> && (std::is_same_v<To, From> || !std::is_convertible_v<From, To>);

}

// MultiArray is a generic N-dimensional array.
// The template parameters (Size and Sizes) encode the dimensions of the array.
template<typename T, usize Size, usize... Sizes>
class MultiArray {
    using ChildType = typename Detail::MultiArrayHelper<T, Size, Sizes...>::ChildType;
    using ArrayType = std::array<ChildType, Size>;
    ArrayType data_;

   public:
    using value_type             = typename ArrayType::value_type;
    using size_type              = typename ArrayType::size_type;
    using difference_type        = typename ArrayType::difference_type;
    using reference              = typename ArrayType::reference;
    using const_reference        = typename ArrayType::const_reference;
    using pointer                = typename ArrayType::pointer;
    using const_pointer          = typename ArrayType::const_pointer;
    using iterator               = typename ArrayType::iterator;
    using const_iterator         = typename ArrayType::const_iterator;
    using reverse_iterator       = typename ArrayType::reverse_iterator;
    using const_reverse_iterator = typename ArrayType::const_reverse_iterator;

    constexpr auto&       at(size_type index) { return data_.at(index); }
    constexpr const auto& at(size_type index) const { return data_.at(index); }

    constexpr auto& operator[](size_type index) noexcept {
        assert(index < Size);
        return data_[index];
    }
    constexpr const auto& operator[](size_type index) const noexcept {
        assert(index < Size);
        return data_[index];
    }

    constexpr auto&       front() noexcept { return data_.front(); }
    constexpr const auto& front() const noexcept { return data_.front(); }
    constexpr auto&       back() noexcept { return data_.back(); }
    constexpr const auto& back() const noexcept { return data_.back(); }

    auto*       data() { return data_.data(); }
    const auto* data() const { return data_.data(); }

    constexpr auto begin() noexcept { return data_.begin(); }
    constexpr auto end() noexcept { return data_.end(); }
    constexpr auto begin() const noexcept { return data_.begin(); }
    constexpr auto end() const noexcept { return data_.end(); }
    constexpr auto cbegin() const noexcept { return data_.cbegin(); }
    constexpr auto cend() const noexcept { return data_.cend(); }

    constexpr auto rbegin() noexcept { return data_.rbegin(); }
    constexpr auto rend() noexcept { return data_.rend(); }
    constexpr auto rbegin() const noexcept { return data_.rbegin(); }
    constexpr auto rend() const noexcept { return data_.rend(); }
    constexpr auto crbegin() const noexcept { return data_.crbegin(); }
    constexpr auto crend() const noexcept { return data_.crend(); }

    constexpr bool      empty() const noexcept { return data_.empty(); }
    constexpr size_type size() const noexcept { return data_.size(); }
    constexpr size_type max_size() const noexcept { return data_.max_size(); }

    template<typename U>
    void fill(const U& v) {
        static_assert(Detail::is_strictly_assignable_v<T, U>,
                      "Cannot assign fill value to entry type");
        for (auto& ele : data_)
        {
            if constexpr (sizeof...(Sizes) == 0)
                ele = v;
            else
                ele.fill(v);
        }
    }

    constexpr void swap(MultiArray<T, Size, Sizes...>& other) noexcept { data_.swap(other.data_); }
};

// Wrapper around std::atomic<T> which uses relaxed accesses or plain
// accesses, depending on the config. Intended use is e.g. wasm where
// the overhead of atomic instructions can be significant, and we only
// require non-tearing for the updates, while ensuring we use relaxed
// accesses otherwise.
template<typename T>
class RelaxedAtomic {
    static constexpr bool UseAtomic =
#ifdef USE_SLOPPY_ATOMICS
      !std::atomic<T>::is_always_lock_free || sizeof(T) > sizeof(usize);
#else
      true;
#endif

   public:
    RelaxedAtomic() = default;
    RelaxedAtomic(T val) :
        inner(val) {}
    RelaxedAtomic(const RelaxedAtomic& a) :
        inner(static_cast<T>(a)) {}

    T operator=(T val) {
        if constexpr (UseAtomic)
            inner.store(val, std::memory_order_relaxed);
        else
            inner = val;
        return val;
    }

    RelaxedAtomic& operator=(const RelaxedAtomic& a) {
        this->store(static_cast<T>(a), std::memory_order_relaxed);
        return *this;
    }

    operator T() const {
        if constexpr (UseAtomic)
            return inner.load(std::memory_order_relaxed);
        else
            return inner;
    }

    RelaxedAtomic& operator+=(T val) {
        T res = this->load(std::memory_order_relaxed) + val;
        this->store(res, std::memory_order_relaxed);
        return *this;
    }

    RelaxedAtomic& operator++() {
        T res = this->load(std::memory_order_relaxed) + 1;
        this->store(res, std::memory_order_relaxed);
        return *this;
    }

    RelaxedAtomic& operator--() {
        T res = this->load(std::memory_order_relaxed) - 1;
        this->store(res, std::memory_order_relaxed);
        return *this;
    }

    T operator++(int) {
        T val = this->load(std::memory_order_relaxed);
        this->store(val + 1, std::memory_order_relaxed);
        return val;
    }

    T operator--(int) {
        T val = this->load(std::memory_order_relaxed);
        this->store(val - 1, std::memory_order_relaxed);
        return val;
    }

    RelaxedAtomic& operator-=(T val) {
        T res = this->load(std::memory_order_relaxed) - val;
        this->store(res, std::memory_order_relaxed);
        return *this;
    }

    T load(std::memory_order order) const {
        assert(order == std::memory_order_relaxed);
        if constexpr (UseAtomic)
            return inner.load(order);
        else
            return inner;
    }

    void store(T val, std::memory_order order) {
        assert(order == std::memory_order_relaxed);
        if constexpr (UseAtomic)
            inner.store(val, order);
        else
            inner = val;
    }

   private:
    std::conditional_t<UseAtomic, std::atomic<T>, T> inner;
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

    u64 s;

    u64 rand64() {

        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return s * 2685821657736338717LL;
    }

   public:
    PRNG(u64 seed) :
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

inline usize mul_hi64(u64 a, usize b) {
#if defined(__GNUC__) && defined(IS_64BIT) && !defined(__wasm__)
    return (u128(a) * u128(b)) >> 64;
#else
    u64 aL = u32(a), aH = a >> 32;
    u64 bL = u32(b), bH = u64(b) >> 32;
    u64 c1 = (aL * bL) >> 32;
    u64 c2 = aH * bL + c1;
    u64 c3 = aL * bH + u32(c2);
    return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif
}

template<typename T1, typename T2>
inline constexpr T2 interpolate(T1 x, T1 x0, T1 x1, T2 y0, T2 y1) {
    assert(x0 != x1);
    return T2(y0 + (y1 - y0) * (x - x0) / (x1 - x0));
}

u64 hash_bytes(const char*, usize);

template<typename T>
inline usize get_raw_data_hash(const T& value) {
    // We must have no padding bytes because we're reinterpreting as char
    static_assert(std::has_unique_object_representations<T>());

    return static_cast<usize>(hash_bytes(reinterpret_cast<const char*>(&value), sizeof(value)));
}

template<typename T>
inline void hash_combine(usize& seed, const T& v) {
    usize x;
    // For primitive types we avoid using the default hasher, which may be
    // nondeterministic across program invocations
    if constexpr (std::is_integral<T>())
        x = v;
    else
        x = std::hash<T>{}(v);
    seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

inline u64 hash_string(const std::string& sv) { return hash_bytes(sv.data(), sv.size()); }

template<usize Capacity>
class FixedString {
   public:
    FixedString() :
        length_(0) {
        data_[0] = '\0';
    }

    FixedString(const char* str) {
        usize len = std::strlen(str);
        if (len > Capacity)
            std::terminate();
        std::memcpy(data_, str, len);
        length_        = len;
        data_[length_] = '\0';
    }

    FixedString(const std::string& str) {
        if (str.size() > Capacity)
            std::terminate();
        std::memcpy(data_, str.data(), str.size());
        length_        = str.size();
        data_[length_] = '\0';
    }

    usize size() const { return length_; }
    usize capacity() const { return Capacity; }

    const char* c_str() const { return data_; }
    const char* data() const { return data_; }

    char& operator[](usize i) { return data_[i]; }

    const char& operator[](usize i) const { return data_[i]; }

    FixedString& operator+=(const char* str) {
        usize len = std::strlen(str);
        if (length_ + len > Capacity)
            std::terminate();
        std::memcpy(data_ + length_, str, len);
        length_ += len;
        data_[length_] = '\0';
        return *this;
    }

    FixedString& operator+=(const FixedString& other) { return (*this += other.c_str()); }

    operator std::string() const { return std::string(data_, length_); }

    operator std::string_view() const { return std::string_view(data_, length_); }

    template<typename T>
    bool operator==(const T& other) const noexcept {
        return (std::string_view) (*this) == other;
    }

    template<typename T>
    bool operator!=(const T& other) const noexcept {
        return (std::string_view) (*this) != other;
    }

    void clear() {
        length_  = 0;
        data_[0] = '\0';
    }

   private:
    char  data_[Capacity + 1];  // +1 for null terminator
    usize length_;
};

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

#if defined(__GNUC__)
    #define sf_always_inline __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define sf_always_inline __forceinline
#else
    // do nothing for other compilers
    #define sf_always_inline
#endif

#if defined(__clang__)
    #define sf_assume(cond) __builtin_assume(cond)
#elif defined(__GNUC__)
    #if __GNUC__ >= 13
        #define sf_assume(cond) __attribute__((assume(cond)))
    #else
        #define sf_assume(cond) \
            do \
            { \
                if (!(cond)) \
                    __builtin_unreachable(); \
            } while (0)
    #endif
#elif defined(_MSC_VER)
    #define sf_assume(cond) __assume(cond)
#else
    // do nothing for other compilers
    #define sf_assume(cond)
#endif

#ifdef __GNUC__
    #define sf_unreachable() __builtin_unreachable()
#elif defined(_MSC_VER)
    #define sf_unreachable() __assume(0)
#else
    #define sf_unreachable()
#endif

#ifdef __GNUC__
    #define RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define RESTRICT __restrict
#else
    #define RESTRICT
#endif

}  // namespace Stockfish

template<Stockfish::usize N>
struct std::hash<Stockfish::FixedString<N>> {
    Stockfish::usize operator()(const Stockfish::FixedString<N>& fstr) const noexcept {
        return Stockfish::hash_bytes(fstr.data(), fstr.size());
    }
};

#endif  // #ifndef MISC_H_INCLUDED
