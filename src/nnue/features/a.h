#ifndef _NNUE_FEATURES_A_H_
#define _NNUE_FEATURES_A_H_

#include "features_common.h"

#include "evaluate.h"

// Definition of input feature A of NNUE evaluation function
// A is a union of P features and K features, so technically the
// same effect can be achieved by including both P and K features
// but it would result in slower index appending because
// P would conditionally exclude K features and vice versa,
// where A doesn't have any conditionals.
namespace Eval::NNUE::Features {

    // Feature P: PieceSquare of pieces other than balls
    class A {
    public:
        // feature quantity name
        static constexpr const char* kName = "A";

        // Hash value embedded in the evaluation function file
        static constexpr std::uint32_t kHashValue = 0x7A4C414Cu;

        // number of feature dimensions
        static constexpr IndexType kDimensions = PS_END2;

        // The maximum value of the number of indexes whose value is 1 at the same time among the feature values
        static constexpr IndexType kMaxActiveDimensions = 32;

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

#endif // #ifndef _NNUE_FEATURES_UNION_P_K_H_
