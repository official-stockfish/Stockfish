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

#ifndef ARM_NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
#define ARM_NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

#include "../../arch.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

#if __ARM_ARCH >= 8

alignas(CacheLineSize) static const std::array<std::array<std::uint16_t, 8>, 256> lookupIndices =
  [] {
      std::array<std::array<std::uint16_t, 8>, 256> array{};
      for (std::uint64_t i = 0; i < 256; ++i)
      {
          std::uint64_t j = i, k = 0;
          while (j)
              array[i][k++] = ctz(j), j &= j - 1;
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
        IndexType  count = 0;
        uint16x8_t base  = vdupq_n_u16(0);

        const auto in = reinterpret_cast<const int32x4_t*>(input);

        for (IndexType i = 0; i < InputDimensions / 8; ++i)
        {
            const int32x4_t chunk0 = in[i * 2];
            const int32x4_t chunk1 = in[i * 2 + 1];

            static const uint32x4_t movemask = [] {
                const std::uint32_t n[4] = {1, 2, 4, 8};
                return vld1q_u32(n);
            }();

            const std::uint32_t nnz = vaddvq_u32(vandq_u32(vtstq_s32(chunk0, chunk0), movemask))
                                    | vaddvq_u32(vandq_u32(vtstq_s32(chunk1, chunk1), movemask))
                                        << 4;
            const uint16x8_t offsets = *reinterpret_cast<const uint16x8_t*>(&lookupIndices[nnz]);
            *reinterpret_cast<uint16x8_t*>(indices + count) = vaddq_u16(base, offsets);
            count += popcount(nnz);
            base = vaddq_u16(base, vdupq_n_u16(8));
        }

        return count;
    }
};

template<IndexType InDims, IndexType OutDims>
void AffineTransformSparseInput<InDims, OutDims>::propagate(const InputType* input,
                                                            OutputType*      output) const {
    static constexpr IndexType OutputLanes = 16 / sizeof(OutputType);

    static constexpr IndexType NumChunks = ceil_to_multiple<IndexType>(InputDimensions, 8) / 4;
    static constexpr IndexType NumRegs   = OutputDimensions / OutputLanes;

    int32x4_t     acc[NumRegs];
    std::uint16_t nnz[NumChunks];
    IndexType     count = populate_nz_indices<NumChunks>(input, nnz);

    for (std::size_t k = 0; k < array_size(acc); ++k)
        acc[k] = reinterpret_cast<const int32x4_t*>(biases)[k];

    for (IndexType j = 0; j < count; ++j)
    {
        const auto      i = nnz[j];
        const int8x16_t in =
          vreinterpretq_s8_s32(vdupq_n_s32(reinterpret_cast<const std::int32_t*>(input)[i]));
        const auto col = reinterpret_cast<const int8x16_t*>(&weights[i * OutputDimensions * 4]);
        for (std::size_t k = 0; k < array_size(acc); ++k)
            vdotq_s32_v(acc[k], in, col[k]);
    }

    for (std::size_t k = 0; k < array_size(acc); ++k)
        reinterpret_cast<int32x4_t*>(output)[k] = acc[k];
}

#else

template<IndexType InDims, IndexType OutDims>
using AffineTransformSparseInput = AffineTransform<InDims, OutDims>;

#endif  // __ARM_ARCH >= 8

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // ARM_NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
