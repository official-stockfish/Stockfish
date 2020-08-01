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

      const auto& dp = pos.state()->dirtyPiece;
      if (dp.dirty_num == 0) return;

      for (Color perspective : { WHITE, BLACK }) {
        reset[perspective] = false;
        switch (trigger) {
          case TriggerEvent::kFriendKingMoved:
            reset[perspective] =
                dp.pieceId[0] == PIECE_ID_KING + perspective;
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
              pos, trigger, perspective,
              &removed[perspective], &added[perspective]);
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
        const Position& pos, const TriggerEvent trigger, const Color perspective,
        IndexList* const removed, IndexList* const added) {

      if (FeatureType::kRefreshTrigger == trigger) {
        FeatureType::AppendChangedIndices(pos, perspective, removed, added);
      }
    }

    // Make the base class and the class template that recursively uses itself a friend
    friend class FeatureSetBase<FeatureSet>;
    template <typename... FeatureTypes>
    friend class FeatureSet;
  };

}  // namespace Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURE_SET_H_INCLUDED
