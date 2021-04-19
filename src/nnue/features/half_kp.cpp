/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

//Definition of input features HalfKP of NNUE evaluation function

#include "half_kp.h"
#include "index_list.h"

namespace Stockfish::Eval::NNUE::Features {

  // Orient a square according to perspective (rotates by 180 for black)
  inline Square orient(Color perspective, Square s) {
    return Square(int(s) ^ (bool(perspective) * 63));
  }

  // Index of a feature for a given king position and another piece on some square
  inline IndexType make_index(Color perspective, Square s, Piece pc, Square ksq) {
    return IndexType(orient(perspective, s) + kpp_board_index[perspective][pc] + PS_END * ksq);
  }

  // Get a list of indices for active features
  template <Side AssociatedKing>
  void HalfKP<AssociatedKing>::AppendActiveIndices(
      const Position& pos, Color perspective, IndexList* active) {

    Square ksq = orient(perspective, pos.square<KING>(perspective));
    Bitboard bb = pos.pieces() & ~pos.pieces(KING);
    while (bb)
    {
      Square s = pop_lsb(bb);
      active->push_back(make_index(perspective, s, pos.piece_on(s), ksq));
    }
  }


  // AppendChangedIndices() : get a list of indices for recently changed features

  // IMPORTANT: The `pos` in this function is pretty much useless as it
  // is not always the position the features are updated to. The feature
  // transformer code right now can update multiple accumulators per move,
  // but since Stockfish only keeps the full state of the current leaf
  // search position it is not possible to always pass here the position for
  // which the accumulator is being updated. Therefore the only thing that
  // can be reliably extracted from `pos` is the king square for the king
  // of the `perspective` color (note: not even the other king's square will
  // match reality in all cases, this is also the reason why `dp` is passed
  // as a parameter and not extracted from pos.state()). This is of particular
  // problem for future nets with other feature sets, where updating the active
  // feature might require more information from the intermediate positions. In
  // this case the only easy solution is to remove the multiple updates from
  // the feature transformer update code and only update the accumulator for
  // the current leaf position (the position after the move).

  template <Side AssociatedKing>
  void HalfKP<AssociatedKing>::AppendChangedIndices(
      const Position& pos, const DirtyPiece& dp, Color perspective,
      IndexList* removed, IndexList* added) {

    Square ksq = orient(perspective, pos.square<KING>(perspective));
    for (int i = 0; i < dp.dirty_num; ++i) {
      Piece pc = dp.piece[i];
      if (type_of(pc) == KING) continue;
      if (dp.from[i] != SQ_NONE)
        removed->push_back(make_index(perspective, dp.from[i], pc, ksq));
      if (dp.to[i] != SQ_NONE)
        added->push_back(make_index(perspective, dp.to[i], pc, ksq));
    }
  }

  template class HalfKP<Side::kFriend>;

}  // namespace Stockfish::Eval::NNUE::Features
