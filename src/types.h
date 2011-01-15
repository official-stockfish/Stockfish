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
*/

#if !defined(TYPES_H_INCLUDED)
#define TYPES_H_INCLUDED

#include <cstdlib>

#if defined(_MSC_VER)

// Disable some silly and noisy warning from MSVC compiler
#pragma warning(disable: 4800) // Forcing value to bool 'true' or 'false'
#pragma warning(disable: 4127) // Conditional expression is constant

// MSVC does not support <inttypes.h>
typedef   signed __int8    int8_t;
typedef unsigned __int8   uint8_t;
typedef   signed __int16  int16_t;
typedef unsigned __int16 uint16_t;
typedef   signed __int32  int32_t;
typedef unsigned __int32 uint32_t;
typedef   signed __int64  int64_t;
typedef unsigned __int64 uint64_t;

#else

#include <inttypes.h>

#endif

// Our hash key and bitboard types
typedef uint64_t Key;
typedef uint64_t Bitboard;

#define Min(x, y) (((x) < (y)) ? (x) : (y))
#define Max(x, y) (((x) < (y)) ? (y) : (x))

////
//// Configuration
////

//// For Linux and OSX configuration is done automatically using Makefile.
//// To get started type "make help".
////
//// For windows part of the configuration is detected automatically, but
//// some switches need to be set manually:
////
//// -DNDEBUG       | Disable debugging mode. Use always.
////
//// -DNO_PREFETCH  | Disable use of prefetch asm-instruction. A must if you want the
////                | executable to run on some very old machines.
////
//// -DUSE_POPCNT   | Add runtime support for use of popcnt asm-instruction.
////                | Works only in 64-bit mode. For compiling requires hardware
////                | with popcnt support. Around 4% speed-up.
////
//// -DOLD_LOCKS    | By default under Windows are used the fast Slim Reader/Writer (SRW)
////                | Locks and Condition Variables: these are not supported by Windows XP
////                | and older, to compile for those platforms you should enable OLD_LOCKS.

// Automatic detection for 64-bit under Windows
#if defined(_WIN64)
#define IS_64BIT
#endif

// Automatic detection for use of bsfq asm-instruction under Windows.
// Works only in 64-bit mode. Does not work with MSVC.
#if defined(_WIN64) && defined(__INTEL_COMPILER)
#define USE_BSFQ
#endif

// Intel header for _mm_popcnt_u64() intrinsic
#if defined(USE_POPCNT) && defined(_MSC_VER) && defined(__INTEL_COMPILER)
#include <nmmintrin.h>
#endif

// Cache line alignment specification
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#define CACHE_LINE_ALIGNMENT __declspec(align(64))
#else
#define CACHE_LINE_ALIGNMENT  __attribute__ ((aligned(64)))
#endif

// Define a __cpuid() function for gcc compilers, for Intel and MSVC
// is already available as an intrinsic.
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
inline void __cpuid(int CPUInfo[4], int InfoType)
{
  int* eax = CPUInfo + 0;
  int* ebx = CPUInfo + 1;
  int* ecx = CPUInfo + 2;
  int* edx = CPUInfo + 3;

  *eax = InfoType;
  *ecx = 0;
  __asm__("cpuid" : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
                  : "0" (*eax), "2" (*ecx));
}
#else
inline void __cpuid(int CPUInfo[4], int)
{
   CPUInfo[0] = CPUInfo[1] = CPUInfo[2] = CPUInfo[3] = 0;
}
#endif

// Define FORCE_INLINE macro to force inlining overriding compiler choice
#if defined(_MSC_VER)
#define FORCE_INLINE  __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE  inline __attribute__((always_inline))
#else
#define FORCE_INLINE  inline
#endif

// Operators used by enum types like Depth, Piece, Square and so on.

#define ENABLE_OPERATORS_ON(T) \
inline T operator+ (const T d1, const T d2) { return T(int(d1) + int(d2)); } \
inline T operator- (const T d1, const T d2) { return T(int(d1) - int(d2)); } \
inline T operator* (int i, const T d) {  return T(i * int(d)); } \
inline T operator* (const T d, int i) {  return T(int(d) * i); } \
inline T operator/ (const T d, int i) { return T(int(d) / i); } \
inline T operator- (const T d) { return T(-int(d)); } \
inline T operator++ (T& d, int) {d = T(int(d) + 1); return d; } \
inline T operator-- (T& d, int) { d = T(int(d) - 1); return d; } \
inline void operator+= (T& d1, const T d2) { d1 = d1 + d2; } \
inline void operator-= (T& d1, const T d2) { d1 = d1 - d2; } \
inline void operator*= (T& d, int i) { d = T(int(d) * i); } \
inline void operator/= (T& d, int i) { d = T(int(d) / i); }

#endif // !defined(TYPES_H_INCLUDED)
