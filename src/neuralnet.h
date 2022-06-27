/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

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

#ifndef NEURALNET_H_INCLUDED
#define NEURALNET_H_INCLUDED

#include <string>
#include <vector>

#include "types.h"

constexpr int INPUT_WEIGHTS  = 12 * 64;
constexpr int HIDDEN_BIAS    = 256;
constexpr int HIDDEN_WEIGHTS = 256;
constexpr int OUTPUT_BIAS    = 1;

class NeuralNet {
public:
  void init(const char* filename);
  void init_accumulator(int16_t *accumulator, int size);
  void activate(int16_t *accumulator, int size, int inputSq);
  void deactivate(int16_t *accumulator, int size, int inputSq);
  int relu(int x);
  int32_t output(int16_t *accumulator, int size);

  int16_t InputWeights[INPUT_WEIGHTS * HIDDEN_WEIGHTS];
  int16_t HiddenBias[HIDDEN_BIAS];
  int16_t HiddenWeights[HIDDEN_WEIGHTS];
  int32_t OutputBias[OUTPUT_BIAS];
};

extern NeuralNet nnue;

#endif // #ifndef NEURALNET_H_INCLUDED
