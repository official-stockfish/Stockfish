//Definition of input features HalfKP of NNUE evaluation function

#ifndef NNUE_FEATURES_HALF_KP_H_INCLUDED
#define NNUE_FEATURES_HALF_KP_H_INCLUDED

#include "../../evaluate.h"
#include "features_common.h"

namespace Eval::NNUE::Features {

  // Feature HalfKP: Combination of the position of own king
  // and the position of pieces other than kings
  template <Side AssociatedKing>
  class HalfKP {

   public:
    // Feature name
    static constexpr const char* kName = "HalfKP(Friend)";
    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t kHashValue =
        0x5D69D5B9u ^ (AssociatedKing == Side::kFriend);
    // Number of feature dimensions
    static constexpr IndexType kDimensions =
        static_cast<IndexType>(SQUARE_NB) * static_cast<IndexType>(PS_END);
    // Maximum number of simultaneously active features
    static constexpr IndexType kMaxActiveDimensions = PIECE_ID_KING;
    // Trigger for full calculation instead of difference calculation
    static constexpr TriggerEvent kRefreshTrigger = TriggerEvent::kFriendKingMoved;

    // Get a list of indices for active features
    static void AppendActiveIndices(const Position& pos, Color perspective,
                                    IndexList* active);

  // Get a list of indices for recently changed features
    static void AppendChangedIndices(const Position& pos, Color perspective,
                                     IndexList* removed, IndexList* added);

    // Index of a feature for a given king position and another piece on some square
    static IndexType MakeIndex(Square sq_k, PieceSquare p);

   private:
    // Get pieces information
    static void GetPieces(const Position& pos, Color perspective,
                          PieceSquare** pieces, Square* sq_target_k);
  };

}  // namespace Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURES_HALF_KP_H_INCLUDED
