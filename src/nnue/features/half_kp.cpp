/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

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

namespace Eval::NNUE::Features {

  // Find the index of the feature quantity from the king position and PieceSquare
  template <Side AssociatedKing>
  inline IndexType HalfKP<AssociatedKing>::MakeIndex(Square sq_k, PieceSquare p) {
    return static_cast<IndexType>(PS_END) * static_cast<IndexType>(sq_k) + p;
  }

  // Get pieces information
  template <Side AssociatedKing>
  inline void HalfKP<AssociatedKing>::GetPieces(
      const Position& pos, Color perspective,
      PieceSquare** pieces, Square* sq_target_k) {

    *pieces = (perspective == BLACK) ?
        pos.eval_list()->piece_list_fb() :
        pos.eval_list()->piece_list_fw();
    const PieceId target = (AssociatedKing == Side::kFriend) ?
        static_cast<PieceId>(PIECE_ID_KING + perspective) :
        static_cast<PieceId>(PIECE_ID_KING + ~perspective);
    *sq_target_k = static_cast<Square>(((*pieces)[target] - PS_W_KING) % SQUARE_NB);
  }

  // Get a list of indices for active features
  template <Side AssociatedKing>
  void HalfKP<AssociatedKing>::AppendActiveIndices(
      const Position& pos, Color perspective, IndexList* active) {

    // Do nothing if array size is small to avoid compiler warning
    if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

    PieceSquare* pieces;
    Square sq_target_k;
    GetPieces(pos, perspective, &pieces, &sq_target_k);
    for (PieceId i = PIECE_ID_ZERO; i < PIECE_ID_KING; ++i) {
      if (pieces[i] != PS_NONE) {
        active->push_back(MakeIndex(sq_target_k, pieces[i]));
      }
    }
  }

  // Get a list of indices for recently changed features
  template <Side AssociatedKing>
  void HalfKP<AssociatedKing>::AppendChangedIndices(
      const Position& pos, Color perspective,
      IndexList* removed, IndexList* added) {

    PieceSquare* pieces;
    Square sq_target_k;
    GetPieces(pos, perspective, &pieces, &sq_target_k);
    const auto& dp = pos.state()->dirtyPiece;
    for (int i = 0; i < dp.dirty_num; ++i) {
      if (dp.pieceId[i] >= PIECE_ID_KING) continue;
      const auto old_p = static_cast<PieceSquare>(
          dp.old_piece[i].from[perspective]);
      if (old_p != PS_NONE) {
        removed->push_back(MakeIndex(sq_target_k, old_p));
      }
      const auto new_p = static_cast<PieceSquare>(
          dp.new_piece[i].from[perspective]);
      if (new_p != PS_NONE) {
        added->push_back(MakeIndex(sq_target_k, new_p));
      }
    }
  }

  template class HalfKP<Side::kFriend>;

}  // namespace Eval::NNUE::Features
