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

  const PieceSquare* pieces = (perspective == BLACK) ?
      pos.eval_list()->piece_list_fb() :
      pos.eval_list()->piece_list_fw();
  assert(pieces[PieceId::PIECE_ID_BKING] != PieceSquare::PS_NONE);
  assert(pieces[PieceId::PIECE_ID_WKING] != PieceSquare::PS_NONE);
  for (PieceId i = PieceId::PIECE_ID_KING; i < PieceId::PIECE_ID_NONE; ++i) {
    active->push_back(pieces[i] - PieceSquare::PS_END);
  }
}

// Get a list of indices whose values ​​have changed from the previous one in the feature quantity
void K::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  const auto& dp = pos.state()->dirtyPiece;
  if (dp.pieceId[0] >= PieceId::PIECE_ID_KING) {
    removed->push_back(
        dp.old_piece[0].from[perspective] - PieceSquare::PS_END);
    added->push_back(
        dp.new_piece[0].from[perspective] - PieceSquare::PS_END);
  }
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
