/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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

  This file is based on original code by Heinz van Saanen and is
  available under the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#if !defined(RKISS_H_INCLUDED)
#define RKISS_H_INCLUDED

#include "types.h"

/// RKISS is our pseudo random number generator (PRNG) used to compute hash keys.
/// George Marsaglia invented the RNG-Kiss-family in the early 90's. This is a
/// specific version that Heinz van Saanen derived from some public domain code
/// by Bob Jenkins. Following the feature list, as tested by Heinz.
///
/// - Quite platform independent
/// - Passes ALL dieharder tests! Here *nix sys-rand() e.g. fails miserably:-)
/// - ~12 times faster than my *nix sys-rand()
/// - ~4 times faster than SSE2-version of Mersenne twister
/// - Average cycle length: ~2^126
/// - 64 bit seed
/// - Return doubles with a full 53 bit mantissa
/// - Thread safe

class RKISS {

  struct S { uint64_t a, b, c, d; } s; // Keep variables always together

  uint64_t rotate(uint64_t x, uint64_t k) const {
    return (x << k) | (x >> (64 - k));
  }

  uint64_t rand64() {

    const uint64_t
      e = s.a - rotate(s.b,  7);
    s.a = s.b ^ rotate(s.c, 13);
    s.b = s.c + rotate(s.d, 37);
    s.c = s.d + e;
    return s.d = e + s.a;
  }

public:
  RKISS(int seed = 73) {

    s.a = 0xf1ea5eed;
    s.b = s.c = s.d = 0xd4e12c77;
    for (int i = 0; i < seed; i++) // Scramble a few rounds
        rand64();
  }

  template<typename T> T rand() { return T(rand64()); }
};

#endif // !defined(RKISS_H_INCLUDED)
