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

#ifndef NNUE_FEATURES_HALF_KP_H_INCLUDED
#define NNUE_FEATURES_HALF_KP_H_INCLUDED

#include "../../evaluate.h"
#include "features_common.h"

namespace Stockfish::Eval::NNUE::Features {

  // Feature HalfKP: Combination of the position of own king
  // and the position of pieces other than kings
  template <Side AssociatedKing>
  class HalfKP {

   public:
    // Feature name
    static constexpr const char* Name = "HalfKP(Friend)";
    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t HashValue =
        0x5D69D5B9u ^ (AssociatedKing == Side::Friend);
    // Number of feature dimensions
    static constexpr IndexType Dimensions =
        static_cast<IndexType>(SQUARE_NB) * static_cast<IndexType>(PS_NB);
    // Maximum number of simultaneously active features
    static constexpr IndexType MaxActiveDimensions = 30; // Kings don't count
    // Trigger for full calculation instead of difference calculation
    static constexpr TriggerEvent RefreshTrigger = TriggerEvent::FriendKingMoved;

    // Get a list of indices for active features
    static void append_active_indices(const Position& pos, Color perspective,
                                      IndexList* active);

    // Get a list of indices for recently changed features
    static void append_changed_indices(const Position& pos, const DirtyPiece& dp, Color perspective,
                                       IndexList* removed, IndexList* added);
  };

}  // namespace Stockfish::Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURES_HALF_KP_H_INCLUDED
