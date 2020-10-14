#include "enpassant.h"
#include "index_list.h"

//Definition of input feature quantity EnPassant of NNUE evaluation function
namespace Eval::NNUE::Features {

    // Get a list of indices with a value of 1 among the features
    void EnPassant::AppendActiveIndices(
        const Position& pos, Color /* perspective */, IndexList* active) {

        // do nothing if array size is small to avoid compiler warning
        if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions)
            return;

        auto epSquare = pos.state()->epSquare;
        if (epSquare == SQ_NONE)
            return;

        auto file = file_of(epSquare);
        active->push_back(file);
    }

    // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
    void EnPassant::AppendChangedIndices(
        const Position& pos, Color /* perspective */,
        IndexList* removed, IndexList* added) {

        auto previous_epSquare = pos.state()->previous->epSquare;
        auto epSquare = pos.state()->epSquare;

        if (previous_epSquare != SQ_NONE) {
            if (epSquare != SQ_NONE && file_of(epSquare) == file_of(previous_epSquare))
                return;

            auto file = file_of(previous_epSquare);
            removed->push_back(file);
        }

        if (epSquare != SQ_NONE) {
            auto file = file_of(epSquare);
            added->push_back(file);
        }
    }

}  // namespace Eval::NNUE::Features
