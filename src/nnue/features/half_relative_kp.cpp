//Definition of input features HalfRelativeKP of NNUE evaluation function

#if defined(EVAL_NNUE)

#include "half_relative_kp.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Find the index of the feature quantity from the ball position and PieceSquare
template <Side AssociatedKing>
inline IndexType HalfRelativeKP<AssociatedKing>::MakeIndex(
    Square sq_k, PieceSquare p) {
  constexpr IndexType W = kBoardWidth;
  constexpr IndexType H = kBoardHeight;
  const IndexType piece_index = (p - PieceSquare::PS_W_PAWN) / SQUARE_NB;
  const Square sq_p = static_cast<Square>((p - PieceSquare::PS_W_PAWN) % SQUARE_NB);
  const IndexType relative_file = file_of(sq_p) - file_of(sq_k) + (W / 2);
  const IndexType relative_rank = rank_of(sq_p) - rank_of(sq_k) + (H / 2);
  return H * W * piece_index + H * relative_file + relative_rank;
}

// Get the piece information
template <Side AssociatedKing>
inline void HalfRelativeKP<AssociatedKing>::GetPieces(
    const Position& pos, Color perspective,
    PieceSquare** pieces, Square* sq_target_k) {
  *pieces = (perspective == BLACK) ?
      pos.eval_list()->piece_list_fb() :
      pos.eval_list()->piece_list_fw();
  const PieceId target = (AssociatedKing == Side::kFriend) ?
      static_cast<PieceId>(PieceId::PIECE_ID_KING + perspective) :
      static_cast<PieceId>(PieceId::PIECE_ID_KING + ~perspective);
  *sq_target_k = static_cast<Square>(((*pieces)[target] - PieceSquare::PS_W_KING) % SQUARE_NB);
}

// Get a list of indices with a value of 1 among the features
template <Side AssociatedKing>
void HalfRelativeKP<AssociatedKing>::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // do nothing if array size is small to avoid compiler warning
  if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

  PieceSquare* pieces;
  Square sq_target_k;
  GetPieces(pos, perspective, &pieces, &sq_target_k);
  for (PieceId i = PieceId::PIECE_ID_ZERO; i < PieceId::PIECE_ID_KING; ++i) {
    if (pieces[i] >= PieceSquare::PS_W_PAWN) {
      if (pieces[i] != PieceSquare::PS_NONE) {
        active->push_back(MakeIndex(sq_target_k, pieces[i]));
      }
    }
  }
}

// Get a list of indices whose values ​​have changed from the previous one in the feature quantity
template <Side AssociatedKing>
void HalfRelativeKP<AssociatedKing>::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  PieceSquare* pieces;
  Square sq_target_k;
  GetPieces(pos, perspective, &pieces, &sq_target_k);
  const auto& dp = pos.state()->dirtyPiece;
  for (int i = 0; i < dp.dirty_num; ++i) {
    if (dp.pieceId[i] >= PieceId::PIECE_ID_KING) continue;
    const auto old_p = static_cast<PieceSquare>(
        dp.old_piece[i].from[perspective]);
    if (old_p >= PieceSquare::PS_W_PAWN) {
      if (old_p != PieceSquare::PS_NONE) {
        removed->push_back(MakeIndex(sq_target_k, old_p));
      }
    }
    const auto new_p = static_cast<PieceSquare>(
        dp.new_piece[i].from[perspective]);
    if (new_p >= PieceSquare::PS_W_PAWN) {
      if (new_p != PieceSquare::PS_NONE) {
        added->push_back(MakeIndex(sq_target_k, new_p));
      }
    }
  }
}

template class HalfRelativeKP<Side::kFriend>;
template class HalfRelativeKP<Side::kEnemy>;

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
