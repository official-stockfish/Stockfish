//Definition of input feature quantity CastlingRight of NNUE evaluation function

#include "castling_right.h"
#include "index_list.h"

namespace Eval::NNUE::Features {

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

    for (Eval::NNUE::IndexType i = 0; i < kDimensions; ++i) {
      if (relative_castling_rights & (1 << i)) {
        active->push_back(i);
      }
    }
  }

  // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
  void CastlingRight::AppendChangedIndices(
    const Position& /* pos */, Color /* perspective */,
    IndexList* /* removed */, IndexList* /* added */) {
    // Not implemented.
    assert(false);
  }

}  // namespace Eval::NNUE::Features
