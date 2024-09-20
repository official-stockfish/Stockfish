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

// nnue_feature_transformer.h contains the definition of FeatureTransformer
// class, which converts a position into NNUE input features.
//
// Following function(s) must be implemented in the architecture-specific
// files:
//
//  FeatureTransformer::permute_weights
//  FeatureTransformer::apply_accumulator_updates_incremental
//  FeatureTransformer::apply_accumulator_updates_refresh_cache
//  FeatureTransformer::convert_accumulators

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iosfwd>
#include <utility>

#include "position.h"
#include "types.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_architecture.h"
#include "nnue/nnue_common.h"

namespace Stockfish::Eval::NNUE {

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
class FeatureTransformer {
    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = FeatureSet::Dimensions;
    static constexpr IndexType OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr std::size_t BufferSize = OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() {
        return FeatureSet::HashValue ^ (OutputDimensions * 2);
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) {
        read_leb_128<BiasType>(stream, biases, HalfDimensions);
        read_leb_128<WeightType>(stream, weights, HalfDimensions * InputDimensions);
        read_leb_128<PSQTWeightType>(stream, psqtWeights, PSQTBuckets * InputDimensions);

        permute_weights<false>();
        scale_weights<false>();
        return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) {
        permute_weights<true>();
        scale_weights<true>();

        write_leb_128<BiasType>(stream, biases, HalfDimensions);
        write_leb_128<WeightType>(stream, weights, HalfDimensions * InputDimensions);
        write_leb_128<PSQTWeightType>(stream, psqtWeights, PSQTBuckets * InputDimensions);

        permute_weights<false>();
        scale_weights<false>();
        return !stream.fail();
    }

    // Convert input features
    std::int32_t transform(const Position&                           pos,
                           AccumulatorCaches::Cache<HalfDimensions>* cache,
                           OutputType*                               output,
                           int                                       bucket) const {

        update_accumulator<WHITE>(pos, cache);
        update_accumulator<BLACK>(pos, cache);
        convert_accumulators(pos, output);

        const auto& psqtAccumulation = (pos.state()->*accPtr).psqtAccumulation;

        return (psqtAccumulation[pos.side_to_move()][bucket]
                - psqtAccumulation[~pos.side_to_move()][bucket])
             / 2;
    }

    void hint_common_access(const Position&                           pos,
                            AccumulatorCaches::Cache<HalfDimensions>* cache) const {
        hint_common_access_for_perspective<WHITE>(pos, cache);
        hint_common_access_for_perspective<BLACK>(pos, cache);
    }

   private:
    using BiasType       = FeatureTransformerBiasType;
    using WeightType     = std::int16_t;
    using PSQTWeightType = FeatureTransformerPSQTWeightType;

    // Stores constants and types based on the target architecture.
    struct Details;

    template<bool Write>
    inline void permute_weights();

    template<bool Write>
    inline void scale_weights() {
        for (IndexType j = 0; j < InputDimensions; ++j)
        {
            WeightType* w = &weights[j * HalfDimensions];
            for (IndexType i = 0; i < HalfDimensions; ++i)
                w[i] = Write ? w[i] / 2 : w[i] * 2;
        }

        for (IndexType i = 0; i < HalfDimensions; ++i)
            biases[i] = Write ? biases[i] / 2 : biases[i] * 2;
    }

    // Look for an accumulator of an earlier position. It traverses the linked
    // list of states starting from the current position and goes back until it
    // finds a computed accumulator or a state that requires a full refresh.
    template<Color Perspective>
    StateInfo* try_find_computed_accumulator(const Position& pos) const {
        // Look for a usable accumulator of an earlier position. We keep track
        // of the estimated gain in terms of features to be added/subtracted.
        StateInfo* st   = pos.state();
        int        gain = FeatureSet::refresh_cost(pos);
        while (st->previous && !(st->*accPtr).computed[Perspective])
        {
            // This governs when a full feature refresh is needed and how many
            // updates are better than just one full refresh.
            if (FeatureSet::requires_refresh(st, Perspective)
                || (gain -= FeatureSet::update_cost(st) + 1) < 0)
                break;
            st = st->previous;
        }
        return st;
    }

    // It computes the accumulator of the next position, or updates the
    // current position's accumulator if CurrentOnly is true.
    template<Color Perspective, bool CurrentOnly>
    void update_accumulator_incremental(const Position& pos, StateInfo* computed) const {
        assert((computed->*accPtr).computed[Perspective]);
        assert(computed->next != nullptr);

        const Square ksq = pos.square<KING>(Perspective);

        // The size must be enough to contain the largest possible update.
        // That might depend on the feature set and generally relies on the
        // feature set's update cost calculation to be correct and never allow
        // updates with more added/removed features than MaxActiveDimensions.
        FeatureSet::IndexList removed, added;

        if constexpr (CurrentOnly)
            for (StateInfo* st = pos.state(); st != computed; st = st->previous)
                FeatureSet::append_changed_indices<Perspective>(ksq, st->dirtyPiece, removed,
                                                                added);
        else
            FeatureSet::append_changed_indices<Perspective>(ksq, computed->next->dirtyPiece,
                                                            removed, added);

        StateInfo* next = CurrentOnly ? pos.state() : computed->next;
        assert(!(next->*accPtr).computed[Perspective]);

        apply_accumulator_updates_incremental<Perspective, CurrentOnly>(computed, next, removed,
                                                                        added);

        (next->*accPtr).computed[Perspective] = true;

        if (!CurrentOnly && next != pos.state())
            update_accumulator_incremental<Perspective, false>(pos, next);
    }

    template<Color Perspective, bool CurrentOnly>
    inline void apply_accumulator_updates_incremental(StateInfo*             computed_st,
                                                      StateInfo*             next,
                                                      FeatureSet::IndexList& removed,
                                                      FeatureSet::IndexList& added) const;

    // Update the accumluator for the current position and refresh the cache.
    //
    // Instead of rebuilding the accumulator from scratch, the accumulator is
    // updated by applying the differences between it and the cached one.
    template<Color Perspective>
    void update_accumulator_refresh_cache(const Position&                           pos,
                                          AccumulatorCaches::Cache<HalfDimensions>* cache) const {
        assert(cache != nullptr);

        FeatureSet::IndexList removed, added;

        const Square ksq   = pos.square<KING>(Perspective);
        auto&        entry = (*cache)[ksq][Perspective];

        for (Color c : {WHITE, BLACK})
        {
            for (PieceType pt = PAWN; pt <= KING; ++pt)
            {
                const Piece    piece    = make_piece(c, pt);
                const Bitboard oldBB    = entry.byColorBB[c] & entry.byTypeBB[pt];
                const Bitboard newBB    = pos.pieces(c, pt);
                Bitboard       toRemove = oldBB & ~newBB;
                Bitboard       toAdd    = newBB & ~oldBB;

                while (toRemove)
                {
                    Square sq = pop_lsb(toRemove);
                    removed.push_back(FeatureSet::make_index<Perspective>(sq, piece, ksq));
                }
                while (toAdd)
                {
                    Square sq = pop_lsb(toAdd);
                    added.push_back(FeatureSet::make_index<Perspective>(sq, piece, ksq));
                }
            }
        }

        for (Color c : {WHITE, BLACK})
            entry.byColorBB[c] = pos.pieces(c);

        for (PieceType pt = PAWN; pt <= KING; ++pt)
            entry.byTypeBB[pt] = pos.pieces(pt);

        auto& accumulator = pos.state()->*accPtr;
        apply_accumulator_updates_refresh_cache<Perspective>(accumulator, entry, removed, added);
        accumulator.computed[Perspective] = true;
    }

    template<Color Perspective>
    inline void apply_accumulator_updates_refresh_cache(
      Accumulator<TransformedFeatureDimensions>&                accumulator,
      typename AccumulatorCaches::Cache<HalfDimensions>::Entry& entry,
      FeatureSet::IndexList                                     removed,
      FeatureSet::IndexList                                     added) const;

    template<Color Perspective>
    void hint_common_access_for_perspective(const Position&                           pos,
                                            AccumulatorCaches::Cache<HalfDimensions>* cache) const {

        // Works like update_accumulator, but performs less work.
        // Updates ONLY the accumulator for pos.

        // Look for a usable accumulator of an earlier position. We keep track
        // of the estimated gain in terms of features to be added/subtracted.
        // Fast early exit.
        if ((pos.state()->*accPtr).computed[Perspective])
            return;

        StateInfo* oldest = try_find_computed_accumulator<Perspective>(pos);

        if ((oldest->*accPtr).computed[Perspective] && oldest != pos.state())
            update_accumulator_incremental<Perspective, true>(pos, oldest);
        else
            update_accumulator_refresh_cache<Perspective>(pos, cache);
    }

    template<Color Perspective>
    void update_accumulator(const Position&                           pos,
                            AccumulatorCaches::Cache<HalfDimensions>* cache) const {

        StateInfo* oldest = try_find_computed_accumulator<Perspective>(pos);

        if ((oldest->*accPtr).computed[Perspective] && oldest != pos.state())
            // Start from the oldest computed accumulator, update all the
            // accumulators up to the current position.
            update_accumulator_incremental<Perspective, false>(pos, oldest);
        else
            update_accumulator_refresh_cache<Perspective>(pos, cache);
    }

    // Called in transform after both accumulators are updated.
    inline void convert_accumulators(const Position& pos, OutputType* output) const;

    template<IndexType Size>
    friend struct AccumulatorCaches::Cache;

    alignas(CacheLineSize) BiasType biases[HalfDimensions];
    alignas(CacheLineSize) WeightType weights[HalfDimensions * InputDimensions];
    alignas(CacheLineSize) PSQTWeightType psqtWeights[InputDimensions * PSQTBuckets];
};

}  // namespace Stockfish::Eval::NNUE

#if defined(__i386__) || defined(__amd64__)

    #include "arch/i386/nnue/nnue_feature_transformer.h"

#elif defined(__arm__) || defined(__aarch64__)

    #include "arch/arm/nnue/nnue_feature_transformer.h"

#else

    #include "arch/generic/nnue/nnue_feature_transformer.h"

#endif

#endif  // NNUE_FEATURE_TRANSFORMER_H_INCLUDED
