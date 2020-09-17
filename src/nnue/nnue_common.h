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

// Constants used in NNUE evaluation function

#ifndef NNUE_COMMON_H_INCLUDED
#define NNUE_COMMON_H_INCLUDED

#include <cstring>
#include <iostream>

#if defined(USE_AVX2)
#include <immintrin.h>

#elif defined(USE_SSE41)
#include <smmintrin.h>

#elif defined(USE_SSSE3)
#include <tmmintrin.h>

#elif defined(USE_SSE2)
#include <emmintrin.h>

#elif defined(USE_MMX)
#include <mmintrin.h>

#elif defined(USE_NEON)
#include <arm_neon.h>
#endif

// HACK: Use _mm256_loadu_si256() instead of _mm256_load_si256. Otherwise a binary
//       compiled with older g++ crashes because the output memory is not aligned
//       even though alignas is specified.
#if defined(USE_AVX2)
#if defined(__GNUC__ ) && (__GNUC__ < 9) && defined(_WIN32) && !defined(__clang__)
#define _mm256_loadA_si256  _mm256_loadu_si256
#define _mm256_storeA_si256 _mm256_storeu_si256
#else
#define _mm256_loadA_si256  _mm256_load_si256
#define _mm256_storeA_si256 _mm256_store_si256
#endif
#endif

#if defined(USE_AVX512)
#if defined(__GNUC__ ) && (__GNUC__ < 9) && defined(_WIN32) && !defined(__clang__)
#define _mm512_loadA_si512   _mm512_loadu_si512
#define _mm512_storeA_si512  _mm512_storeu_si512
#else
#define _mm512_loadA_si512   _mm512_load_si512
#define _mm512_storeA_si512  _mm512_store_si512
#endif
#endif

namespace Eval::NNUE {

  // Version of the evaluation file
  constexpr std::uint32_t kVersion = 0x7AF32F16u;

  // Constant used in evaluation value calculation
  constexpr int FV_SCALE = 16;
  constexpr int kWeightScaleBits = 6;

  // Size of cache line (in bytes)
  constexpr std::size_t kCacheLineSize = 64;

  // SIMD width (in bytes)
  #if defined(USE_AVX2)
  constexpr std::size_t kSimdWidth = 32;

  #elif defined(USE_SSE2)
  constexpr std::size_t kSimdWidth = 16;

  #elif defined(USE_MMX)
  constexpr std::size_t kSimdWidth = 8;

  #elif defined(USE_NEON)
  constexpr std::size_t kSimdWidth = 16;
  #endif

  constexpr std::size_t kMaxSimdWidth = 32;

  // unique number for each piece type on each square
  enum {
    PS_NONE     =  0,
    PS_W_PAWN   =  1,
    PS_B_PAWN   =  1 * SQUARE_NB + 1,
    PS_W_KNIGHT =  2 * SQUARE_NB + 1,
    PS_B_KNIGHT =  3 * SQUARE_NB + 1,
    PS_W_BISHOP =  4 * SQUARE_NB + 1,
    PS_B_BISHOP =  5 * SQUARE_NB + 1,
    PS_W_ROOK   =  6 * SQUARE_NB + 1,
    PS_B_ROOK   =  7 * SQUARE_NB + 1,
    PS_W_QUEEN  =  8 * SQUARE_NB + 1,
    PS_B_QUEEN  =  9 * SQUARE_NB + 1,
    PS_W_KING   = 10 * SQUARE_NB + 1,
    PS_END      = PS_W_KING, // pieces without kings (pawns included)
    PS_B_KING   = 11 * SQUARE_NB + 1,
    PS_END2     = 12 * SQUARE_NB + 1
  };

  extern uint32_t kpp_board_index[PIECE_NB][COLOR_NB];

  // Type of input feature after conversion
  using TransformedFeatureType = std::uint8_t;
  using IndexType = std::uint32_t;

  // Round n up to be a multiple of base
  template <typename IntType>
  constexpr IntType CeilToMultiple(IntType n, IntType base) {
      return (n + base - 1) / base * base;
  }

  // read_little_endian() is our utility to read an integer (signed or unsigned, any size)
  // from a stream in little-endian order. We swap the byte order after the read if
  // necessary to return a result with the byte ordering of the compiling machine.
  template <typename IntType>
  inline IntType read_little_endian(std::istream& stream) {

      IntType result;
      std::uint8_t u[sizeof(IntType)];
      typename std::make_unsigned<IntType>::type v = 0;

      stream.read(reinterpret_cast<char*>(u), sizeof(IntType));
      for (std::size_t i = 0; i < sizeof(IntType); ++i)
          v = (v << 8) | u[sizeof(IntType) - i - 1];

      std::memcpy(&result, &v, sizeof(IntType));
      return result;
  }

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_COMMON_H_INCLUDED
