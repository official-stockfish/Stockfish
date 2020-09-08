//Definition of input feature quantity K of NNUE evaluation function

#include "k.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Orient a square according to perspective (rotates by 180 for black)
inline Square orient(Color perspective, Square s) {
  return Square(int(s) ^ (bool(perspective) * 63));
}

// Index of a feature for a given king position.
IndexType K::MakeIndex(Color perspective, Square s, Color king_color) {
  return IndexType(orient(perspective, s) + bool(perspective ^ king_color) * 64);
}

// Get a list of indices with a value of 1 among the features
void K::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  for (auto color : Colors) {
    active->push_back(MakeIndex(perspective, pos.square<KING>(color), color));
  }
}

// Get a list of indices whose values ​​have changed from the previous one in the feature quantity
void K::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  const auto& dp = pos.state()->dirtyPiece;
  Color king_color;
  if (dp.piece[0] == Piece::W_KING) {
    king_color = WHITE;
  }
  else if (dp.piece[0] == Piece::B_KING) {
    king_color = BLACK;
  }
  else {
    return;
  }

  removed->push_back(MakeIndex(perspective, dp.from[0], king_color));
  added->push_back(MakeIndex(perspective, dp.to[0], king_color));
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval
