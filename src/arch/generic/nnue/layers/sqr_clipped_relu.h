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

#ifndef GENERIC_NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
#define GENERIC_NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED

#ifndef NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

#include <algorithm>
#include <cstdint>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims>
void SqrClippedReLU<InDims>::propagate(const InputType* input, OutputType* output) const {
    // The correct formula is to divide by 127, but we need to make it fast
    // therefore right-shift by extra 7 bits is used instead. Needs to be
    // accounted for in the trainer.
    for (IndexType i = 0; i < InputDimensions; ++i)
        output[i] = static_cast<OutputType>(std::min(
          std::int64_t(127), std::int64_t(input[i]) * input[i] >> (2 * WeightScaleBits + 7)));
}

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // GENERIC_NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
