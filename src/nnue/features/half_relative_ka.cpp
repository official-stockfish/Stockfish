#include "half_relative_ka.h"
#include "index_list.h"

//Definition of input features HalfRelativeKA of NNUE evaluation function
namespace Eval::NNUE::Features {

    // Orient a square according to perspective (flip rank for black)
    inline Square orient(Color perspective, Square s) {
        return Square(int(s) ^ (bool(perspective) * SQ_A8));
    }

    // Find the index of the feature quantity from the ball position and PieceSquare
    template <Side AssociatedKing>
    inline IndexType HalfRelativeKA<AssociatedKing>::make_index(
        Color perspective,
        Square s,
        Piece pc,
        Square sq_k) {

        const IndexType p = IndexType(orient(perspective, s) + kpp_board_index[pc][perspective]);
        return make_index(sq_k, p);
    }

    // Find the index of the feature quantity from the ball position and PieceSquare
    template <Side AssociatedKing>
    inline IndexType HalfRelativeKA<AssociatedKing>::make_index(
        Square sq_k,
        IndexType p) {

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
    void HalfRelativeKA<AssociatedKing>::append_active_indices(
        const Position& pos,
        Color perspective,
        IndexList* active) {

        Square ksq = orient(
            perspective,
            pos.square<KING>(
                AssociatedKing == Side::kFriend ? perspective : ~perspective));

        Bitboard bb = pos.pieces();
        while (bb) {
            Square s = pop_lsb(&bb);
            active->push_back(make_index(perspective, s, pos.piece_on(s), ksq));
        }
    }

    // Get a list of indices whose values ​​have changed from the previous one in the feature quantity
    template <Side AssociatedKing>
    void HalfRelativeKA<AssociatedKing>::append_changed_indices(
        const Position& pos,
        Color perspective,
        IndexList* removed,
        IndexList* added) {

        Square ksq = orient(
            perspective,
            pos.square<KING>(
                AssociatedKing == Side::kFriend ? perspective : ~perspective));

        const auto& dp = pos.state()->dirtyPiece;
        for (int i = 0; i < dp.dirty_num; ++i) {
            Piece pc = dp.piece[i];

            if (dp.from[i] != SQ_NONE)
                removed->push_back(make_index(perspective, dp.from[i], pc, ksq));

            if (dp.to[i] != SQ_NONE)
                added->push_back(make_index(perspective, dp.to[i], pc, ksq));
        }
    }

    template class HalfRelativeKA<Side::kFriend>;
    template class HalfRelativeKA<Side::kEnemy>;

}  // namespace Eval::NNUE::Features
