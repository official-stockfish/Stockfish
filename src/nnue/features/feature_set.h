/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

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

// A class template that represents the input feature set of the NNUE evaluation function

#ifndef NNUE_FEATURE_SET_H_INCLUDED
#define NNUE_FEATURE_SET_H_INCLUDED

#include "features_common.h"
#include <array>

namespace Eval::NNUE::Features {

  // Class template that represents a list of values
  template <typename T, T... Values>
  struct CompileTimeList;

  template <typename T, T First, T... Remaining>
  struct CompileTimeList<T, First, Remaining...> {
    static constexpr bool Contains(T value) {
      return value == First || CompileTimeList<T, Remaining...>::Contains(value);
    }
    static constexpr std::array<T, sizeof...(Remaining) + 1>
        kValues = {{First, Remaining...}};
  };

  // Base class of feature set
  template <typename Derived>
  class FeatureSetBase {

   public:
    // Get a list of indices for active features
    template <typename IndexListType>
    static void AppendActiveIndices(
        const Position& pos, TriggerEvent trigger, IndexListType active[2]) {

      for (Color perspective : { WHITE, BLACK }) {
        Derived::CollectActiveIndices(
            pos, trigger, perspective, &active[perspective]);
      }
    }

    // Get a list of indices for recently changed features
    template <typename PositionType, typename IndexListType>
    static void AppendChangedIndices(
        const PositionType& pos, TriggerEvent trigger,
        IndexListType removed[2], IndexListType added[2], bool reset[2]) {

      auto collect_for_one = [&](const DirtyPiece& dp) {
        for (Color perspective : { WHITE, BLACK }) {
          switch (trigger) {
            case TriggerEvent::kFriendKingMoved:
              reset[perspective] = dp.piece[0] == make_piece(perspective, KING);
              break;
            default:
              assert(false);
              break;
          }
          if (reset[perspective]) {
            Derived::CollectActiveIndices(
                pos, trigger, perspective, &added[perspective]);
          } else {
            Derived::CollectChangedIndices(
                pos, dp, trigger, perspective,
                &removed[perspective], &added[perspective]);
          }
        }
      };

      auto collect_for_two = [&](const DirtyPiece& dp1, const DirtyPiece& dp2) {
        for (Color perspective : { WHITE, BLACK }) {
          switch (trigger) {
            case TriggerEvent::kFriendKingMoved:
              reset[perspective] = dp1.piece[0] == make_piece(perspective, KING)
                                || dp2.piece[0] == make_piece(perspective, KING);
              break;
            default:
              assert(false);
              break;
          }
          if (reset[perspective]) {
            Derived::CollectActiveIndices(
                pos, trigger, perspective, &added[perspective]);
          } else {
            Derived::CollectChangedIndices(
                pos, dp1, trigger, perspective,
                &removed[perspective], &added[perspective]);
            Derived::CollectChangedIndices(
                pos, dp2, trigger, perspective,
                &removed[perspective], &added[perspective]);
          }
        }
      };

      if (pos.state()->previous->accumulator.computed_accumulation) {
        const auto& prev_dp = pos.state()->dirtyPiece;
        if (prev_dp.dirty_num == 0) return;
        collect_for_one(prev_dp);
      } else {
        const auto& prev_dp = pos.state()->previous->dirtyPiece;
        if (prev_dp.dirty_num == 0) {
          const auto& prev2_dp = pos.state()->dirtyPiece;
          if (prev2_dp.dirty_num == 0) return;
          collect_for_one(prev2_dp);
        } else {
          const auto& prev2_dp = pos.state()->dirtyPiece;
          if (prev2_dp.dirty_num == 0) {
            collect_for_one(prev_dp);
          } else {
            collect_for_two(prev_dp, prev2_dp);
          }
        }
      }
    }
  };

  // Class template that represents the feature set
  template <typename FeatureType>
  class FeatureSet<FeatureType> : public FeatureSetBase<FeatureSet<FeatureType>> {

   public:
    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t kHashValue = FeatureType::kHashValue;
    // Number of feature dimensions
    static constexpr IndexType kDimensions = FeatureType::kDimensions;
    // Maximum number of simultaneously active features
    static constexpr IndexType kMaxActiveDimensions =
        FeatureType::kMaxActiveDimensions;
    // Trigger for full calculation instead of difference calculation
    using SortedTriggerSet =
        CompileTimeList<TriggerEvent, FeatureType::kRefreshTrigger>;
    static constexpr auto kRefreshTriggers = SortedTriggerSet::kValues;

   private:
    // Get a list of indices for active features
    static void CollectActiveIndices(
        const Position& pos, const TriggerEvent trigger, const Color perspective,
        IndexList* const active) {
      if (FeatureType::kRefreshTrigger == trigger) {
        FeatureType::AppendActiveIndices(pos, perspective, active);
      }
    }

    // Get a list of indices for recently changed features
    static void CollectChangedIndices(
        const Position& pos, const DirtyPiece& dp, const TriggerEvent trigger, const Color perspective,
        IndexList* const removed, IndexList* const added) {

      if (FeatureType::kRefreshTrigger == trigger) {
        FeatureType::AppendChangedIndices(pos, dp, perspective, removed, added);
      }
    }

    // Make the base class and the class template that recursively uses itself a friend
    friend class FeatureSetBase<FeatureSet>;
    template <typename... FeatureTypes>
    friend class FeatureSet;
  };

}  // namespace Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURE_SET_H_INCLUDED
