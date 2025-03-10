/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"

namespace Stockfish {
class Position;
}

namespace Stockfish::Eval::NNUE {

using BiasType       = std::int16_t;
using PSQTWeightType = std::int32_t;
using IndexType      = std::uint32_t;

struct Networks;

template<IndexType Size>
struct alignas(CacheLineSize) Accumulator;

struct AccumulatorState;

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> AccumulatorState::*accPtr>
class FeatureTransformer;

// Class that holds the result of affine transformation of input features
template<IndexType Size>
struct alignas(CacheLineSize) Accumulator {
    std::int16_t               accumulation[COLOR_NB][Size];
    std::int32_t               psqtAccumulation[COLOR_NB][PSQTBuckets];
    std::array<bool, COLOR_NB> computed;
};


// AccumulatorCaches struct provides per-thread accumulator caches, where each
// cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea, was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCaches {

    template<typename Networks>
    AccumulatorCaches(const Networks& networks) {
        clear(networks);
    }

    template<IndexType Size>
    struct alignas(CacheLineSize) Cache {

        struct alignas(CacheLineSize) Entry {
            BiasType       accumulation[Size];
            PSQTWeightType psqtAccumulation[PSQTBuckets];
            Bitboard       byColorBB[COLOR_NB];
            Bitboard       byTypeBB[PIECE_TYPE_NB];

            // To initialize a refresh entry, we set all its bitboards empty,
            // so we put the biases in the accumulation, without any weights on top
            void clear(const BiasType* biases) {

                std::memcpy(accumulation, biases, sizeof(accumulation));
                std::memset((uint8_t*) this + offsetof(Entry, psqtAccumulation), 0,
                            sizeof(Entry) - offsetof(Entry, psqtAccumulation));
            }
        };

        template<typename Network>
        void clear(const Network& network) {
            for (auto& entries1D : entries)
                for (auto& entry : entries1D)
                    entry.clear(network.featureTransformer->biases);
        }

        std::array<Entry, COLOR_NB>& operator[](Square sq) { return entries[sq]; }

        std::array<std::array<Entry, COLOR_NB>, SQUARE_NB> entries;
    };

    template<typename Networks>
    void clear(const Networks& networks) {
        big.clear(networks.big);
        small.clear(networks.small);
    }

    Cache<TransformedFeatureDimensionsBig>   big;
    Cache<TransformedFeatureDimensionsSmall> small;
};


struct AccumulatorState {
    Accumulator<TransformedFeatureDimensionsBig>   accumulatorBig;
    Accumulator<TransformedFeatureDimensionsSmall> accumulatorSmall;
    DirtyPiece                                     dirtyPiece;

    void reset(const DirtyPiece& dp) noexcept;
};


class AccumulatorStack {
   public:
    AccumulatorStack() :
        m_accumulators(MAX_PLY + 1),
        m_current_idx{} {}

    [[nodiscard]] const AccumulatorState& latest() const noexcept;

    void
    reset(const Position& rootPos, const Networks& networks, AccumulatorCaches& caches) noexcept;
    void push(const DirtyPiece& dirtyPiece) noexcept;
    void pop() noexcept;

    template<IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
    void evaluate(const Position&                               pos,
                  const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                  AccumulatorCaches::Cache<Dimensions>&         cache) noexcept;

   private:
    [[nodiscard]] AccumulatorState& mut_latest() noexcept;

    template<Color                   Perspective,
             IndexType               Dimensions,
             Accumulator<Dimensions> AccumulatorState::*accPtr>
    void evaluate_side(const Position&                               pos,
                       const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                       AccumulatorCaches::Cache<Dimensions>&         cache) noexcept;

    template<Color                   Perspective,
             IndexType               Dimensions,
             Accumulator<Dimensions> AccumulatorState::*accPtr>
    [[nodiscard]] std::size_t find_last_usable_accumulator() const noexcept;

    template<Color                   Perspective,
             IndexType               Dimensions,
             Accumulator<Dimensions> AccumulatorState::*accPtr>
    void
    forward_update_incremental(const Position&                               pos,
                               const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                               const std::size_t                             begin) noexcept;

    template<Color                   Perspective,
             IndexType               Dimensions,
             Accumulator<Dimensions> AccumulatorState::*accPtr>
    void
    backward_update_incremental(const Position&                               pos,
                                const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                                const std::size_t                             end) noexcept;

    std::vector<AccumulatorState> m_accumulators;
    std::size_t                   m_current_idx;
};

}  // namespace Stockfish::Eval::NNUE

#endif  // NNUE_ACCUMULATOR_H_INCLUDED
