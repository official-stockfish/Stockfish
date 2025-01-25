#ifndef SIMD_OPS_H_INCLUDED
#define SIMD_OPS_H_INCLUDED

#include <immintrin.h>

namespace Stockfish {

struct SIMDHelper {
    static inline __m256i mm256_msb_mask_32() {
        return _mm256_set1_epi32(0x80000000);
    }

    static inline __m256i mm256_not_si256(__m256i a) {
        return _mm256_xor_si256(a, _mm256_set1_epi32(-1));
    }

    template<typename T>
    static inline void prefetch(T* addr) {
        _mm_prefetch((char*)addr, _MM_HINT_T0);
    }

    static inline __m256i mm256_multishift_epi64_epi8(__m256i a, __m256i count) {
        __m256i mask = _mm256_set1_epi64x(0xFF);
        __m256i result = _mm256_setzero_si256();
        
        for (int i = 0; i < 8; i++) {
            __m256i shifted = _mm256_srlv_epi64(a, count);
            result = _mm256_or_si256(result, _mm256_and_si256(shifted, mask));
            mask = _mm256_slli_epi64(mask, 8);
            count = _mm256_add_epi64(count, _mm256_set1_epi64x(8));
        }
        
        return result;
    }

    static inline __m256i mm256_merge_epi32(__m128i lo, __m128i hi) {
        return _mm256_insertf128_si256(_mm256_castsi128_si256(lo), hi, 1);
    }
};

} 

#endif 
