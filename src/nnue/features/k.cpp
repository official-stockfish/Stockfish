//Definition of input feature quantity K of NNUE evaluation function

#include "k.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// Orient a square according to perspective (flip rank for black)
inline Square orient(Color perspective, Square s) {
  return Square(int(s) ^ (bool(perspective) * SQ_A8));
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
  if (type_of(dp.piece[0]) == KING)
  {
    removed->push_back(MakeIndex(perspective, dp.from[0], color_of(dp.piece[0])));
    added->push_back(MakeIndex(perspective, dp.to[0], color_of(dp.piece[0])));
  }
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval
