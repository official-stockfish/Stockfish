//Definition of input feature quantity K of NNUE evaluation function

#if defined(EVAL_NNUE)

#include "castling_right.h"
#include "index_list.h"

namespace Eval {

  namespace NNUE {

    namespace Features {

      // Get a list of indices with a value of 1 among the features
      void CastlingRight::AppendActiveIndices(
        const Position& pos, Color perspective, IndexList* active) {
        // do nothing if array size is small to avoid compiler warning
        if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

        int castling_rights = pos.state()->castlingRights;
        int relative_castling_rights;
        if (perspective == WHITE) {
          relative_castling_rights = castling_rights;
        }
        else {
          // Invert the perspective.
          relative_castling_rights = ((castling_rights & 3) << 2)
            & ((castling_rights >> 2) & 3);
        }

        for (int i = 0; i <kDimensions; ++i) {
          if (relative_castling_rights & (i << 1)) {
            active->push_back(i);
          }
        }
      }

      // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
      void CastlingRight::AppendChangedIndices(
        const Position& pos, Color perspective,
        IndexList* removed, IndexList* added) {

        int previous_castling_rights = pos.state()->previous->castlingRights;
        int current_castling_rights = pos.state()->castlingRights;
        int relative_previous_castling_rights;
        int relative_current_castling_rights;
        if (perspective == WHITE) {
          relative_previous_castling_rights = previous_castling_rights;
          relative_current_castling_rights = current_castling_rights;
        }
        else {
          // Invert the perspective.
          relative_previous_castling_rights = ((previous_castling_rights & 3) << 2)
            & ((previous_castling_rights >> 2) & 3);
          relative_current_castling_rights = ((current_castling_rights & 3) << 2)
            & ((current_castling_rights >> 2) & 3);
        }

        for (int i = 0; i < kDimensions; ++i) {
          if ((relative_previous_castling_rights & (i << 1)) &&
            (relative_current_castling_rights & (i << 1)) == 0) {
            removed->push_back(i);
          }
        }
      }

    }  // namespace Features

  }  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
