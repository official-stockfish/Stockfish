// NNUE評価関数の入力特徴量Kの定義

#if defined(EVAL_NNUE)

#include "k.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// 特徴量のうち、値が1であるインデックスのリストを取得する
void K::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // コンパイラの警告を回避するため、配列サイズが小さい場合は何もしない
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

// 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
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
