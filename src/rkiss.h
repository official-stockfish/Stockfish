/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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

 ** A small "keep it simple and stupid" RNG with some fancy merits:
 **
 ** Quite platform independent
 ** Passes ALL dieharder tests! Here *nix sys-rand() e.g. fails miserably:-)
 ** ~12 times faster than my *nix sys-rand()
 ** ~4 times faster than SSE2-version of Mersenne twister
 ** Average cycle length: ~2^126
 ** 64 bit seed
 ** Return doubles with a full 53 bit mantissa
 ** Thread safe
 **
 ** (c) Heinz van Saanen

*/

#if !defined(RKISS_H_INCLUDED)
#define RKISS_H_INCLUDED


////
//// Includes
////

#include "types.h"


////
//// Types
////

class RKISS {

  // Keep variables always together
  struct S { uint64_t a, b, c, d; } s;

  // Return 64 bit unsigned integer in between [0,2^64-1]
  uint64_t rand64() {

      const uint64_t
        e = s.a - ((s.b <<  7) | (s.b >> 57));
      s.a = s.b ^ ((s.c << 13) | (s.c >> 51));
      s.b = s.c + ((s.d << 37) | (s.d >> 27));
      s.c = s.d + e;
      return s.d = e + s.a;
  }

  // Init seed and scramble a few rounds
  void raninit() {

      s.a = 0xf1ea5eed;
      s.b = s.c = s.d = 0xd4e12c77;
      for (int i = 0; i < 73; i++)
          rand64();
  }

public:
  RKISS() { raninit(); }
  template<typename T> T rand() { return T(rand64()); }
};

#endif // !defined(RKISS_H_INCLUDED)
