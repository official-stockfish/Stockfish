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

#ifndef GENERIC_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define GENERIC_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

#include <cstring>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims, IndexType OutDims>
constexpr IndexType AffineTransform<InDims, OutDims>::get_weight_index(IndexType i) {
    return i;
}

template<IndexType InDims, IndexType OutDims>
void AffineTransform<InDims, OutDims>::propagate(const InputType* input, OutputType* output) const {
    std::memcpy(output, biases, sizeof(OutputType) * OutputDimensions);

    // Traverse weights in transpose order to take advantage of input sparsity
    for (IndexType i = 0; i < InputDimensions; ++i)
    {
        const InputType in = input[i];
        if (in)
        {
            const WeightType* w = &weights[i];
            for (IndexType j = 0; j < OutputDimensions; ++j)
                output[j] += w[j * PaddedInputDimensions] * in;
        }
    }
}

template<IndexType InDims, IndexType OutDims>
using AffineTransformSparseInput = AffineTransform<InDims, OutDims>;

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // GENERIC_NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
