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

#ifndef _RKISS_H_
#define _RKISS_H_

/** Includes **/
#include <cstdlib>    // srand(), rand()
#include <ctime>      // time()
#include "types.h"    // (u)int8_t .. (u)int64_t


/** Random class **/
class RKISS {

private:
	// Keep variables always together
	struct S { uint64_t a; uint64_t b; uint64_t c; uint64_t d; } s;

	// Init seed and scramble a few rounds
	void raninit ( uint64_t seed ) {
		s.a = 0xf1ea5eed; s.b = s.c = s.d = seed;
		for ( uint64_t i=0; i<8; i++ ) rand64();
	}

public:
	// Instance seed random or implicite
	RKISS() { ::srand ( (uint32_t)time(NULL) ); raninit ( (uint64_t)::rand() ); }
	// RKISS( uint64_t s ) { raninit ( s ); }

	// (Re)init seed
	// void init ( uint64_t seed ) { raninit ( seed ); }

	// Return 32 bit unsigned integer in between [0,2^32-1]
	uint32_t rand32 () { return (uint32_t) rand64 (); }

	// Return 64 bit unsigned integer in between [0,2^64-1]
	uint64_t rand64 () {
		const uint64_t e = s.a - ((s.b<<7) | (s.b>>57));
		s.a = s.b ^ ((s.c<<13) | (s.c>>51));
		s.b = s.c + ((s.d<<37) | (s.d>>27));
		s.c = s.d + e;
		return s.d = e + s.a;
	}

	// Return double in between [0,1). Keep full 53 bit mantissa
	// double frand () { return (int64_t)(rand64()>>11) * (1.0/(67108864.0*134217728.0)); }
};

// _RKISS_H_
#endif
