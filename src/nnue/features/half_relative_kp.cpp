//Definition of input features HalfRelativeKP of NNUE evaluation function

#if defined(EVAL_NNUE)

#include "half_relative_kp.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Orient a square according to perspective (rotates by 180 for black)
inline Square orient(Color perspective, Square s) {
  return Square(int(s) ^ (bool(perspective) * 63));
}

// Find the index of the feature quantity from the ball position and PieceSquare
template <Side AssociatedKing>
inline IndexType HalfRelativeKP<AssociatedKing>::MakeIndex(
  Color perspective, Square s, Piece pc, Square sq_k) {
  const IndexType p = IndexType(orient(perspective, s) + kpp_board_index[pc][perspective]);
  return MakeIndex(sq_k, p);
}

// Find the index of the feature quantity from the ball position and PieceSquare
template <Side AssociatedKing>
inline IndexType HalfRelativeKP<AssociatedKing>::MakeIndex(
    Square sq_k, IndexType p) {
  constexpr IndexType W = kBoardWidth;
  constexpr IndexType H = kBoardHeight;
  const IndexType piece_index = (p - PS_W_PAWN) / SQUARE_NB;
  const Square sq_p = static_cast<Square>((p - PS_W_PAWN) % SQUARE_NB);
  const IndexType relative_file = file_of(sq_p) - file_of(sq_k) + (W / 2);
  const IndexType relative_rank = rank_of(sq_p) - rank_of(sq_k) + (H / 2);
  return H * W * piece_index + H * relative_file + relative_rank;
}

// Get a list of indices with a value of 1 among the features
template <Side AssociatedKing>
void HalfRelativeKP<AssociatedKing>::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  Square ksq = orient(perspective, pos.square<KING>(perspective));
  Bitboard bb = pos.pieces() & ~pos.pieces(KING);
  while (bb) {
    Square s = pop_lsb(&bb);
    active->push_back(MakeIndex(perspective, s, pos.piece_on(s), ksq));
  }
}

// Get a list of indices whose values ​​have changed from the previous one in the feature quantity
template <Side AssociatedKing>
void HalfRelativeKP<AssociatedKing>::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  Square ksq = orient(perspective, pos.square<KING>(perspective));
  const auto& dp = pos.state()->dirtyPiece;
  for (int i = 0; i < dp.dirty_num; ++i) {
    Piece pc = dp.piece[i];
    if (type_of(pc) == KING) continue;
    if (dp.from[i] != SQ_NONE)
      removed->push_back(MakeIndex(perspective, dp.from[i], pc, ksq));
    if (dp.to[i] != SQ_NONE)
      added->push_back(MakeIndex(perspective, dp.to[i], pc, ksq));
  }
}

template class HalfRelativeKP<Side::kFriend>;
template class HalfRelativeKP<Side::kEnemy>;

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
