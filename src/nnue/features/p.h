#ifndef _NNUE_FEATURES_P_H_
#define _NNUE_FEATURES_P_H_

#include "features_common.h"

#include "evaluate.h"

//Definition of input feature P of NNUE evaluation function
namespace Eval::NNUE::Features {

    // Feature P: PieceSquare of pieces other than balls
    class P {
    public:
        // feature quantity name
        static constexpr const char* kName = "P";

        // Hash value embedded in the evaluation function file
        static constexpr std::uint32_t kHashValue = 0x764CFB4Bu;

        // number of feature dimensions
        static constexpr IndexType kDimensions = PS_END;

        // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
        static constexpr IndexType kMaxActiveDimensions = 30; // Kings don't count

        // Timing of full calculation instead of difference calculation
        static constexpr TriggerEvent kRefreshTrigger = TriggerEvent::kNone;

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

    private:
        // Index of a feature for a given piece on some square
        static IndexType make_index(Color perspective, Square s, Piece pc);
    };

}  // namespace Eval::NNUE::Features

#endif
