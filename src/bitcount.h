/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

#if !defined(BITCOUNT_H_INCLUDED)
#define BITCOUNT_H_INCLUDED

#include <cassert>
#include "types.h"

enum BitCountType {
  CNT_64,
  CNT_64_MAX15,
  CNT_32,
  CNT_32_MAX15,
  CNT_HW_POPCNT
};

/// Determine at compile time the best popcount<> specialization according if
/// platform is 32 or 64 bits, to the maximum number of nonzero bits to count
/// and if hardware popcnt instruction is available.
const BitCountType Full  = HasPopCnt ? CNT_HW_POPCNT : Is64Bit ? CNT_64 : CNT_32;
const BitCountType Max15 = HasPopCnt ? CNT_HW_POPCNT : Is64Bit ? CNT_64_MAX15 : CNT_32_MAX15;


/// popcount() counts the number of nonzero bits in a bitboard
template<BitCountType> inline int popcount(Bitboard);

template<>
inline int popcount<CNT_64>(Bitboard b) {
  b -=  (b >> 1) & 0x5555555555555555ULL;
  b  = ((b >> 2) & 0x3333333333333333ULL) + (b & 0x3333333333333333ULL);
  b  = ((b >> 4) + b) & 0x0F0F0F0F0F0F0F0FULL;
  return (b * 0x0101010101010101ULL) >> 56;
}

template<>
inline int popcount<CNT_64_MAX15>(Bitboard b) {
  b -=  (b >> 1) & 0x5555555555555555ULL;
  b  = ((b >> 2) & 0x3333333333333333ULL) + (b & 0x3333333333333333ULL);
  return (b * 0x1111111111111111ULL) >> 60;
}

template<>
inline int popcount<CNT_32>(Bitboard b) {
  unsigned w = unsigned(b >> 32), v = unsigned(b);
  v -=  (v >> 1) & 0x55555555; // 0-2 in 2 bits
  w -=  (w >> 1) & 0x55555555;
  v  = ((v >> 2) & 0x33333333) + (v & 0x33333333); // 0-4 in 4 bits
  w  = ((w >> 2) & 0x33333333) + (w & 0x33333333);
  v  = ((v >> 4) + v + (w >> 4) + w) & 0x0F0F0F0F;
  return (v * 0x01010101) >> 24;
}

template<>
inline int popcount<CNT_32_MAX15>(Bitboard b) {
  unsigned w = unsigned(b >> 32), v = unsigned(b);
  v -=  (v >> 1) & 0x55555555; // 0-2 in 2 bits
  w -=  (w >> 1) & 0x55555555;
  v  = ((v >> 2) & 0x33333333) + (v & 0x33333333); // 0-4 in 4 bits
  w  = ((w >> 2) & 0x33333333) + (w & 0x33333333);
  return ((v + w) * 0x11111111) >> 28;
}

template<>
inline int popcount<CNT_HW_POPCNT>(Bitboard b) {

#if !defined(USE_POPCNT)

  assert(false);
  return b != 0; // Avoid 'b not used' warning

#elif defined(_MSC_VER) && defined(__INTEL_COMPILER)

  return _mm_popcnt_u64(b);

#elif defined(_MSC_VER)

  return (int)__popcnt64(b);

#else

  __asm__("popcnt %1, %0" : "=r" (b) : "r" (b));
  return b;

#endif
}

#endif // !defined(BITCOUNT_H_INCLUDED)
