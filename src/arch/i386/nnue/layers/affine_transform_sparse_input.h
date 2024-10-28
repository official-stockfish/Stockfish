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

#ifndef I386_NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
#define I386_NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

#include "../../arch.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

#ifdef __SSSE3__

alignas(CacheLineSize) static const std::array<std::array<std::uint16_t, 8>, 256> lookupIndices =
  [] {
      std::array<std::array<std::uint16_t, 8>, 256> array{};
      for (std::uint64_t i = 0; i < 256; ++i)
      {
          std::uint64_t j = i, k = 0;
          while (j)
              array[i][k++] = tzcnt(j), j = blsr(j);
      }
      return array;
  }();

template<IndexType InDims, IndexType OutDims>
class AffineTransformSparseInput: public AffineTransform<InDims, OutDims> {
    __DEFINE_BASE_PROPERTIES

    static_assert(OutputDimensions % 16 == 0,
                  "OutputDimensions must be multiple of 16 for this layer.");

   public:
    void propagate(const InputType* input, OutputType* output) const;

   private:
    template<IndexType InputDimensions>
    static IndexType populate_nz_indices(const std::uint8_t* input, std::uint16_t* indices) {
#if defined(__AVX512F__) && !defined(NO_AVX512)
        using vec_t = __m512i;
#elif defined(__AVX__)
        using vec_t = __m256i;
#else
        using vec_t = __m128i;
#endif

        static constexpr IndexType InputLanes = sizeof(vec_t) / 4;

        // Inputs are processed InputLanes at a time and outputs are processed
        // 8 at a time so we process in chunks of max(InputLanes, 8).
        static constexpr IndexType ChunkSize       = std::max<IndexType>(InputLanes, 8);
        static constexpr IndexType NumChunks       = InputDimensions / ChunkSize;
        static constexpr IndexType InputsPerChunk  = ChunkSize / InputLanes;
        static constexpr IndexType OutputsPerChunk = ChunkSize / 8;

        IndexType count = 0;
        __m128i   base  = _mm_setzero_si128();

        for (IndexType i = 0; i < NumChunks; ++i)
        {
            std::uint32_t nnz = 0;
            for (IndexType j = 0; j < InputsPerChunk; ++j)
            {
                const vec_t chunk = reinterpret_cast<const vec_t*>(input)[i * InputsPerChunk + j];

                // Since all 32-bit blocks are non-negative, it is safe to use cmpgt
                // if the target architecture does not support cmpneq.
#if defined(__AVX512F__) && !defined(NO_AVX512)
                const std::uint32_t mask = _mm512_cmpneq_epi32_mask(chunk, _mm512_setzero_si512());
#elif defined(__AVX2__)
                const std::uint32_t mask = _mm256_movemask_ps(
                  _mm256_castsi256_ps(_mm256_cmpgt_epi32(chunk, _mm256_setzero_si256())));
#elif defined(__AVX__)
                const std::uint32_t mask = _mm256_movemask_ps(
                  _mm256_cmp_ps(_mm256_castsi256_ps(chunk), _mm256_setzero_ps(), _CMP_NEQ_UQ));
#else
                const std::uint32_t mask =
                  _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpgt_epi32(chunk, _mm_setzero_si128())));
#endif

                nnz |= mask << (j * InputLanes);
            }
            for (IndexType j = 0; j < OutputsPerChunk; ++j)
            {
                const std::uint8_t lookup = (nnz >> (j * 8)) & 0xFF;
                const __m128i offsets = *reinterpret_cast<const __m128i*>(&lookupIndices[lookup]);
                _mm_storeu_si128(reinterpret_cast<__m128i_u*>(indices + count),
                                 _mm_add_epi16(base, offsets));
                count += popcount(lookup);
                base = _mm_add_epi16(base, _mm_set1_epi16(8));
            }
        }

        return count;
    }
};

template<IndexType InDims, IndexType OutDims>
void AffineTransformSparseInput<InDims, OutDims>::propagate(const InputType* input,
                                                            OutputType*      output) const {
#if defined(__AVX512F__) && (defined(__AVX512BW__) || defined(__AVX512VNNI__)) \
  && !defined(NO_AVX512)
    using vec_t = __m512i;
#elif defined(__AVX2__)
    using vec_t = __m256i;
#else
    using vec_t = __m128i;
#endif

    static constexpr IndexType OutputLanes = sizeof(vec_t) / sizeof(OutputType);

    static constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(InputDimensions, 8) / 4;
    static constexpr IndexType NumRegs   = OutputDimensions / OutputLanes;

    vec_t         acc[NumRegs];
    std::uint16_t nnz[NumChunks];
    IndexType     count = populate_nz_indices<NumChunks>(input, nnz);

    for (std::size_t k = 0; k < array_size(acc); ++k)
        acc[k] = reinterpret_cast<const vec_t*>(biases)[k];

    for (IndexType j = 0; j < count; ++j)
    {
        const auto  i   = nnz[j];
        const vec_t in  = _mm_set1_epi32_v<vec_t>(reinterpret_cast<const std::uint32_t*>(input)[i]);
        const auto  col = reinterpret_cast<const vec_t*>(&weights[i * OutputDimensions * 4]);
        for (std::size_t k = 0; k < array_size(acc); ++k)
            _mm_dpbusd_epi32_v(acc[k], in, col[k]);
    }

    for (std::size_t k = 0; k < array_size(acc); ++k)
        reinterpret_cast<vec_t*>(output)[k] = acc[k];
}

#else

template<IndexType InDims, IndexType OutDims>
using AffineTransformSparseInput = AffineTransform<InDims, OutDims>;

#endif  // __SSSE3__

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // I386_NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
