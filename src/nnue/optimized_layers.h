#ifndef STOCKFISH_EVAL_NNUE_OPTIMIZED_LAYERS_H_INCLUDED
#define STOCKFISH_EVAL_NNUE_OPTIMIZED_LAYERS_H_INCLUDED

#include "../simd_ops.h"
#include "../cache_optimizer.h"
#include "nnue_common.h"

namespace Stockfish::Eval::NNUE {

struct OptimizedLayer {
    static void affine_transform_avx2(
        const std::int32_t* input,
        const std::int32_t* weights,
        std::int32_t* output,
        const std::int32_t* biases,
        unsigned inputDimensions,
        unsigned outputDimensions
    ) {
        const unsigned numChunks = (inputDimensions + 7) / 8;
        
        for (unsigned i = 0; i < outputDimensions; i++) {
            __m256i sum = _mm256_setzero_si256();
            
            for (unsigned j = 0; j < numChunks; j++) {
                __m256i in = _mm256_load_si256(
                    reinterpret_cast<const __m256i*>(input + j * 8));
                __m256i w = _mm256_load_si256(
                    reinterpret_cast<const __m256i*>(weights + i * inputDimensions + j * 8));
                
                __m256i prod = _mm256_mullo_epi32(in, w);
                sum = _mm256_add_epi32(sum, prod);
            }
            
            
            __m128i sum128 = _mm_add_epi32(_mm256_castsi256_si128(sum), 
                                          _mm256_extracti128_si256(sum, 1));
            sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_SHUFFLE(1, 0, 3, 2)));
            sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_SHUFFLE(2, 3, 0, 1)));
            output[i] = _mm_cvtsi128_si32(sum128) + biases[i];
        }
    }

    static void quantize_weights(
        const float* input,
        std::int32_t* output,
        unsigned size,
        float scale
    ) {
        const __m256 scale_v = _mm256_set1_ps(scale);
        
        for (unsigned i = 0; i < size; i += 8) {
            __m256 in = _mm256_load_ps(input + i);
            __m256 scaled = _mm256_mul_ps(in, scale_v);
            __m256i rounded = _mm256_cvtps_epi32(scaled);
            _mm256_store_si256(reinterpret_cast<__m256i*>(output + i), rounded);
        }
    }
};

} 

#endif
