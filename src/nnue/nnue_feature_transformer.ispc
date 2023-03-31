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


export void transform(const uniform int16 in0[], const uniform int16 in1[],
                      uniform uint8 output[], uniform int size) {
  foreach (j = 0 ... size) {
    int16 sum0 = max((uniform int16)0, min((uniform int16)127, in0[j]));
    int16 sum1 = max((uniform int16)0, min((uniform int16)127, in1[j]));
    output[j] = (sum0 * sum1) >> 7;
  }
}

export void update_accumulator_remove(
    const uniform uint32 active[], uniform int size, uniform int16 accumulation[],
    uniform int psqtAccumulation[], const uniform int16 weights[], const uniform int psqtWeights[],
    uniform int HalfDimensions, uniform int PSQTBuckets) {
  for (uniform int i = 0; i < size; i++) {
    int index = active[i];

    foreach (j = 0 ... HalfDimensions)
      accumulation[j] -= weights[HalfDimensions * index + j];

    foreach (k = 0 ... PSQTBuckets)
      psqtAccumulation[k] -= psqtWeights[index * PSQTBuckets + k];
  }
}

export void update_accumulator_add(
    const uniform uint32 active[], uniform int size, uniform int16 accumulation[],
    uniform int psqtAccumulation[], const uniform int16 weights[], const uniform int psqtWeights[],
    uniform int HalfDimensions, uniform int PSQTBuckets) {
  for (uniform int i = 0; i < size; i++) {
    int index = active[i];

    foreach (j = 0 ... HalfDimensions)
      accumulation[j] += weights[HalfDimensions * index + j];

    foreach (k = 0 ... PSQTBuckets)
      psqtAccumulation[k] += psqtWeights[index * PSQTBuckets + k];
  }
}
