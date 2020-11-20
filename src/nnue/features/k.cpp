#include "k.h"
#include "index_list.h"

//Definition of input feature quantity K of NNUE evaluation function
namespace Eval::NNUE::Features {

    // Orient a square according to perspective (rotate the board 180° for black)
    // this has to stay until we find a better arch that works with "flip".
    // allows us to use current master net for gensfen (primarily needed for higher quality data)
    inline Square orient(Color perspective, Square s) {
        return Square(int(s) ^ (bool(perspective) * 63));
    }

    // Index of a feature for a given king position.
    IndexType K::make_index(Color perspective, Square s, Color king_color) {
        return IndexType(orient(perspective, s) + bool(perspective ^ king_color) * 64);
    }

    // Get a list of indices with a value of 1 among the features
    void K::append_active_indices(
        const Position& pos,
        Color perspective,
        IndexList* active) {

        for (auto color : Colors) {
          active->push_back(make_index(perspective, pos.square<KING>(color), color));
        }
    }

    // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
    void K::append_changed_indices(
        const Position& pos,
        Color perspective,
        IndexList* removed,
        IndexList* added) {

        const auto& dp = pos.state()->dirtyPiece;
        if (type_of(dp.piece[0]) == KING)
        {
            removed->push_back(make_index(perspective, dp.from[0], color_of(dp.piece[0])));
            added->push_back(make_index(perspective, dp.to[0], color_of(dp.piece[0])));
        }
    }

}  // namespace Eval::NNUE::Features
