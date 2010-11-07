/** *********************************************************************** **
 ** A small "keep it simple and stupid" RNG with some fancy merits:
 **
 ** Quite platform independent
 ** Passes ALL dieharder tests! Here *nix sys-rand() e.g. fails miserably:-)
 ** ~12 times faster than my *nix sys-rand()
 ** ~4 times faster than SSE2-version of Mersenne twister
 ** Average cycle length: ~2^126
 ** 64 bit seed
 ** Return doubles with a full 53 bit mantissa
 ** Thread save
 **
 ** (c) Heinz van Saanen

  This file is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This file is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

 ** *********************************************************************** **/

#if !defined(RKISS_H_INCLUDED)
#define RKISS_H_INCLUDED


////
//// Includes
////

#include <cstdlib>
#include <ctime>

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
	void raninit(uint64_t seed) {

		s.a = 0xf1ea5eed;
        s.b = s.c = s.d = seed;
		for (uint64_t i = 0; i < 8; i++)
            rand64();
	}

public:
	// Instance seed random or implicite
	RKISS() { ::srand(uint32_t(time(NULL))); raninit(uint64_t(::rand())); }

	// Return random number of type T (must be castable from uint64_t)
    template<typename T>
	T rand() { return T(rand64()); }
};

#endif // !defined(RKISS_H_INCLUDED)
