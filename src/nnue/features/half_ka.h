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

#ifndef NNUE_FEATURES_HALF_KA_H_INCLUDED
#define NNUE_FEATURES_HALF_KA_H_INCLUDED

#include "features_common.h"

#include "evaluate.h"

//Definition of input features HalfKPK of NNUE evaluation function
namespace Eval::NNUE::Features {

    // Feature HalfKPK: Combination of the position of own king
    // and the position of pieces other than kings
    template <Side AssociatedKing>
    class HalfKA {

    public:
        // Feature name
        static constexpr const char* kName = (AssociatedKing == Side::kFriend) ?
            "HalfKA(Friend)" : "HalfKA(Enemy)";

        // Hash value embedded in the evaluation file
        static constexpr std::uint32_t kHashValue =
            0x5F134CB9u ^ (AssociatedKing == Side::kFriend);

        // Number of feature dimensions
        static constexpr IndexType kDimensions =
            static_cast<IndexType>(SQUARE_NB) * static_cast<IndexType>(PS_END2);

        // Maximum number of simultaneously active features
        static constexpr IndexType kMaxActiveDimensions = 32;

        // Trigger for full calculation instead of difference calculation
        static constexpr TriggerEvent kRefreshTrigger =
            (AssociatedKing == Side::kFriend) ?
            TriggerEvent::kFriendKingMoved : TriggerEvent::kEnemyKingMoved;

        // Get a list of indices for active features
        static void append_active_indices(
            const Position& pos,
            Color perspective,
            IndexList* active);

        // Get a list of indices for recently changed features
        static void append_changed_indices(
            const Position& pos,
            Color perspective,
            IndexList* removed,
            IndexList* added);

    private:
        // Index of a feature for a given king position and another piece on some square
        static IndexType make_index(Color perspective, Square s, Piece pc, Square sq_k);
    };

}  // namespace Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURES_HALF_KA_H_INCLUDED
