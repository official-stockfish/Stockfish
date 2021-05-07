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

namespace Stockfish::Eval::NNUE {

  // Version of the evaluation file
  constexpr std::uint32_t Version = 0x7AF32F16u;

  // Constant used in evaluation value calculation
  constexpr int OutputScale = 16;
  constexpr int WeightScaleBits = 6;

  // Size of cache line (in bytes)
  constexpr std::size_t CacheLineSize = 64;

  // SIMD width (in bytes)
  #if defined(USE_AVX2)
  constexpr std::size_t SimdWidth = 32;

  #elif defined(USE_SSE2)
  constexpr std::size_t SimdWidth = 16;

  #elif defined(USE_MMX)
  constexpr std::size_t SimdWidth = 8;

  #elif defined(USE_NEON)
  constexpr std::size_t SimdWidth = 16;
  #endif

  constexpr std::size_t MaxSimdWidth = 32;

  // Type of input feature after conversion
  using TransformedFeatureType = std::uint8_t;
  using IndexType = std::uint32_t;

  // Round n up to be a multiple of base
  template <typename IntType>
  constexpr IntType ceil_to_multiple(IntType n, IntType base) {
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

  template <typename IntType>
  inline void write_little_endian(std::ostream& stream, IntType value) {

      std::uint8_t u[sizeof(IntType)];
      typename std::make_unsigned<IntType>::type v = value;

      std::size_t i = 0;
      // if constexpr to silence the warning about shift by 8
      if constexpr (sizeof(IntType) > 1) {
        for (; i + 1 < sizeof(IntType); ++i) {
            u[i] = v;
            v >>= 8;
        }
      }
      u[i] = v;

      stream.write(reinterpret_cast<char*>(u), sizeof(IntType));
  }
}  // namespace Stockfish::Eval::NNUE

#endif // #ifndef NNUE_COMMON_H_INCLUDED
