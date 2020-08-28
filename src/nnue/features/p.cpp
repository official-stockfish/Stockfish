//Definition of input feature P of NNUE evaluation function

#if defined(EVAL_NNUE)

#include "p.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Orient a square according to perspective (rotates by 180 for black)
inline Square orient(Color perspective, Square s) {
  return Square(int(s) ^ (bool(perspective) * 63));
}

// Find the index of the feature quantity from the king position and PieceSquare
inline IndexType P::MakeIndex(
  Color perspective, Square s, Piece pc) {
  return IndexType(orient(perspective, s) + kpp_board_index[pc][perspective]);
}

// Get a list of indices with a value of 1 among the features
void P::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  Bitboard bb = pos.pieces() & ~pos.pieces(KING);
  while (bb) {
    Square s = pop_lsb(&bb);
    active->push_back(MakeIndex(perspective, s, pos.piece_on(s)));
  }
}

// Get a list of indices whose values ​​have changed from the previous one in the feature quantity
void P::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  const auto& dp = pos.state()->dirtyPiece;
  for (int i = 0; i < dp.dirty_num; ++i) {
    Piece pc = dp.piece[i];
    if (type_of(pc) == KING) continue;
    if (dp.from[i] != SQ_NONE)
      removed->push_back(MakeIndex(perspective, dp.from[i], pc));
    if (dp.to[i] != SQ_NONE)
      added->push_back(MakeIndex(perspective, dp.to[i], pc));
  }
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
