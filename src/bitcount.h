/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef BITCOUNT_H_INCLUDED
#define BITCOUNT_H_INCLUDED

#include <cassert>

#include "types.h"

enum BitCountType {
  CNT_SW_POPCNT,
  CNT_HW_POPCNT
};

/// Determine at compile time the best popcount<> specialization according to
/// whether the platform is 32 or 64 bit, the maximum number of non-zero
/// bits to count and if the hardware popcnt instruction is available.
const BitCountType Full  = HasPopCnt ? CNT_HW_POPCNT : CNT_SW_POPCNT;
const BitCountType Max15 = HasPopCnt ? CNT_HW_POPCNT : CNT_SW_POPCNT;


/// popcount() counts the number of non-zero bits in a bitboard
template<BitCountType> inline int popcount(Bitboard);

template<>
inline int popcount<CNT_SW_POPCNT>(Bitboard b) {

  union { Bitboard bb; uint16_t u[4]; } v = { b };
  extern uint8_t PopCounts16[1 << 16];
  return PopCounts16[v.u[0]] + PopCounts16[v.u[1]] + PopCounts16[v.u[2]] + PopCounts16[v.u[3]];
}

template<>
inline int popcount<CNT_HW_POPCNT>(Bitboard b) {

#ifndef USE_POPCNT

  assert(false);
  return b != 0; // Avoid 'b not used' warning

#elif defined(_MSC_VER) && defined(__INTEL_COMPILER)

  return _mm_popcnt_u64(b);

#elif defined(_MSC_VER)

  return (int)__popcnt64(b);

#else // Assumed gcc or compatible compiler

  return __builtin_popcountll(b);

#endif
}

#endif // #ifndef BITCOUNT_H_INCLUDED
