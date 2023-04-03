/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

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

export void affine_transform(
    const uniform uint8 input[], const uniform int8 weights[], const uniform int biases[],
    uniform int output[], uniform int InputDimensions, uniform int PaddedInputDimensions,
    uniform int OutputDimensions) {
  #define TILE 16
  assume(OutputDimensions % TILE == 0);
  uniform int sum[TILE];
  uniform uint8 ij;
  for (uniform int i = 0; i < OutputDimensions; i += TILE) {
    foreach (j = 0 ... TILE) sum[j] = 0;
    for (uniform int j = 0; j < InputDimensions; j++) {
      ij = input[j];
      foreach (k = 0 ... TILE) {
        sum[k] += weights[j * OutputDimensions + i + k] * (int)ij;
      }
    }
    foreach (j = 0 ... TILE)
      output[i + j] = biases[i + j] + sum[j];
  }
}

export uniform int affine_transform1(const uniform int8 weights[], const uniform uint8 input[],
                                     uniform int InputDimensions) {
  assume(InputDimensions % programCount == 0);

  int sum = 0;
  foreach (j = 0 ... InputDimensions) {
    sum += weights[j] * (int)input[j];
  }
  return reduce_add(sum);
}

