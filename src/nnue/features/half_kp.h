//Definition of input features HalfKP of NNUE evaluation function

#ifndef NNUE_FEATURES_HALF_KP_H_INCLUDED
#define NNUE_FEATURES_HALF_KP_H_INCLUDED

#include "../../evaluate.h"
#include "features_common.h"

namespace Eval::NNUE::Features {

  // Feature HalfKP: Combination of the position of own king or enemy king
  // and the position of pieces other than kings
  template <Side AssociatedKing>
  class HalfKP {

   public:
    // feature quantity name
    static constexpr const char* kName =
        (AssociatedKing == Side::kFriend) ? "HalfKP(Friend)" : "HalfKP(Enemy)";
    // Hash value embedded in the evaluation function file
    static constexpr std::uint32_t kHashValue =
        0x5D69D5B9u ^ (AssociatedKing == Side::kFriend);
    // number of feature dimensions
    static constexpr IndexType kDimensions =
        static_cast<IndexType>(SQUARE_NB) * static_cast<IndexType>(PS_END);
    // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
    static constexpr IndexType kMaxActiveDimensions = PIECE_ID_KING;
    // Timing of full calculation instead of difference calculation
    static constexpr TriggerEvent kRefreshTrigger = TriggerEvent::kFriendKingMoved;

    // Get a list of indices with a value of 1 among the features
    static void AppendActiveIndices(const Position& pos, Color perspective,
                                    IndexList* active);

    // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
    static void AppendChangedIndices(const Position& pos, Color perspective,
                                     IndexList* removed, IndexList* added);

    // Find the index of the feature quantity from the king position and PieceSquare
    static IndexType MakeIndex(Square sq_k, PieceSquare p);

   private:
    // Get the piece information
    static void GetPieces(const Position& pos, Color perspective,
                          PieceSquare** pieces, Square* sq_target_k);
  };

}  // namespace Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURES_HALF_KP_H_INCLUDED
