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

#ifndef I386_ARCH_H_INCLUDED
#define I386_ARCH_H_INCLUDED

#if !defined(__i386__) && !defined(__amd64__)
#error "Not supported in the current architecture."
#endif

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "common.h"

#if defined(__AVX__)

#include <immintrin.h>

#elif defined(__SSE4_1__)

#include <smmintrin.h>

#elif defined(__SSSE3__)

#include <tmmintrin.h>

#elif defined(__SSE2__)

#include <emmintrin.h>

// Some AMD processors with ABM do not support SSSE3/SSE4.1.
#if defined(__POPCNT__)
#include <popcntintrin.h>
#endif

#elif defined(__SSE__)

#include <xmmintrin.h>

#endif

#ifdef ARCH_NATIVE

// Do not use BMI2 PDEP/PEXT on AMD Zen 1/2.
#if defined(__BMI2__) && (defined(__znver1__) || defined(__znver2__))
#define NO_PDEP_PEXT 1
#endif

// Enable AVX-512 on AMD Zen 5 only.
#if defined(__AVX512F__) && !defined(__znver5__)
#define NO_AVX512 1
#endif

#endif  // ARCH_NATIVE

namespace Stockfish {

enum class PrefetchHint {
    ET0 = 7,
    T0  = 3,
    T1  = 2,
    T2  = 1,
    NTA = 0
};

// Register size is equal to address bits.
inline constexpr bool ArchImpl::Is64Bit = sizeof(void*) == 8;

#if defined(__BMI2__) && !defined(NO_PDEP_PEXT)
#define USE_PEXT 1
inline constexpr bool ArchImpl::UsePEXT = true;
#else
inline constexpr bool ArchImpl::UsePEXT = false;
#endif

template<int Hint>
inline void ArchImpl::prefetch([[maybe_unused]] const void* m) {
#ifdef __SSE__
    constexpr int __Hint = [] {
        if constexpr (Hint == -1)
            return 3;
        else
#ifdef __PRFCHW__
            return (Hint & 0x4) ? 7 : (Hint & 0x3);
#else
            return Hint & 0x3;
#endif
    }();

    // GCC doesn't comply with Intel Intrinsics Guide and uses enum instead
    // of int.
#if STOCKFISH_COMPILER == STOCKFISH_COMPILER_GCC
    _mm_prefetch(m, [] {
        if constexpr (__Hint == 7)
            return _MM_HINT_ET0;
        else if constexpr (__Hint == 3)
            return _MM_HINT_T0;
        else if constexpr (__Hint == 2)
            return _MM_HINT_T1;
        else if constexpr (__Hint == 1)
            return _MM_HINT_T2;
        else
            return _MM_HINT_NTA;
    }());
#else
    _mm_prefetch(m, __Hint);
#endif

#endif  // __SSE__
}

template<typename T>
inline unsigned int ArchImpl::popcount(T n) {
    static_assert(std::is_integral_v<T> && sizeof(T) <= 8);

#ifdef __POPCNT__
    if constexpr (sizeof(T) == 8)
        return _mm_popcnt_u64(std::uint64_t(n));
    else
        return _mm_popcnt_u32(std::uint32_t(n));
#else
    if constexpr (!is_64bit() && sizeof(T) == 8)
        return __popcount_table(n);
    else
        return __popcount_value(n);
#endif
}

template<typename T>
inline T ArchImpl::pext([[maybe_unused]] T n, [[maybe_unused]] T mask) {
#if defined(__BMI2__) && !defined(NO_PDEP_PEXT)
    static_assert(std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8));

    if constexpr (sizeof(T) == 8)
        return _pext_u64(std::uint64_t(n), std::uint64_t(mask));
    else
        return _pext_u32(std::uint32_t(n), std::uint32_t(mask));
#else
    return 0;
#endif
}

// ===========================================================================
// The functions below are used on i386/AMD64 targets only.
// ===========================================================================

template<typename T>
inline std::make_unsigned_t<T> blsr(T n) {
    static_assert(std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8));

#ifdef __BMI__
    if constexpr (sizeof(T) == 8)
        return _blsr_u64(std::uint64_t(n));
    else
        return _blsr_u32(std::uint32_t(n));
#else
    return std::make_unsigned_t<T>(n) & std::make_unsigned_t<T>(n - 1);
#endif
}

template<typename T>
inline int tzcnt(T n) {
    static_assert(std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8));

#ifdef __BMI__
    if constexpr (sizeof(T) == 8)
        return _tzcnt_u64(std::uint64_t(n));
    else
        return _tzcnt_u32(std::uint32_t(n));
#else
    assert(n != 0);

    if constexpr (sizeof(T) == 8)
        return __builtin_ctzll(n);
    else
        return __builtin_ctz(n);
#endif
}

#ifdef __SSE2__

template<typename T>
struct is_valid_vector {
    static constexpr bool value = sizeof(T) == 16
#ifdef __AVX2__
                               || sizeof(T) == 32
#endif
#ifdef __AVX512F__
                               || sizeof(T) == 64
#endif
      ;
};

template<typename T>
inline constexpr bool is_valid_vector_v = is_valid_vector<T>::value;

template<typename T>
inline T _mm_setzero_v() {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
        return _mm512_setzero_si512();
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
        return _mm256_setzero_si256();
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_setzero_si128();
}

template<typename T>
inline T _mm_set1_epi16_v(std::uint16_t n) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
        return _mm512_set1_epi16(n);
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
        return _mm256_set1_epi16(n);
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_set1_epi16(n);
}

template<typename T>
inline T _mm_set1_epi32_v(std::uint32_t n) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
        return _mm512_set1_epi32(n);
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
        return _mm256_set1_epi32(n);
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_set1_epi32(n);
}

template<typename T>
inline T _mm_packus_epi16_v(T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
#ifdef __AVX512BW__
        return _mm512_packus_epi16(a, b);
#else
        static_assert(false, "_mm_packus_epi16_v<__m512i> is not allowed without AVX-512 BW.");
#endif
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_packus_epi16(a, b);
#else
        static_assert(false, "_mm_packus_epi16_v<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_packus_epi16(a, b);
}

template<typename T>
inline T _mm_add_epi16_v(T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
#ifdef __AVX512BW__
        return _mm512_add_epi16(a, b);
#else
        static_assert(false, "_mm_add_epi16_v<__m512i> is not allowed without AVX-512 BW.");
#endif
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_add_epi16(a, b);
#else
        static_assert(false, "_mm_add_epi16_v<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_add_epi16(a, b);
}

template<typename T>
inline T _mm_add_epi32_v(T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
        return _mm512_add_epi32(a, b);
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_add_epi32(a, b);
#else
        static_assert(false, "_mm_add_epi32_v<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_add_epi32(a, b);
}

template<typename T>
inline T _mm_sub_epi16_v(T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
#ifdef __AVX512BW__
        return _mm512_sub_epi16(a, b);
#else
        static_assert(false, "_mm_sub_epi16_v<__m512i> is not allowed without AVX-512 BW.");
#endif
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_sub_epi16(a, b);
#else
        static_assert(false, "_mm_sub_epi16_v<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_sub_epi16(a, b);
}

template<typename T>
inline T _mm_sub_epi32_v(T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
        return _mm512_sub_epi32(a, b);
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_sub_epi32(a, b);
#else
        static_assert(false, "_mm_sub_epi32_v<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_sub_epi32(a, b);
}

template<typename T>
inline T _mm_mulhi_epi16_v(T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
#ifdef __AVX512BW__
        return _mm512_mulhi_epi16(a, b);
#else
        static_assert(false, "vmulhi_16<__m512i> is not allowed without AVX-512 BW.");
#endif
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_mulhi_epi16(a, b);
#else
        static_assert(false, "vmulhi_16<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_mulhi_epi16(a, b);
}

template<typename T>
inline T _mm_slli_epi16_v(T a, int n) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
#ifdef __AVX512BW__
        return _mm512_slli_epi16(a, n);
#else
        static_assert(false, "_mm_slli_epi16_v<__m512i> is not allowed without AVX-512 BW.");
#endif
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_slli_epi16(a, n);
#else
        static_assert(false, "_mm_slli_epi16_v<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_slli_epi16(a, n);
}

template<typename T>
inline T _mm_max_epi16_v(T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
#ifdef __AVX512BW__
        return _mm512_max_epi16(a, b);
#else
        static_assert(false, "_mm_max_epi16_v<__m512i> is not allowed without AVX-512 BW.");
#endif
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_max_epi16(a, b);
#else
        static_assert(false, "_mm_max_epi16_v<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_max_epi16(a, b);
}

template<typename T>
inline T _mm_min_epi16_v(T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
#ifdef __AVX512BW__
        return _mm512_min_epi16(a, b);
#else
        static_assert(false, "vmin_16<__m512i> is not allowed without AVX-512 BW.");
#endif
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
#ifdef __AVX2__
        return _mm256_min_epi16(a, b);
#else
        static_assert(false, "vmin_16<__m256i> is not allowed without AVX2.");
#endif
#endif

    if constexpr (sizeof(T) == 16)
        return _mm_min_epi16(a, b);
}

template<typename T>
inline std::int32_t _mm_reduce_add_epi32_v(T a) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
        return _mm512_reduce_add_epi32(a);
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
    {
        __m128i sum = _mm_add_epi32(_mm256_castsi256_si128(a), _mm256_extracti128_si256(a, 1));
        sum         = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, _MM_PERM_BADC));
        sum         = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, _MM_PERM_CDAB));
        return _mm_cvtsi128_si32(sum);
    }
#endif

    if constexpr (sizeof(T) == 16)
    {
        a = _mm_add_epi32(a, _mm_shuffle_epi32(a, 0x4E));  // _MM_PERM_BADC
        a = _mm_add_epi32(a, _mm_shuffle_epi32(a, 0xB1));  // _MM_PERM_CDAB
        return _mm_cvtsi128_si32(a);
    }
}

// Non-VNNI implementation of dpbusd works even with type saturation, only
// because output values are clamped in ReLU layers immediately after
// AffineTransform layer. Do not use this without VNNI for general purpose.
template<typename T>
inline void _mm_dpbusd_epi32_v(T& acc, T a, T b) {
    static_assert(is_valid_vector_v<T>);

#ifdef __AVX512F__
    if constexpr (sizeof(T) == 64)
    {
#if defined(__AVX512VNNI__)

        acc = _mm512_dpbusd_epi32(acc, a, b);

#elif defined(__AVX512BW__)

        __m512i product = _mm512_maddubs_epi16(a, b);
        product         = _mm512_madd_epi16(product, _mm512_set1_epi16(1));
        acc             = _mm512_add_epi32(acc, product);

#else
        static_assert(false, "_mm_dpbusd_epi32_v<__m512i> is not allowed without AVX-512 BW.");
#endif
    }
#endif

#ifdef __AVX__
    if constexpr (sizeof(T) == 32)
    {
#if (defined(__AVX512VL__) && defined(__AVX512VNNI__)) || defined(__AVXVNNI__)

        acc = _mm256_dpbusd_epi32(acc, a, b);

#elif defined(__AVX2__)

        __m256i product = _mm256_madd_epi16(_mm256_maddubs_epi16(a, b), _mm256_set1_epi16(1));
        acc             = _mm256_add_epi32(acc, product);

#else
        static_assert(false, "_mm_dpbusd_epi32_v<__m256i> is not allowed without AVX2.");
#endif
    }
#endif

    if constexpr (sizeof(T) == 16)
    {
#if (defined(__AVX512VL__) && defined(__AVX512VNNI__)) || defined(__AVXVNNI__)

        acc = _mm_dpbusd_epi32(acc, a, b);

#elif defined(__SSSE3__)

        __m128i product = _mm_madd_epi16(_mm_maddubs_epi16(a, b), _mm_set1_epi16(1));
        acc             = _mm_add_epi32(acc, product);

#else

        __m128i a0       = _mm_unpacklo_epi8(a, _mm_setzero_si128());
        __m128i a1       = _mm_unpackhi_epi8(a, _mm_setzero_si128());
        __m128i sgn      = _mm_cmplt_epi8(b, _mm_setzero_si128());
        __m128i b0       = _mm_unpacklo_epi8(b, sgn);
        __m128i b1       = _mm_unpackhi_epi8(b, sgn);
        __m128i product0 = _mm_madd_epi16(a0, b0);
        __m128i product1 = _mm_madd_epi16(a1, b1);
        __m128i product  = _mm_madd_epi16(_mm_packs_epi32(product0, product1), _mm_set1_epi16(1));
        acc              = _mm_add_epi32(acc, product);

#endif
    }
}

#endif  // __SSE2__

}  // namespace Stockfish

#endif  // I386_ARCH_H_INCLUDED
