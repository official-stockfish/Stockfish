//Definition of input feature P of NNUE evaluation function

#if defined(EVAL_NNUE)

#include "p.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Get a list of indices with a value of 1 among the features
void P::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // do nothing if array size is small to avoid compiler warning
  if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

  const PieceSquare* pieces = (perspective == BLACK) ?
      pos.eval_list()->piece_list_fb() :
      pos.eval_list()->piece_list_fw();
  for (PieceId i = PieceId::PIECE_ID_ZERO; i < PieceId::PIECE_ID_KING; ++i) {
    if (pieces[i] != PieceSquare::PS_NONE) {
      active->push_back(pieces[i]);
    }
  }
}

// Get a list of indices whose values ​​have changed from the previous one in the feature quantity
void P::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  const auto& dp = pos.state()->dirtyPiece;
  for (int i = 0; i < dp.dirty_num; ++i) {
    if (dp.pieceId[i] >= PieceId::PIECE_ID_KING) continue;
    if (dp.old_piece[i].from[perspective] != PieceSquare::PS_NONE) {
      removed->push_back(dp.old_piece[i].from[perspective]);
    }
    if (dp.new_piece[i].from[perspective] != PieceSquare::PS_NONE) {
      added->push_back(dp.new_piece[i].from[perspective]);
    }
  }
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
