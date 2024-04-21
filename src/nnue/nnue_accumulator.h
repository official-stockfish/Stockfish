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

// Class for difference calculation of NNUE evaluation function

#ifndef NNUE_ACCUMULATOR_H_INCLUDED
#define NNUE_ACCUMULATOR_H_INCLUDED

#include <cstdint>

#include "nnue_architecture.h"
#include "nnue_common.h"

namespace Stockfish::Eval::NNUE {

// Class that holds the result of affine transformation of input features
template<IndexType Size>
struct alignas(CacheLineSize) Accumulator {
    std::int16_t accumulation[2][Size];
    std::int32_t psqtAccumulation[2][PSQTBuckets];
    bool         computed[2];
    bool         computedPSQT[2];
};


struct alignas(CacheLineSize) AccumulatorRefreshEntry {
    std::int16_t accumulation[2][TransformedFeatureDimensionsBig];
    // using Acc = std::array<std::array<std::int16_t, TransformedFeatureDimensionsBig>, 2>;
    // Acc          accumulation;
    std::int32_t psqtAccumulation[2][PSQTBuckets];
    Bitboard     byColorBB[COLOR_NB][COLOR_NB];
    Bitboard     byTypeBB[COLOR_NB][PIECE_TYPE_NB];

    // todo use BiasType
    void clear(const std::int16_t* biases) {
        // To initialize a refresh entry, we set all its bitboards empty,
        // so we put the biases in the accumulation, without any weights on top

        std::memset(byColorBB, 0, 2 * 2 * sizeof(Bitboard));
        std::memset(byTypeBB, 0, 2 * 8 * sizeof(Bitboard));

        std::memcpy(accumulation[WHITE], biases,
                    TransformedFeatureDimensionsBig * sizeof(std::int16_t));
        std::memcpy(accumulation[BLACK], biases,
                    TransformedFeatureDimensionsBig * sizeof(std::int16_t));

        std::memset(psqtAccumulation, 0, sizeof(psqtAccumulation));
    }
};


struct alignas(CacheLineSize) AccumulatorCache {
    AccumulatorCache() = default;

    template<typename Network>
    AccumulatorCache(const Network& network) {
        clear(network);
    }

    template<typename Network>
    void clear(const Network& network) {
        for (auto& entry : entries)
        {
            entry.clear(network.featureTransformer->biases);
        }
    }

    void clear(const std::int16_t* biases) {
        for (auto& entry : entries)
        {
            entry.clear(biases);
        }
    }

    AccumulatorRefreshEntry& operator[](Square sq) { return entries[sq]; }
    AccumulatorRefreshEntry& operator[](int sq) { return entries[sq]; }

    std::array<AccumulatorRefreshEntry, SQUARE_NB> entries;
};

}  // namespace Stockfish::Eval::NNUE

#endif  // NNUE_ACCUMULATOR_H_INCLUDED
