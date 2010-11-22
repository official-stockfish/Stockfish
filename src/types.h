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

#if !defined(_MSC_VER)

#include <inttypes.h>

#else

// Disable some silly and noisy warning from MSVC compiler
#pragma warning(disable: 4800) // Forcing value to bool 'true' or 'false'
#pragma warning(disable: 4127) // Conditional expression is constant

typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;

typedef __int16 int16_t;
typedef __int64 int64_t;

#endif // !defined(_MSC_VER)

// Hash keys
typedef uint64_t Key;

// Bitboard type
typedef uint64_t Bitboard;

#include <cstdlib>

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

// Automatic detection for 64-bit under Windows
#if defined(_WIN64)
#define IS_64BIT
#endif

// Automatic detection for use of bsfq asm-instruction under Windows.
// Works only in 64-bit mode. Does not work with MSVC.
#if defined(_WIN64) && defined(__INTEL_COMPILER)
#define USE_BSFQ
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


// Templetized operators used by enum types like Depth, Piece, Square and so on.
// We don't want to write the same inline for each different enum. Note that we
// pass by value to silence scaring warnings when using volatiles.
// Because these templates override common operators and are included in all the
// files, there is a possibility that the compiler silently performs some unwanted
// overrides. To avoid possible very nasty bugs the templates are disabled by default
// and must be enabled for each type on a case by case base. The enabling trick
// uses template specialization, namely we just declare following struct.
template<typename T> struct TempletizedOperator;

// Then to enable the enum type we use following macro that defines a specialization
// of TempletizedOperator for the given enum T. Here is defined typedef Not_Enabled.
// Name of typedef is chosen to produce somewhat informative compile error messages.
#define ENABLE_OPERATORS_ON(T)  \
        template<> struct TempletizedOperator<T> { typedef T Not_Enabled; }

// Finally we use macro OK(T) to check if type T is enabled. The macro simply
// tries to use Not_Enabled, if was not previously defined a compile error occurs.
// The check is done fully at compile time and there is zero overhead at runtime.
#define OK(T) typedef typename TempletizedOperator<T>::Not_Enabled Type

template<typename T>
inline T operator+ (const T d1, const T d2) { OK(T); return T(int(d1) + int(d2)); }

template<typename T>
inline T operator- (const T d1, const T d2) { OK(T); return T(int(d1) - int(d2)); }

template<typename T>
inline T operator* (int i, const T d) { OK(T); return T(i * int(d)); }

template<typename T>
inline T operator* (const T d, int i) { OK(T); return T(int(d) * i); }

template<typename T>
inline T operator/ (const T d, int i) { OK(T); return T(int(d) / i); }

template<typename T>
inline T operator- (const T d) { OK(T); return T(-int(d)); }

template<typename T>
inline T operator++ (T& d, int) { OK(T); d = T(int(d) + 1); return d; }

template<typename T>
inline T operator-- (T& d, int) { OK(T); d = T(int(d) - 1); return d; }

template<typename T>
inline void operator+= (T& d1, const T d2) { OK(T); d1 = d1 + d2; }

template<typename T>
inline void operator-= (T& d1, const T d2) { OK(T); d1 = d1 - d2; }

template<typename T>
inline void operator*= (T& d, int i) { OK(T); d = T(int(d) * i); }

template<typename T>
inline void operator/= (T& d, int i) { OK(T); d = T(int(d) / i); }

#undef OK

#endif // !defined(TYPES_H_INCLUDED)
