#ifndef _NNUE_FEATURES_HALF_RELATIVE_KP_H_
#define _NNUE_FEATURES_HALF_RELATIVE_KP_H_

#include "features_common.h"

#include "evaluate.h"

//Definition of input features HalfRelativeKP of NNUE evaluation function
namespace Eval::NNUE::Features {

    // Feature HalfRelativeKP: Relative position of each piece other than the ball based on own ball or enemy ball
    template <Side AssociatedKing>
    class HalfRelativeKP {
    public:
        // feature quantity name
        static constexpr const char* kName = (AssociatedKing == Side::kFriend) ?
            "HalfRelativeKP(Friend)" : "HalfRelativeKP(Enemy)";

        // Hash value embedded in the evaluation function file
        static constexpr std::uint32_t kHashValue =
            0xF9180919u ^ (AssociatedKing == Side::kFriend);

        // Piece type excluding balls
        static constexpr IndexType kNumPieceKinds = 5 * 2;

        // width of the virtual board with the ball in the center
        static constexpr IndexType kBoardWidth = FILE_NB * 2 - 1;

        // height of a virtual board with balls in the center
        static constexpr IndexType kBoardHeight = RANK_NB * 2 - 1;

        // number of feature dimensions
        static constexpr IndexType kDimensions =
            kNumPieceKinds * kBoardHeight * kBoardWidth;

        // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
        static constexpr IndexType kMaxActiveDimensions = 30; // Kings don't count

        // Timing of full calculation instead of difference calculation
        static constexpr TriggerEvent kRefreshTrigger =
            (AssociatedKing == Side::kFriend) ?
            TriggerEvent::kFriendKingMoved : TriggerEvent::kEnemyKingMoved;

        // Get a list of indices with a value of 1 among the features
        static void append_active_indices(
            const Position& pos,
            Color perspective,
            IndexList* active);

        // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
        static void append_changed_indices(
            const Position& pos,
            Color perspective,
            IndexList* removed,
            IndexList* added);

        // Find the index of the feature quantity from the ball position and PieceSquare
        static IndexType make_index(Square s, IndexType p);

        // Find the index of the feature quantity from the ball position and PieceSquare
        static IndexType make_index(Color perspective, Square s, Piece pc, Square sq_k);
    };

}  // namespace Eval::NNUE::Features

#endif
