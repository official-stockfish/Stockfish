//Definition of input feature quantity K of NNUE evaluation function

#if defined(EVAL_NNUE)

#include "k.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Get a list of indices with a value of 1 among the features
void K::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // do nothing if array size is small to avoid compiler warning
  if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

  const BonaPiece* pieces = (perspective == BLACK) ?
      pos.eval_list()->piece_list_fb() :
      pos.eval_list()->piece_list_fw();
  assert(pieces[PIECE_NUMBER_BKING] != BONA_PIECE_ZERO);
  assert(pieces[PIECE_NUMBER_WKING] != BONA_PIECE_ZERO);
  for (PieceNumber i = PIECE_NUMBER_KING; i < PIECE_NUMBER_NB; ++i) {
    active->push_back(pieces[i] - fe_end);
  }
}

// Get a list of indices whose values ​​have changed from the previous one in the feature quantity
void K::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  const auto& dp = pos.state()->dirtyPiece;
  if (dp.pieceNo[0] >= PIECE_NUMBER_KING) {
    removed->push_back(
        dp.changed_piece[0].old_piece.from[perspective] - fe_end);
    added->push_back(
        dp.changed_piece[0].new_piece.from[perspective] - fe_end);
  }
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
