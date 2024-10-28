/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#ifndef PPC_ARCH_H_INCLUDED
#define PPC_ARCH_H_INCLUDED

#if !defined(__PPC__)
#error "Not supported in the current architecture."
#endif

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "common.h"

#if STOCKFISH_COMPILER == STOCKFISH_COMPILER_GCC
#include <ppu_intrinsics.h>
#endif

namespace Stockfish {

#ifdef __PPC64__
inline constexpr bool ArchImpl::Is64Bit = true;
#else
inline constexpr bool ArchImpl::Is64Bit = false;
#endif

template<int Hint>
inline void ArchImpl::prefetch(const void* m) {
    __dcbt(m);
}

template<typename T>
inline unsigned int ArchImpl::popcount(T n) {
    static_assert(std::is_integral_v<T> && sizeof(T) <= 8);

    // This will generate POPCNTD instruction on PWR7 and later.
    return __popcount_value(n);
}

template<typename T>
inline T ArchImpl::pext([[maybe_unused]] T n, [[maybe_unused]] T mask) {
    return 0;
}

}  // namespace Stockfish

#endif  // PPC_ARCH_H_INCLUDED
