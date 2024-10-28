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

#ifndef ARM_ARCH_H_INCLUDED
#define ARM_ARCH_H_INCLUDED

#if !defined(__arm__) && !defined(__aarch64__) && __ARM_ARCH_PROFILE != 'A'
#error "Not supported in the current architecture."
#endif

#if __ARM_ARCH >= 8 && !defined(__ARM_64BIT_STATE)
#error "AArch32 state in ARMv8 and above is not supported."
#endif

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "common.h"

#include <arm_acle.h>

#ifdef __ARM_NEON

#include <arm_neon.h>

namespace Stockfish {

template<typename T>
inline int __neon_cnt(T n) {
    static_assert(std::is_integral_v<T> && sizeof(T) <= 8);

    uint8x8_t cnt = vcnt_u8(vcreate_u8(std::uint64_t(n)));

#if __ARM_ARCH >= 8
    return vaddv_u8(cnt);
#else
    return vget_lane_u64(vpaddl_u32(vpaddl_u16(vpaddl_u8(cnt))), 0);
#endif
}

inline void vdotq_s32_v(int32x4_t& acc, int8x16_t in, int8x16_t col) {
#ifdef __ARM_FEATURE_DOTPROD
    acc = vdotq_s32(acc, in, col);
#elif __ARM_ARCH >= 8
    int16x8_t product0 = vmull_s8(vget_low_s8(in), vget_low_s8(col));
    int16x8_t product1 = vmull_high_s8(in, col);
    int16x8_t sum      = vpaddq_s16(product0, product1);
    acc                = vpadalq_s16(acc, sum);
#else
    int16x8_t product0 = vmull_s8(vget_low_s8(in), vget_low_s8(col));
    int16x8_t product1 = vmull_s8(vget_high_s8(in), vget_high_s8(col));
    int16x8_t sum =
      vcombine_s16(vqmovn_s32(vpaddlq_s16(product0)), vqmovn_s32(vpaddlq_s16(product1)));
    acc = vpadalq_s16(acc, sum);
#endif
}

}  // namespace Stockfish

#endif  // __ARM_NEON

namespace Stockfish {

inline constexpr bool ArchImpl::Is64Bit = __ARM_ARCH >= 8;

template<int Hint>
inline void ArchImpl::prefetch([[maybe_unused]] const void* m) {}

template<typename T>
inline unsigned int ArchImpl::popcount(T n) {
    static_assert(std::is_integral_v<T> && sizeof(T) <= 8);

#ifdef __ARM_NEON
    return __neon_cnt(n);
#else
    return __popcount_value(n);
#endif
}

template<typename T>
inline T ArchImpl::pext([[maybe_unused]] T n, [[maybe_unused]] T mask) {
    return 0;
}

// ===========================================================================
// The functions below are used on ARM/AArch64 targets only.
// ===========================================================================

template<typename T>
inline int ctz(T n) {
    static_assert(std::is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8));
    assert(n != 0);

    if constexpr (sizeof(T) == 8)
        return __clzll(__rbitll(std::uint64_t(n)));
    else
        return __clz(__rbit(std::uint32_t(n)));
}

}  // namespace Stockfish

#endif  // ARM_ARCH_H_INCLUDED
