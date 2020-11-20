#include "a.h"
#include "index_list.h"

// Definition of input feature A of NNUE evaluation function
namespace Eval::NNUE::Features {

    // Orient a square according to perspective (rotate the board 180° for black)
    // Important note for "halfka": this arch was designed with "flip" in mind 
    // although it still is untested which approach is better.
    // this has to stay until we find a better arch that works with "flip".
    // allows us to use current master net for gensfen (primarily needed for higher quality data)
    inline Square orient(Color perspective, Square s) {
        return Square(int(s) ^ (bool(perspective) * 63));
    }

    // Find the index of the feature quantity from the king position and PieceSquare
    inline IndexType A::make_index(
        Color perspective, Square s, Piece pc) {
        return IndexType(orient(perspective, s) + kpp_board_index[pc][perspective]);
    }

    // Get a list of indices with a value of 1 among the features
    void A::append_active_indices(
        const Position& pos,
        Color perspective,
        IndexList* active) {

        Bitboard bb = pos.pieces();
        while (bb) {
            Square s = pop_lsb(&bb);
            active->push_back(make_index(perspective, s, pos.piece_on(s)));
        }
    }

    // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
    void A::append_changed_indices(
        const Position& pos,
        Color perspective,
        IndexList* removed,
        IndexList* added) {

        const auto& dp = pos.state()->dirtyPiece;
        for (int i = 0; i < dp.dirty_num; ++i) {
            Piece pc = dp.piece[i];

            if (dp.from[i] != SQ_NONE)
              removed->push_back(make_index(perspective, dp.from[i], pc));

            if (dp.to[i] != SQ_NONE)
              added->push_back(make_index(perspective, dp.to[i], pc));
        }
    }

}  // namespace Eval::NNUE::Features
