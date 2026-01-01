/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#ifndef NNUE_SIMD_H_INCLUDED
#define NNUE_SIMD_H_INCLUDED

#if defined(USE_AVX2)
    #include <immintrin.h>

#elif defined(USE_SSE41)
    #include <smmintrin.h>

#elif defined(USE_SSSE3)
    #include <tmmintrin.h>

#elif defined(USE_SSE2)
    #include <emmintrin.h>

#elif defined(USE_NEON)
    #include <arm_neon.h>
#endif

#include "../types.h"
#include "nnue_common.h"

namespace Stockfish::Eval::NNUE::SIMD {

// If vector instructions are enabled, we update and refresh the
// accumulator tile by tile such that each tile fits in the CPU's
// vector registers.
#define VECTOR

#ifdef USE_AVX512
using vec_t      = __m512i;
using vec_i8_t   = __m256i;
using vec128_t   = __m128i;
using psqt_vec_t = __m256i;
using vec_uint_t = __m512i;
    #define vec_load(a) _mm512_load_si512(a)
    #define vec_store(a, b) _mm512_store_si512(a, b)
    #define vec_convert_8_16(a) _mm512_cvtepi8_epi16(a)
    #define vec_add_16(a, b) _mm512_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm512_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm512_mulhi_epi16(a, b)
    #define vec_zero() _mm512_setzero_epi32()
    #define vec_set_16(a) _mm512_set1_epi16(a)
    #define vec_max_16(a, b) _mm512_max_epi16(a, b)
    #define vec_min_16(a, b) _mm512_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm512_slli_epi16(a, b)
    // Inverse permuted at load time
    #define vec_packus_16(a, b) _mm512_packus_epi16(a, b)
    #define vec_load_psqt(a) _mm256_load_si256(a)
    #define vec_store_psqt(a, b) _mm256_store_si256(a, b)
    #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
    #define vec_zero_psqt() _mm256_setzero_si256()

    #ifdef USE_SSSE3
        #define vec_nnz(a) _mm512_cmpgt_epi32_mask(a, _mm512_setzero_si512())
    #endif

    #define vec128_zero _mm_setzero_si128()
    #define vec128_set_16(a) _mm_set1_epi16(a)
    #define vec128_load(a) _mm_load_si128(a)
    #define vec128_storeu(a, b) _mm_storeu_si128(a, b)
    #define vec128_add(a, b) _mm_add_epi16(a, b)
    #define NumRegistersSIMD 16
    #define MaxChunkSize 64

#elif USE_AVX2
using vec_t      = __m256i;
using vec_i8_t   = __m128i;
using vec128_t   = __m128i;
using psqt_vec_t = __m256i;
using vec_uint_t = __m256i;
    #define vec_load(a) _mm256_load_si256(a)
    #define vec_store(a, b) _mm256_store_si256(a, b)
    #define vec_convert_8_16(a) _mm256_cvtepi8_epi16(a)
    #define vec_add_16(a, b) _mm256_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm256_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm256_mulhi_epi16(a, b)
    #define vec_zero() _mm256_setzero_si256()
    #define vec_set_16(a) _mm256_set1_epi16(a)
    #define vec_max_16(a, b) _mm256_max_epi16(a, b)
    #define vec_min_16(a, b) _mm256_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm256_slli_epi16(a, b)
    // Inverse permuted at load time
    #define vec_packus_16(a, b) _mm256_packus_epi16(a, b)
    #define vec_load_psqt(a) _mm256_load_si256(a)
    #define vec_store_psqt(a, b) _mm256_store_si256(a, b)
    #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
    #define vec_zero_psqt() _mm256_setzero_si256()

    #ifdef USE_SSSE3
        #if defined(USE_VNNI) && !defined(USE_AVXVNNI)
            #define vec_nnz(a) _mm256_cmpgt_epi32_mask(a, _mm256_setzero_si256())
        #else
            #define vec_nnz(a) \
                _mm256_movemask_ps( \
                  _mm256_castsi256_ps(_mm256_cmpgt_epi32(a, _mm256_setzero_si256())))
        #endif
    #endif

    #define vec128_zero _mm_setzero_si128()
    #define vec128_set_16(a) _mm_set1_epi16(a)
    #define vec128_load(a) _mm_load_si128(a)
    #define vec128_storeu(a, b) _mm_storeu_si128(a, b)
    #define vec128_add(a, b) _mm_add_epi16(a, b)

    #define NumRegistersSIMD 12
    #define MaxChunkSize 32

#elif USE_SSE2
using vec_t      = __m128i;
using vec_i8_t   = std::uint64_t;  // for the correct size -- will be loaded into an xmm reg
using vec128_t   = __m128i;
using psqt_vec_t = __m128i;
using vec_uint_t = __m128i;
    #define vec_load(a) (*(a))
    #define vec_store(a, b) *(a) = (b)
    #define vec_add_16(a, b) _mm_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm_mulhi_epi16(a, b)
    #define vec_zero() _mm_setzero_si128()
    #define vec_set_16(a) _mm_set1_epi16(a)
    #define vec_max_16(a, b) _mm_max_epi16(a, b)
    #define vec_min_16(a, b) _mm_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm_slli_epi16(a, b)
    #define vec_packus_16(a, b) _mm_packus_epi16(a, b)
    #define vec_load_psqt(a) (*(a))
    #define vec_store_psqt(a, b) *(a) = (b)
    #define vec_add_psqt_32(a, b) _mm_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm_sub_epi32(a, b)
    #define vec_zero_psqt() _mm_setzero_si128()

    #ifdef USE_SSSE3
        #define vec_nnz(a) \
            _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpgt_epi32(a, _mm_setzero_si128())))
    #endif

    #ifdef __i386__
inline __m128i _mm_cvtsi64_si128(int64_t val) {
    return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&val));
}
    #endif

    #ifdef USE_SSE41
        #define vec_convert_8_16(a) _mm_cvtepi8_epi16(_mm_cvtsi64_si128(static_cast<int64_t>(a)))
    #else
// Credit: Yoshie2000
inline __m128i vec_convert_8_16(uint64_t x) {
    __m128i v8   = _mm_cvtsi64_si128(static_cast<int64_t>(x));
    __m128i sign = _mm_cmpgt_epi8(_mm_setzero_si128(), v8);
    return _mm_unpacklo_epi8(v8, sign);
}
    #endif

    #define vec128_zero _mm_setzero_si128()
    #define vec128_set_16(a) _mm_set1_epi16(a)
    #define vec128_load(a) _mm_load_si128(a)
    #define vec128_storeu(a, b) _mm_storeu_si128(a, b)
    #define vec128_add(a, b) _mm_add_epi16(a, b)

    #define NumRegistersSIMD (Is64Bit ? 12 : 6)
    #define MaxChunkSize 16

#elif USE_NEON
using vec_t      = int16x8_t;
using vec_i8_t   = int8x16_t;
using psqt_vec_t = int32x4_t;
using vec128_t   = uint16x8_t;
using vec_uint_t = uint32x4_t;
    #define vec_load(a) (*(a))
    #define vec_store(a, b) *(a) = (b)
    #define vec_add_16(a, b) vaddq_s16(a, b)
    #define vec_sub_16(a, b) vsubq_s16(a, b)
    #define vec_mulhi_16(a, b) vqdmulhq_s16(a, b)
    #define vec_zero() vec_t{0}
    #define vec_set_16(a) vdupq_n_s16(a)
    #define vec_max_16(a, b) vmaxq_s16(a, b)
    #define vec_min_16(a, b) vminq_s16(a, b)
    #define vec_slli_16(a, b) vshlq_s16(a, vec_set_16(b))
    #define vec_packus_16(a, b) reinterpret_cast<vec_t>(vcombine_u8(vqmovun_s16(a), vqmovun_s16(b)))
    #define vec_load_psqt(a) (*(a))
    #define vec_store_psqt(a, b) *(a) = (b)
    #define vec_add_psqt_32(a, b) vaddq_s32(a, b)
    #define vec_sub_psqt_32(a, b) vsubq_s32(a, b)
    #define vec_zero_psqt() psqt_vec_t{0}

static constexpr std::uint32_t Mask[4] = {1, 2, 4, 8};
    #define vec_nnz(a) vaddvq_u32(vandq_u32(vtstq_u32(a, a), vld1q_u32(Mask)))
    #define vec128_zero vdupq_n_u16(0)
    #define vec128_set_16(a) vdupq_n_u16(a)
    #define vec128_load(a) vld1q_u16(reinterpret_cast<const std::uint16_t*>(a))
    #define vec128_storeu(a, b) vst1q_u16(reinterpret_cast<std::uint16_t*>(a), b)
    #define vec128_add(a, b) vaddq_u16(a, b)

    #define NumRegistersSIMD 16
    #define MaxChunkSize 16

    #ifndef __aarch64__
// Single instruction doesn't exist on 32-bit ARM
inline int8x16_t vmovl_high_s8(int8x16_t val) { return vmovl_s8(vget_high_s8(val)); }
    #endif

#else
    #undef VECTOR

#endif

struct Vec16Wrapper {
#ifdef VECTOR
    using type = vec_t;
    static type add(const type& lhs, const type& rhs) { return vec_add_16(lhs, rhs); }
    static type sub(const type& lhs, const type& rhs) { return vec_sub_16(lhs, rhs); }
#else
    using type = BiasType;
    static type add(const type& lhs, const type& rhs) { return lhs + rhs; }
    static type sub(const type& lhs, const type& rhs) { return lhs - rhs; }
#endif
};

struct Vec32Wrapper {
#ifdef VECTOR
    using type = psqt_vec_t;
    static type add(const type& lhs, const type& rhs) { return vec_add_psqt_32(lhs, rhs); }
    static type sub(const type& lhs, const type& rhs) { return vec_sub_psqt_32(lhs, rhs); }
#else
    using type = PSQTWeightType;
    static type add(const type& lhs, const type& rhs) { return lhs + rhs; }
    static type sub(const type& lhs, const type& rhs) { return lhs - rhs; }
#endif
};

enum UpdateOperation {
    Add,
    Sub
};

template<typename VecWrapper,
         UpdateOperation... ops,
         std::enable_if_t<sizeof...(ops) == 0, bool> = true>
typename VecWrapper::type fused(const typename VecWrapper::type& in) {
    return in;
}

template<typename VecWrapper,
         UpdateOperation update_op,
         UpdateOperation... ops,
         typename T,
         typename... Ts,
         std::enable_if_t<is_all_same_v<typename VecWrapper::type, T, Ts...>, bool> = true,
         std::enable_if_t<sizeof...(ops) == sizeof...(Ts), bool>                    = true>
typename VecWrapper::type
fused(const typename VecWrapper::type& in, const T& operand, const Ts&... operands) {
    switch (update_op)
    {
    case Add :
        return fused<VecWrapper, ops...>(VecWrapper::add(in, operand), operands...);
    case Sub :
        return fused<VecWrapper, ops...>(VecWrapper::sub(in, operand), operands...);
    default :
        static_assert(update_op == Add || update_op == Sub,
                      "Only Add and Sub are currently supported.");
        return typename VecWrapper::type();
    }
}

#if defined(USE_AVX512)

[[maybe_unused]] static int m512_hadd(__m512i sum, int bias) {
    return _mm512_reduce_add_epi32(sum) + bias;
}

[[maybe_unused]] static void m512_add_dpbusd_epi32(__m512i& acc, __m512i a, __m512i b) {

    #if defined(USE_VNNI)
    acc = _mm512_dpbusd_epi32(acc, a, b);
    #else
    __m512i product0 = _mm512_maddubs_epi16(a, b);
    product0         = _mm512_madd_epi16(product0, _mm512_set1_epi16(1));
    acc              = _mm512_add_epi32(acc, product0);
    #endif
}

#endif

#if defined(USE_AVX2)

[[maybe_unused]] static int m256_hadd(__m256i sum, int bias) {
    __m128i sum128 = _mm_add_epi32(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1));
    sum128         = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_BADC));
    sum128         = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_CDAB));
    return _mm_cvtsi128_si32(sum128) + bias;
}

[[maybe_unused]] static void m256_add_dpbusd_epi32(__m256i& acc, __m256i a, __m256i b) {

    #if defined(USE_VNNI)
    acc = _mm256_dpbusd_epi32(acc, a, b);
    #else
    __m256i product0 = _mm256_maddubs_epi16(a, b);
    product0         = _mm256_madd_epi16(product0, _mm256_set1_epi16(1));
    acc              = _mm256_add_epi32(acc, product0);
    #endif
}

#endif

#if defined(USE_SSSE3)

[[maybe_unused]] static int m128_hadd(__m128i sum, int bias) {
    sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0x4E));  //_MM_PERM_BADC
    sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0xB1));  //_MM_PERM_CDAB
    return _mm_cvtsi128_si32(sum) + bias;
}

[[maybe_unused]] static void m128_add_dpbusd_epi32(__m128i& acc, __m128i a, __m128i b) {

    __m128i product0 = _mm_maddubs_epi16(a, b);
    product0         = _mm_madd_epi16(product0, _mm_set1_epi16(1));
    acc              = _mm_add_epi32(acc, product0);
}

#endif

#if defined(USE_NEON_DOTPROD)

[[maybe_unused]] static void
dotprod_m128_add_dpbusd_epi32(int32x4_t& acc, int8x16_t a, int8x16_t b) {

    acc = vdotq_s32(acc, a, b);
}
#endif

#if defined(USE_NEON)

[[maybe_unused]] static int neon_m128_reduce_add_epi32(int32x4_t s) {
    #if USE_NEON >= 8
    return vaddvq_s32(s);
    #else
    return s[0] + s[1] + s[2] + s[3];
    #endif
}

[[maybe_unused]] static int neon_m128_hadd(int32x4_t sum, int bias) {
    return neon_m128_reduce_add_epi32(sum) + bias;
}

#endif

#if USE_NEON >= 8
[[maybe_unused]] static void neon_m128_add_dpbusd_epi32(int32x4_t& acc, int8x16_t a, int8x16_t b) {

    int16x8_t product0 = vmull_s8(vget_low_s8(a), vget_low_s8(b));
    int16x8_t product1 = vmull_high_s8(a, b);
    int16x8_t sum      = vpaddq_s16(product0, product1);
    acc                = vpadalq_s16(acc, sum);
}
#endif


// Compute optimal SIMD register count for feature transformer accumulation.
template<IndexType TransformedFeatureWidth, IndexType HalfDimensions, IndexType PSQTBuckets>
class SIMDTiling {
#ifdef VECTOR
        // We use __m* types as template arguments, which causes GCC to emit warnings
        // about losing some attribute information. This is irrelevant to us as we
        // only take their size, so the following pragma are harmless.
    #if defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wignored-attributes"
    #endif

    template<typename SIMDRegisterType, typename LaneType, int NumLanes, int MaxRegisters>
    static constexpr int BestRegisterCount() {
        constexpr std::size_t RegisterSize = sizeof(SIMDRegisterType);
        constexpr std::size_t LaneSize     = sizeof(LaneType);

        static_assert(RegisterSize >= LaneSize);
        static_assert(MaxRegisters <= NumRegistersSIMD);
        static_assert(MaxRegisters > 0);
        static_assert(NumRegistersSIMD > 0);
        static_assert(RegisterSize % LaneSize == 0);
        static_assert((NumLanes * LaneSize) % RegisterSize == 0);

        const int ideal = (NumLanes * LaneSize) / RegisterSize;
        if (ideal <= MaxRegisters)
            return ideal;

        // Look for the largest divisor of the ideal register count that is smaller than MaxRegisters
        for (int divisor = MaxRegisters; divisor > 1; --divisor)
            if (ideal % divisor == 0)
                return divisor;

        return 1;
    }

    #if defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif

   public:
    static constexpr int NumRegs =
      BestRegisterCount<vec_t, WeightType, TransformedFeatureWidth, NumRegistersSIMD>();
    static constexpr int NumPsqtRegs =
      BestRegisterCount<psqt_vec_t, PSQTWeightType, PSQTBuckets, NumRegistersSIMD>();

    static constexpr IndexType TileHeight     = NumRegs * sizeof(vec_t) / 2;
    static constexpr IndexType PsqtTileHeight = NumPsqtRegs * sizeof(psqt_vec_t) / 4;

    static_assert(HalfDimensions % TileHeight == 0, "TileHeight must divide HalfDimensions");
    static_assert(PSQTBuckets % PsqtTileHeight == 0, "PsqtTileHeight must divide PSQTBuckets");
#endif
};
}

#endif
