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

#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// When compiling with provided Makefile (e.g. for Linux and OSX),
// configuration is done automatically. To get started, type 'make help'.
//
// When Makefile is not used (e.g. with Microsoft Visual Studio), some macros
// need to be pre-defined manually:
//
// NDEBUG       Disable debugging mode. Always use this for release.
//
// __SSE__      Generate x86 prefetch instruction.
// __SSE2__     Generate x86 SSE2 SIMD instructions.
// __SSSE3__    Generate x86 SSSE3 SIMD instructions.
// __SSE4_1__   Generate x86 SSE4.1 SIMD instructions.
// __POPCNT__   Generate x86 POPCNT instruction.
// __PRFCHW__   Generate x86 PREFETCHW instruction. (not used currently)
// __AVX__      Generate x86 AVX SIMD instructions.
// __BMI__      Generate x86 BLSR and TZCNT instructions.
// __AVX2__     Generate x86 AVX2 SIMD instructions.
// __BMI2__     Generate x86 PEXT instruction.
// __AVX512F__  Generate x86 AVX-512 SIMD instructions.
// __AVX512BW__         ...
// __AVX512VL__         ...
// __AVX512VNNI__       ...

#define STOCKFISH_COMPILER_UNKNOWN 0
#define STOCKFISH_COMPILER_GCC 1
#define STOCKFISH_COMPILER_CLANG 2
#define STOCKFISH_COMPILER_INTEL 3
#define STOCKFISH_COMPILER_MSVC 4

#if defined(__GNUC__)
    #if defined(__INTEL_LLVM_COMPILER)
        #define STOCKFISH_COMPILER STOCKFISH_COMPILER_INTEL
    #elif defined(__clang__)
        #define STOCKFISH_COMPILER STOCKFISH_COMPILER_CLANG
    #else
        #define STOCKFISH_COMPILER STOCKFISH_COMPILER_GCC
    #endif
#elif defined(_MSC_VER)
    #define STOCKFISH_COMPILER STOCKFISH_COMPILER_MSVC
#else
    #define STOCKFISH_COMPILER STOCKFISH_COMPILER_UNKNOWN
#endif

#if STOCKFISH_COMPILER == STOCKFISH_COMPILER_GCC
    #if __GNUC__ < 9 || (__GNUC__ == 9 && __GNUC_MINOR__ <= 2)
        #define ALIGNAS_ON_STACK_VARIABLES_BROKEN
    #endif
#elif STOCKFISH_COMPILER == STOCKFISH_COMPILER_MSVC
    #pragma warning(disable: 4127)  // Conditional expression is constant
    #pragma warning(disable: 4146)  // Unary minus operator applied to unsigned type
    #pragma warning(disable: 4800)  // Forcing value to bool 'true' or 'false'

    #if defined(_WIN64)
        #include <intrin.h>
    #endif
#endif

#define ASSERT_ALIGNED(ptr, alignment) \
    assert(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0)

namespace Stockfish {

template<typename T, std::size_t N>
constexpr std::size_t array_size(T (&)[N]) {
    return N;
}

// Instead of using raw integer values, give each hint a comprehensive name.
// Default is always -1, however, the actual value of it is defined in
// implementation detail.
enum class PrefetchHint;

// This struct is used to provide generalized functionalities that might have
// different implementations depending on the target architecture. Each
// member and function defined in this struct is specialized in arch.h file
// respectively.
struct ArchImpl {
    static const bool Is64Bit;

    // Clang apparently does not follow SFIANE in if constexpr statements,
    // therefore annotate arguments with maybe_unused attribute to avoid
    // warnings.

    template<int Hint>
    static inline void prefetch(const void* m);

    template<typename T>
    static inline unsigned int popcount(T n);

    template<typename T>
    static inline T pext(T n, T mask);
};

constexpr bool is_64bit() { return ArchImpl::Is64Bit; }

// 64-bit builtin popcount is sometimes slower than using table, especially on
// 32-bit environment. Therefore we provide two versions of it and leave it
// for each platform to decide which one to use.
template<typename T>
inline int __popcount_table(T n) {
    static_assert(std::is_integral_v<T> && sizeof(T) % 2 == 0);

    static const std::array<std::uint8_t, 1 << 16> popcntTable = [] {
        std::array<std::uint8_t, 1 << 16> table;
        for (int i = 0; i < 1 << 16; ++i)
            table[i] = std::uint8_t(std::bitset<16>(i).count());
        return table;
    }();

    union {
        T             raw;
        std::uint16_t words[sizeof(T) / 2];
    } v = {n};

    int count = 0;
    for (std::size_t i = 0; i < sizeof(T) / 2; ++i)
        count += popcntTable[v.words[i]];

    return count;
}

template<typename T>
inline int __popcount_value(T n) {
#ifdef __GNUC__
    if constexpr (sizeof(T) == 8)
        return __builtin_popcountll(std::uint64_t(n));
    else
        return __builtin_popcount(std::uint32_t(n));
#else
    if constexpr (sizeof(T) == 8)
        return __popcount_value(std::uint32_t(n)) + __popcount_value(std::uint32_t(n >> 32));
    else
    {
        n = n - ((n >> 1) & 0x55555555);
        n = (n & 0x33333333) + ((n >> 2) & 0x33333333);
        return (((n + (n >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
    }
#endif
}

template<PrefetchHint Hint = static_cast<PrefetchHint>(-1)>
inline void prefetch(const void* m) {
    return ArchImpl::prefetch<static_cast<int>(Hint)>(m);
}

template<typename T>
inline unsigned int popcount(T n) {
    return ArchImpl::popcount(n);
}

template<typename T>
inline T pext(T n, T mask) {
    return ArchImpl::pext(n, mask);
}

}  // namespace Stockfish

#if defined(__i386__) || defined(__amd64__)

    #include "arch/i386/arch.h"

#elif defined(__arm__) || defined(__aarch64__)

    #include "arch/arm/arch.h"

#elif defined(__PPC__)

    #include "arch/ppc/arch.h"

#else

    #include "arch/generic/arch.h"

#endif

#endif  // COMMON_H_INCLUDED
