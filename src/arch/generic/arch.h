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

#ifndef GENERIC_ARCH_H_INCLUDED
#define GENERIC_ARCH_H_INCLUDED

#include <cstdint>
#include <type_traits>

#include "common.h"

namespace Stockfish {

// There is no practical way to detect the register width, so we assume that
// it is always 64-bit if address size is 64-bit.
inline constexpr bool ArchImpl::Is64Bit = sizeof(void*) == 8;

inline constexpr bool ArchImpl::UsePEXT = false;

template<int Hint>
inline void ArchImpl::prefetch([[maybe_unused]] const void* m) {}

template<typename T>
inline unsigned int ArchImpl::popcount(T n) {
    static_assert(std::is_integral_v<T> && sizeof(T) <= 8);
    return __popcount_value(n);
}

template<typename T>
inline T ArchImpl::pext([[maybe_unused]] T n, [[maybe_unused]] T mask) {
    return 0;
}

}  // namespace Stockfish

#endif  // GENERIC_ARCH_H_INCLUDED
