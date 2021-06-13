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

#ifndef SIMD_H_INCLUDED
#define SIMD_H_INCLUDED

#if defined(USE_AVX2)
#  include <immintrin.h>
#  define SIMD_AVAILABLE
#endif

#if defined(USE_SSE41)
#  include <smmintrin.h>
#  define SIMD_AVAILABLE
#endif

#if defined(USE_SSSE3)
#  include <tmmintrin.h>
#  define SIMD_AVAILABLE
#endif

#if defined(USE_SSE2)
#  include <emmintrin.h>
#  define SIMD_AVAILABLE
#endif

#if defined(USE_MMX)
#  include <mmintrin.h>
#  define SIMD_AVAILABLE
#endif

#if defined(USE_NEON)
#  include <arm_neon.h>
#  define SIMD_AVAILABLE
#endif

#include <cstdint>
#include <type_traits>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"

namespace Stockfish::Simd {

  enum struct Arch {
    None,
    MMX,
    SSE2,
    SSSE3,
    SSE41,
    AVX2,
    VNNI256,
    AVX512,
    VNNI512,
    NEON
  };

  namespace Detail {

    template <typename T, typename... Us>
    constexpr bool isTypeAnyOf() {
      return (std::is_same_v<T, Us> || ...);
    }

#if defined(AVX512)
    constexpr std::size_t NumSimdRegistersX86 = 32;
#elif defined(IS_64BIT)
    constexpr std::size_t NumSimdRegistersX86 = 16;
#else
    constexpr std::size_t NumSimdRegistersX86 = 8;
#endif

    template <Arch ArchV, typename LaneT> struct TypeDef {};

    template <Arch ArchV> struct NumRegistersDef { static constexpr std::size_t Value = 0; };

    template <Arch ArchV> struct NumBytesDef {};

    template <Arch ArchV, typename LaneT>
    struct NumLanesDef {
      using SimdT = typename TypeDef<ArchV, LaneT>::Type;

      static constexpr std::size_t Value = sizeof(SimdT) / sizeof(LaneT);
    };

    template <Arch ArchV, typename LaneT> struct LoadAlignedDef {};
    template <Arch ArchV, typename LaneT> struct StoreAlignedDef {};
    template <Arch ArchV, typename LaneT> struct AddDef {};
    template <Arch ArchV, typename LaneT> struct SubDef {};
    template <Arch ArchV, typename LaneT> struct ZerosDef {};
    template <Arch ArchV, typename LaneT> struct BroadcastDef {};

#if defined(USE_MMX)

    template <> struct TypeDef<Arch::MMX, std::uint8_t > { using Type = __m64; };
    template <> struct TypeDef<Arch::MMX, std:: int8_t > { using Type = __m64; };
    template <> struct TypeDef<Arch::MMX, std::uint16_t> { using Type = __m64; };
    template <> struct TypeDef<Arch::MMX, std:: int16_t> { using Type = __m64; };
    template <> struct TypeDef<Arch::MMX, std::uint32_t> { using Type = __m64; };
    template <> struct TypeDef<Arch::MMX, std:: int32_t> { using Type = __m64; };

    template <> struct NumRegistersDef<Arch::MMX> { static constexpr std::size_t Value = 8; };

    template <> struct NumBytesDef<Arch::MMX> { static constexpr std::size_t Value = 8; };

    template <typename LaneT>
    struct LoadAlignedDef<Arch::MMX, LaneT> {
      using SimdT = typename TypeDef<Arch::MMX, LaneT>::Type;

      static constexpr auto Value = [](const SimdT* v) {
        return *v;
      };
    };

    template <typename LaneT>
    struct StoreAlignedDef<Arch::MMX, LaneT> {
      using SimdT = typename TypeDef<Arch::MMX, LaneT>::Type;

      static constexpr auto Value = [](SimdT* lhs, SimdT rhs) {
        return (*lhs) = rhs;
      };
    };

    template <typename LaneT>
    struct AddDef<Arch::MMX, LaneT> {
      using SimdT = typename TypeDef<Arch::MMX, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (sizeof(LaneT) == 1)
          return _mm_add_pi8(lhs, rhs);
        else if constexpr (sizeof(LaneT) == 2)
          return _mm_add_pi16(lhs, rhs);
        else
          return _mm_add_pi32(lhs, rhs);
      };
    };

    template <typename LaneT>
    struct SubDef<Arch::MMX, LaneT> {
      using SimdT = typename TypeDef<Arch::MMX, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (sizeof(LaneT) == 1)
          return _mm_sub_pi8(lhs, rhs);
        else if constexpr (sizeof(LaneT) == 2)
          return _mm_sub_pi16(lhs, rhs);
        else
          return _mm_sub_pi32(lhs, rhs);
      };
    };

    template <typename LaneT>
    struct ZerosDef<Arch::MMX, LaneT> {
      using SimdT = typename TypeDef<Arch::MMX, LaneT>::Type;

      static constexpr auto Value = []() {
        return _mm_setzero_si64();
      };
    };

#endif

#if defined(USE_SSE2)

    template <> struct TypeDef<Arch::SSE2, std::uint8_t > { using Type = __m128i; };
    template <> struct TypeDef<Arch::SSE2, std:: int8_t > { using Type = __m128i; };
    template <> struct TypeDef<Arch::SSE2, std::uint16_t> { using Type = __m128i; };
    template <> struct TypeDef<Arch::SSE2, std:: int16_t> { using Type = __m128i; };
    template <> struct TypeDef<Arch::SSE2, std::uint32_t> { using Type = __m128i; };
    template <> struct TypeDef<Arch::SSE2, std:: int32_t> { using Type = __m128i; };
    template <> struct TypeDef<Arch::SSE2, std::uint64_t> { using Type = __m128i; };
    template <> struct TypeDef<Arch::SSE2, std:: int64_t> { using Type = __m128i; };
    template <> struct TypeDef<Arch::SSE2,         float> { using Type = __m128;  };
    template <> struct TypeDef<Arch::SSE2,        double> { using Type = __m128;  };

    template <> struct NumRegistersDef<Arch::SSE2> { static constexpr std::size_t Value = NumSimdRegistersX86; };

    template <> struct NumBytesDef<Arch::SSE2> { static constexpr std::size_t Value = 16; };

    template <typename LaneT>
    struct LoadAlignedDef<Arch::SSE2, LaneT> {
      using SimdT = typename TypeDef<Arch::SSE2, LaneT>::Type;

      static constexpr auto Value = [](const SimdT* v) {
        return *v;
      };
    };

    template <typename LaneT>
    struct StoreAlignedDef<Arch::SSE2, LaneT> {
      using SimdT = typename TypeDef<Arch::SSE2, LaneT>::Type;

      static constexpr auto Value = [](SimdT* lhs, SimdT rhs) {
        return (*lhs) = rhs;
      };
    };

    template <typename LaneT>
    struct AddDef<Arch::SSE2, LaneT> {
      using SimdT = typename TypeDef<Arch::SSE2, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm_add_ps(lhs, rhs);
          else
            return _mm_add_pd(lhs, rhs);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm_add_epi8(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm_add_epi16(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm_add_epi32(lhs, rhs);
          else
            return _mm_add_epi64(lhs, rhs);
        }
      };
    };

    template <typename LaneT>
    struct SubDef<Arch::SSE2, LaneT> {
      using SimdT = typename TypeDef<Arch::SSE2, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm_sub_ps(lhs, rhs);
          else
            return _mm_sub_pd(lhs, rhs);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm_sub_epi8(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm_sub_epi16(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm_sub_epi32(lhs, rhs);
          else
            return _mm_sub_epi64(lhs, rhs);
        }
      };
    };

    template <typename LaneT>
    struct ZerosDef<Arch::SSE2, LaneT> {
      using SimdT = typename TypeDef<Arch::SSE2, LaneT>::Type;

      static constexpr auto Value = []() {
        return _mm_setzero_si128();
      };
    };

    template <typename LaneT>
    struct BroadcastDef<Arch::SSE2, LaneT> {
      using SimdT = typename TypeDef<Arch::SSE2, LaneT>::Type;

      static constexpr auto Value = [](LaneT value) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm_set1_ps(value);
          else
            return _mm_set1_pd(value);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm_set1_epi8(value);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm_set1_epi16(value);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm_set1_epi32(value);
          else
            return _mm_set1_epi64(value);
        }
      };
    };

# if defined(USE_SSSE3)

    template <typename LaneT> struct TypeDef<Arch::SSSE3, LaneT> : TypeDef<Arch::SSE2, LaneT> {};
    template <> struct NumRegistersDef<Arch::SSSE3> : NumRegistersDef<Arch::SSE2> {};
    template <> struct NumBytesDef<Arch::SSSE3> : NumBytesDef<Arch::SSE2> {};
    template <typename LaneT> struct LoadAlignedDef<Arch::SSSE3, LaneT> : LoadAlignedDef<Arch::SSE2, LaneT> {};
    template <typename LaneT> struct StoreAlignedDef<Arch::SSSE3, LaneT> : StoreAlignedDef<Arch::SSE2, LaneT> {};
    template <typename LaneT> struct AddDef<Arch::SSSE3, LaneT> : AddDef<Arch::SSE2, LaneT> {};
    template <typename LaneT> struct SubDef<Arch::SSSE3, LaneT> : SubDef<Arch::SSE2, LaneT> {};
    template <typename LaneT> struct ZerosDef<Arch::SSSE3, LaneT> : ZerosDef<Arch::SSE2, LaneT> {};
    template <typename LaneT> struct BroadcastDef<Arch::SSSE3, LaneT> : BroadcastDef<Arch::SSE2, LaneT> {};
# endif

# if defined(USE_SSE41)

    template <typename LaneT> struct TypeDef<Arch::SSE41, LaneT> : TypeDef<Arch::SSSE3, LaneT> {};
    template <> struct NumRegistersDef<Arch::SSE41> : NumRegistersDef<Arch::SSSE3> {};
    template <> struct NumBytesDef<Arch::SSE41> : NumBytesDef<Arch::SSSE3> {};
    template <typename LaneT> struct LoadAlignedDef<Arch::SSE41, LaneT> : LoadAlignedDef<Arch::SSSE3, LaneT> {};
    template <typename LaneT> struct StoreAlignedDef<Arch::SSE41, LaneT> : StoreAlignedDef<Arch::SSSE3, LaneT> {};
    template <typename LaneT> struct AddDef<Arch::SSE41, LaneT> : AddDef<Arch::SSSE3, LaneT> {};
    template <typename LaneT> struct SubDef<Arch::SSE41, LaneT> : SubDef<Arch::SSSE3, LaneT> {};
    template <typename LaneT> struct ZerosDef<Arch::SSE41, LaneT> : ZerosDef<Arch::SSSE3, LaneT> {};
    template <typename LaneT> struct BroadcastDef<Arch::SSE41, LaneT> : BroadcastDef<Arch::SSSE3, LaneT> {};
# endif

#endif

#if defined(USE_AVX2)

    template <> struct TypeDef<Arch::AVX2, std::uint8_t > { using Type = __m256i; };
    template <> struct TypeDef<Arch::AVX2, std:: int8_t > { using Type = __m256i; };
    template <> struct TypeDef<Arch::AVX2, std::uint16_t> { using Type = __m256i; };
    template <> struct TypeDef<Arch::AVX2, std:: int16_t> { using Type = __m256i; };
    template <> struct TypeDef<Arch::AVX2, std::uint32_t> { using Type = __m256i; };
    template <> struct TypeDef<Arch::AVX2, std:: int32_t> { using Type = __m256i; };
    template <> struct TypeDef<Arch::AVX2, std::uint64_t> { using Type = __m256i; };
    template <> struct TypeDef<Arch::AVX2, std:: int64_t> { using Type = __m256i; };
    template <> struct TypeDef<Arch::AVX2,         float> { using Type = __m256;  };
    template <> struct TypeDef<Arch::AVX2,        double> { using Type = __m256;  };

    template <> struct NumRegistersDef<Arch::AVX2> { static constexpr std::size_t Value = NumSimdRegistersX86; };

    template <> struct NumBytesDef<Arch::AVX2> { static constexpr std::size_t Value = 32; };

    template <typename LaneT>
    struct LoadAlignedDef<Arch::AVX2, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX2, LaneT>::Type;

      static constexpr auto Value = [](const SimdT* v) {
        return *v;
      };
    };

    template <typename LaneT>
    struct StoreAlignedDef<Arch::AVX2, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX2, LaneT>::Type;

      static constexpr auto Value = [](SimdT* lhs, SimdT rhs) {
        return (*lhs) = rhs;
      };
    };

    template <typename LaneT>
    struct AddDef<Arch::AVX2, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX2, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm256_add_ps(lhs, rhs);
          else
            return _mm256_add_pd(lhs, rhs);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm256_add_epi8(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm256_add_epi16(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm256_add_epi32(lhs, rhs);
          else
            return _mm256_add_epi64(lhs, rhs);
        }
      };
    };

    template <typename LaneT>
    struct SubDef<Arch::AVX2, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX2, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm256_sub_ps(lhs, rhs);
          else
            return _mm256_sub_pd(lhs, rhs);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm256_sub_epi8(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm256_sub_epi16(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm256_sub_epi32(lhs, rhs);
          else
            return _mm256_sub_epi64(lhs, rhs);
        }
      };
    };

    template <typename LaneT>
    struct ZerosDef<Arch::AVX2, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX2, LaneT>::Type;

      static constexpr auto Value = []() {
        return _mm256_setzero_si256();
      };
    };

    template <typename LaneT>
    struct BroadcastDef<Arch::AVX2, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX2, LaneT>::Type;

      static constexpr auto Value = [](LaneT value) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm256_set1_ps(value);
          else
            return _mm256_set1_pd(value);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm256_set1_epi8(value);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm256_set1_epi16(value);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm256_set1_epi32(value);
          else
            return _mm256_set1_epi64(value);
        }
      };
    };

# if defined(USE_VNNI)

    template <typename LaneT> struct TypeDef<Arch::VNNI256, LaneT> : TypeDef<Arch::AVX2, LaneT> {};
    template <> struct NumRegistersDef<Arch::VNNI256> : NumRegistersDef<Arch::AVX2> {};
    template <> struct NumBytesDef<Arch::VNNI256> : NumBytesDef<Arch::AVX2> {};
    template <typename LaneT> struct LoadAlignedDef<Arch::VNNI256, LaneT> : LoadAlignedDef<Arch::AVX2, LaneT> {};
    template <typename LaneT> struct StoreAlignedDef<Arch::VNNI256, LaneT> : StoreAlignedDef<Arch::AVX2, LaneT> {};
    template <typename LaneT> struct AddDef<Arch::VNNI256, LaneT> : AddDef<Arch::AVX2, LaneT> {};
    template <typename LaneT> struct SubDef<Arch::VNNI256, LaneT> : SubDef<Arch::AVX2, LaneT> {};
    template <typename LaneT> struct ZerosDef<Arch::VNNI256, LaneT> : ZerosDef<Arch::AVX2, LaneT> {};
    template <typename LaneT> struct BroadcastDef<Arch::VNNI256, LaneT> : BroadcastDef<Arch::AVX2, LaneT> {};
# endif

#endif

#if defined(USE_AVX512)

    template <> struct TypeDef<Arch::AVX512, std::uint8_t > { using Type = __m512i; };
    template <> struct TypeDef<Arch::AVX512, std:: int8_t > { using Type = __m512i; };
    template <> struct TypeDef<Arch::AVX512, std::uint16_t> { using Type = __m512i; };
    template <> struct TypeDef<Arch::AVX512, std:: int16_t> { using Type = __m512i; };
    template <> struct TypeDef<Arch::AVX512, std::uint32_t> { using Type = __m512i; };
    template <> struct TypeDef<Arch::AVX512, std:: int32_t> { using Type = __m512i; };
    template <> struct TypeDef<Arch::AVX512, std::uint64_t> { using Type = __m512i; };
    template <> struct TypeDef<Arch::AVX512, std:: int64_t> { using Type = __m512i; };
    template <> struct TypeDef<Arch::AVX512,         float> { using Type = __m512;  };
    template <> struct TypeDef<Arch::AVX512,        double> { using Type = __m512;  };

    template <> struct NumRegistersDef<Arch::AVX512> { static constexpr std::size_t Value = NumSimdRegistersX86; };

    template <> struct NumBytesDef<Arch::AVX512> { static constexpr std::size_t Value = 64; };

    template <typename LaneT>
    struct LoadAlignedDef<Arch::AVX512, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX512, LaneT>::Type;

      static constexpr auto Value = [](const SimdT* v) {
        return *v;
      };
    };

    template <typename LaneT>
    struct StoreAlignedDef<Arch::AVX512, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX512, LaneT>::Type;

      static constexpr auto Value = [](SimdT* lhs, SimdT rhs) {
        return (*lhs) = rhs;
      };
    };

    template <typename LaneT>
    struct AddDef<Arch::AVX512, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX512, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm512_add_ps(lhs, rhs);
          else
            return _mm512_add_pd(lhs, rhs);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm512_add_epi8(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm512_add_epi16(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm512_add_epi32(lhs, rhs);
          else
            return _mm512_add_epi64(lhs, rhs);
        }
      };
    };

    template <typename LaneT>
    struct SubDef<Arch::AVX512, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX512, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm512_sub_ps(lhs, rhs);
          else
            return _mm512_sub_pd(lhs, rhs);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm512_sub_epi8(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm512_sub_epi16(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm512_sub_epi32(lhs, rhs);
          else
            return _mm512_sub_epi64(lhs, rhs);
        }
      };
    };

    template <typename LaneT>
    struct ZerosDef<Arch::AVX512, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX512, LaneT>::Type;

      static constexpr auto Value = []() {
        return _mm512_setzero_si512();
      };
    };

    template <typename LaneT>
    struct BroadcastDef<Arch::AVX512, LaneT> {
      using SimdT = typename TypeDef<Arch::AVX512, LaneT>::Type;

      static constexpr auto Value = [](LaneT value) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return _mm512_set1_ps(value);
          else
            return _mm512_set1_pd(value);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return _mm512_set1_epi8(value);
          else if constexpr (sizeof(LaneT) == 2)
            return _mm512_set1_epi16(value);
          else if constexpr (sizeof(LaneT) == 4)
            return _mm512_set1_epi32(value);
          else
            return _mm512_set1_epi64(value);
        }
      };
    };

# if defined(USE_VNNI)

    template <typename LaneT> struct TypeDef<Arch::VNNI512, LaneT> : TypeDef<Arch::AVX512, LaneT> {};
    template <> struct NumRegistersDef<Arch::VNNI512> : NumRegistersDef<Arch::AVX512> {};
    template <> struct NumBytesDef<Arch::VNNI512> : NumBytesDef<Arch::AVX512> {};
    template <typename LaneT> struct LoadAlignedDef<Arch::VNNI512, LaneT> : LoadAlignedDef<Arch::AVX512, LaneT> {};
    template <typename LaneT> struct StoreAlignedDef<Arch::VNNI512, LaneT> : StoreAlignedDef<Arch::AVX512, LaneT> {};
    template <typename LaneT> struct AddDef<Arch::VNNI512, LaneT> : AddDef<Arch::AVX512, LaneT> {};
    template <typename LaneT> struct SubDef<Arch::VNNI512, LaneT> : SubDef<Arch::AVX512, LaneT> {};
    template <typename LaneT> struct ZerosDef<Arch::VNNI512, LaneT> : ZerosDef<Arch::AVX512, LaneT> {};
    template <typename LaneT> struct BroadcastDef<Arch::VNNI512, LaneT> : BroadcastDef<Arch::AVX512, LaneT> {};
# endif

#endif

#if defined(USE_NEON)

    template <> struct TypeDef<Arch::NEON, std::uint8_t > { using Type =  uint8x16_t; };
    template <> struct TypeDef<Arch::NEON, std:: int8_t > { using Type =   int8x16_t; };
    template <> struct TypeDef<Arch::NEON, std::uint16_t> { using Type =  uint16x8_t; };
    template <> struct TypeDef<Arch::NEON, std:: int16_t> { using Type =   int16x8_t; };
    template <> struct TypeDef<Arch::NEON, std::uint32_t> { using Type =  uint32x4_t; };
    template <> struct TypeDef<Arch::NEON, std:: int32_t> { using Type =   int32x4_t; };
    template <> struct TypeDef<Arch::NEON, std::uint64_t> { using Type =  uint64x2_t; };
    template <> struct TypeDef<Arch::NEON, std:: int64_t> { using Type =   int64x2_t; };
    template <> struct TypeDef<Arch::NEON,         float> { using Type = float32x4_t; };
    template <> struct TypeDef<Arch::NEON,        double> { using Type = float64x2_t; };

    template <> struct NumRegistersDef<Arch::NEON> { static constexpr std::size_t Value = 16; };

    template <> struct NumBytesDef<Arch::NEON> { static constexpr std::size_t Value = 16; };

    template <typename LaneT>
    struct LoadAlignedDef<Arch::NEON, LaneT> {
      using SimdT = typename TypeDef<Arch::NEON, LaneT>::Type;

      static constexpr auto Value = [](const SimdT* v) {
        return *v;
      };
    };

    template <typename LaneT>
    struct StoreAlignedDef<Arch::NEON, LaneT> {
      using SimdT = typename TypeDef<Arch::NEON, LaneT>::Type;

      static constexpr auto Value = [](SimdT* lhs, SimdT rhs) {
        return (*lhs) = rhs;
      };
    };

    template <typename LaneT>
    struct AddDef<Arch::NEON, LaneT> {
      using SimdT = typename TypeDef<Arch::NEON, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return vaddq_f32(lhs, rhs);
          else
            return vaddq_f64(lhs, rhs);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return vaddq_s8(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 2)
            return vaddq_s16(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 4)
            return vaddq_s32(lhs, rhs);
          else
            return vaddq_s64(lhs, rhs);
        }
      };
    };

    template <typename LaneT>
    struct SubDef<Arch::NEON, LaneT> {
      using SimdT = typename TypeDef<Arch::NEON, LaneT>::Type;

      static constexpr auto Value = [](SimdT lhs, SimdT rhs) {
        if constexpr (std::is_floating_point_v<LaneT>) {
          if constexpr (sizeof(LaneT) == 4)
            return vsubq_f32(lhs, rhs);
          else
            return vsubq_f64(lhs, rhs);
        } else {
          if constexpr (sizeof(LaneT) == 1)
            return vsubq_s8(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 2)
            return vsubq_s16(lhs, rhs);
          else if constexpr (sizeof(LaneT) == 4)
            return vsubq_s32(lhs, rhs);
          else
            return vsubq_s64(lhs, rhs);
        }
      };
    };

    template <typename LaneT>
    struct ZerosDef<Arch::NEON, LaneT> {
      using SimdT = typename TypeDef<Arch::NEON, LaneT>::Type;

      static constexpr auto Value = []() {
        return SimdT{0};
      };
    };

#endif

  }

  template <Arch ArchV>
  struct Traits {
    template <typename LaneT>
    using Type = typename Detail::TypeDef<ArchV, LaneT>::Type;

    static constexpr std::size_t NumRegisters = Detail::NumRegistersDef<ArchV>::Value;

    static constexpr std::size_t NumBytes = Detail::NumBytesDef<ArchV>::Value;

    template <typename LaneT>
    static constexpr std::size_t NumLanes = Detail::NumLanesDef<ArchV, LaneT>::Value;

    template <typename LaneT>
    static constexpr auto LoadAligned = Detail::LoadAlignedDef<ArchV, LaneT>::Value;

    template <typename LaneT>
    static constexpr auto StoreAligned = Detail::StoreAlignedDef<ArchV, LaneT>::Value;

    template <typename LaneT>
    static constexpr auto Add = Detail::AddDef<ArchV, LaneT>::Value;

    template <typename LaneT>
    static constexpr auto Sub = Detail::SubDef<ArchV, LaneT>::Value;

    template <typename LaneT>
    static constexpr auto Zeros = Detail::ZerosDef<ArchV, LaneT>::Value;

    template <typename LaneT>
    static constexpr auto Broadcast = Detail::BroadcastDef<ArchV, LaneT>::Value;
  };

static constexpr Arch AvailableArchs[] = {

#if defined (USE_AVX512) && defined (USE_VNNI)
  Arch::VNNI512,
#endif

#if defined (USE_AVX512)
  Arch::AVX512,
#endif

#if defined (USE_AVX2) && defined (USE_VNNI)
  Arch::VNNI256,
#endif

#if defined (USE_AVX2)
  Arch::AVX2,
#endif

#if defined (USE_SSE41)
  Arch::SSE41,
#endif

#if defined (USE_SSSE3)
  Arch::SSSE3,
#endif

#if defined (USE_SSE2)
  Arch::SSE2,
#endif

#if defined (USE_MMX)
  Arch::MMX,
#endif

#if defined (USE_NEON)
  Arch::NEON,
#endif

  Arch::None

};

  static constexpr Arch BestAvailableArch = AvailableArchs[0];

  namespace Detail {
    template <Arch ArchV>
    static constexpr inline bool GetIsArchAvailable() {
      for (auto arch : AvailableArchs)
        if (arch == ArchV)
          return true;
      return false;
    }
  }

  template <Arch ArchV>
  static constexpr bool IsArchAvailable = Detail::GetIsArchAvailable<ArchV>();

  namespace Detail {
    template <Arch... Archs>
    static constexpr inline Arch GetBestAvailableArchFromSubset() {
      for (auto arch : AvailableArchs)
        if (((Archs == arch) || ...))
          return arch;
      return Arch::None;
    }
  }

  template <Arch... Archs>
  static constexpr Arch BestAvailableArchFromSubset = Detail::GetBestAvailableArchFromSubset<Archs...>();

}

#pragma GCC diagnostic pop

#endif