// A class template that represents the input feature set of the NNUE evaluation function

#ifndef _NNUE_FEATURE_SET_H_
#define _NNUE_FEATURE_SET_H_

#if defined(EVAL_NNUE)

#include "features_common.h"
#include <array>

namespace Eval {

namespace NNUE {

namespace Features {

// A class template that represents a list of values
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
template <typename T, T First, T... Remaining>
constexpr std::array<T, sizeof...(Remaining) + 1>
    CompileTimeList<T, First, Remaining...>::kValues;
template <typename T>
struct CompileTimeList<T> {
  static constexpr bool Contains(T /*value*/) {
    return false;
  }
  static constexpr std::array<T, 0> kValues = {{}};
};

// Class template that adds to the beginning of the list
template <typename T, typename ListType, T Value>
struct AppendToList;
template <typename T, T... Values, T AnotherValue>
struct AppendToList<T, CompileTimeList<T, Values...>, AnotherValue> {
  using Result = CompileTimeList<T, AnotherValue, Values...>;
};

// Class template for adding to a sorted, unique list
template <typename T, typename ListType, T Value>
struct InsertToSet;
template <typename T, T First, T... Remaining, T AnotherValue>
struct InsertToSet<T, CompileTimeList<T, First, Remaining...>, AnotherValue> {
  using Result = std::conditional_t<
      CompileTimeList<T, First, Remaining...>::Contains(AnotherValue),
      CompileTimeList<T, First, Remaining...>,
      std::conditional_t<(AnotherValue <First),
          CompileTimeList<T, AnotherValue, First, Remaining...>,
          typename AppendToList<T, typename InsertToSet<
              T, CompileTimeList<T, Remaining...>, AnotherValue>::Result,
              First>::Result>>;
};
template <typename T, T Value>
struct InsertToSet<T, CompileTimeList<T>, Value> {
  using Result = CompileTimeList<T, Value>;
};

// Base class of feature set
template <typename Derived>
class FeatureSetBase {
 public:
  // Get a list of indices with a value of 1 among the features
  template <typename IndexListType>
  static void AppendActiveIndices(
      const Position& pos, TriggerEvent trigger, IndexListType active[2]) {
    for (const auto perspective :Colors) {
      Derived::CollectActiveIndices(
          pos, trigger, perspective, &active[perspective]);
    }
  }

  // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
  template <typename PositionType, typename IndexListType>
  static void AppendChangedIndices(
      const PositionType& pos, TriggerEvent trigger,
      IndexListType removed[2], IndexListType added[2], bool reset[2]) {
    const auto& dp = pos.state()->dirtyPiece;
    if (dp.dirty_num == 0) return;

    for (const auto perspective :Colors) {
      reset[perspective] = false;
      switch (trigger) {
        case TriggerEvent::kNone:
          break;
        case TriggerEvent::kFriendKingMoved:
          reset[perspective] =
              dp.pieceNo[0] == PIECE_NUMBER_KING + perspective;
          break;
        case TriggerEvent::kEnemyKingMoved:
          reset[perspective] =
              dp.pieceNo[0] == PIECE_NUMBER_KING + ~perspective;
          break;
        case TriggerEvent::kAnyKingMoved:
          reset[perspective] = dp.pieceNo[0] >= PIECE_NUMBER_KING;
          break;
        case TriggerEvent::kAnyPieceMoved:
          reset[perspective] = true;
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
// do internal processing in reverse order of template arguments in order to linearize the amount of calculation at runtime
template <typename FirstFeatureType, typename... RemainingFeatureTypes>
class FeatureSet<FirstFeatureType, RemainingFeatureTypes...> :
    public FeatureSetBase<
        FeatureSet<FirstFeatureType, RemainingFeatureTypes...>> {
 private:
  using Head = FirstFeatureType;
  using Tail = FeatureSet<RemainingFeatureTypes...>;

 public:
  // Hash value embedded in the evaluation function file
  static constexpr std::uint32_t kHashValue =
      Head::kHashValue ^ (Tail::kHashValue << 1) ^ (Tail::kHashValue >> 31);
  // number of feature dimensions
  static constexpr IndexType kDimensions =
      Head::kDimensions + Tail::kDimensions;
  // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
  static constexpr IndexType kMaxActiveDimensions =
      Head::kMaxActiveDimensions + Tail::kMaxActiveDimensions;
  // List of timings to perform all calculations instead of difference calculation
  using SortedTriggerSet = typename InsertToSet<TriggerEvent,
      typename Tail::SortedTriggerSet, Head::kRefreshTrigger>::Result;
  static constexpr auto kRefreshTriggers = SortedTriggerSet::kValues;

  // Get the feature quantity name
  static std::string GetName() {
    return std::string(Head::kName) + "+" + Tail::GetName();
  }

 private:
  // Get a list of indices with a value of 1 among the features
  template <typename IndexListType>
  static void CollectActiveIndices(
      const Position& pos, const TriggerEvent trigger, const Color perspective,
      IndexListType* const active) {
    Tail::CollectActiveIndices(pos, trigger, perspective, active);
    if (Head::kRefreshTrigger == trigger) {
      const auto start = active->size();
      Head::AppendActiveIndices(pos, perspective, active);
      for (auto i = start; i < active->size(); ++i) {
        (*active)[i] += Tail::kDimensions;
      }
    }
  }

  // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
  template <typename IndexListType>
  static void CollectChangedIndices(
      const Position& pos, const TriggerEvent trigger, const Color perspective,
      IndexListType* const removed, IndexListType* const added) {
    Tail::CollectChangedIndices(pos, trigger, perspective, removed, added);
    if (Head::kRefreshTrigger == trigger) {
      const auto start_removed = removed->size();
      const auto start_added = added->size();
      Head::AppendChangedIndices(pos, perspective, removed, added);
      for (auto i = start_removed; i < removed->size(); ++i) {
        (*removed)[i] += Tail::kDimensions;
      }
      for (auto i = start_added; i < added->size(); ++i) {
        (*added)[i] += Tail::kDimensions;
      }
    }
  }

  // Make the base class and the class template that recursively uses itself a friend
  friend class FeatureSetBase<FeatureSet>;
  template <typename... FeatureTypes>
  friend class FeatureSet;
};

// Class template that represents the feature set
// Specialization with one template argument
template <typename FeatureType>
class FeatureSet<FeatureType> : public FeatureSetBase<FeatureSet<FeatureType>> {
 public:
  // Hash value embedded in the evaluation function file
  static constexpr std::uint32_t kHashValue = FeatureType::kHashValue;
  // number of feature dimensions
  static constexpr IndexType kDimensions = FeatureType::kDimensions;
  // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
  static constexpr IndexType kMaxActiveDimensions =
      FeatureType::kMaxActiveDimensions;
  // List of timings to perform all calculations instead of difference calculation
  using SortedTriggerSet =
      CompileTimeList<TriggerEvent, FeatureType::kRefreshTrigger>;
  static constexpr auto kRefreshTriggers = SortedTriggerSet::kValues;

  // Get the feature quantity name
  static std::string GetName() {
    return FeatureType::kName;
  }

 private:
  // Get a list of indices with a value of 1 among the features
  static void CollectActiveIndices(
      const Position& pos, const TriggerEvent trigger, const Color perspective,
      IndexList* const active) {
    if (FeatureType::kRefreshTrigger == trigger) {
      FeatureType::AppendActiveIndices(pos, perspective, active);
    }
  }

  // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
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

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
