/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#ifndef RKISS_H_INCLUDED
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

  uint64_t a, b, c, d;

  uint64_t rotate_L(uint64_t x, unsigned k) const {
    return (x << k) | (x >> (64 - k));
  }

  uint64_t rand64() {

    const uint64_t e = a - rotate_L(b,  7);
    a = b ^ rotate_L(c, 13);
    b = c + rotate_L(d, 37);
    c = d + e;
    return d = e + a;
  }

public:
  RKISS(int seed = 73) {

    a = 0xF1EA5EED, b = c = d = 0xD4E12C77;

    for (int i = 0; i < seed; ++i) // Scramble a few rounds
        rand64();
  }

  template<typename T> T rand() { return T(rand64()); }

  /// Special generator used to fast init magic numbers. Here the
  /// trick is to rotate the randoms of a given quantity 's' known
  /// to be optimal to quickly find a good magic candidate.
  template<typename T> T magic_rand(int s) {
    return rotate_L(rotate_L(rand<T>(), (s >> 0) & 0x3F) & rand<T>()
                                      , (s >> 6) & 0x3F) & rand<T>();
  }
};

#endif // #ifndef RKISS_H_INCLUDED
