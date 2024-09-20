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

#ifndef GENERIC_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#define GENERIC_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED

#ifndef NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#error "Never use architecture specific header files directly."
#endif

#include <algorithm>

#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE::Layers {

template<IndexType InDims>
void ClippedReLU<InDims>::propagate(const InputType* input, OutputType* output) const {
    for (IndexType i = 0; i < InputDimensions; ++i)
        output[i] = static_cast<OutputType>(std::clamp(input[i] >> WeightScaleBits, 0, 127));
}

}  // namespace Stockfish::Eval::NNUE::Layers

#endif  // GENERIC_NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
