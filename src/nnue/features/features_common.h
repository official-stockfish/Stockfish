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

//Common header of input features of NNUE evaluation function

#ifndef NNUE_FEATURES_COMMON_H_INCLUDED
#define NNUE_FEATURES_COMMON_H_INCLUDED

#include "../../evaluate.h"
#include "../nnue_common.h"

namespace Eval::NNUE::Features {

  class IndexList;

  template <typename... FeatureTypes>
  class FeatureSet;

  // Trigger to perform full calculations instead of difference only
  enum class TriggerEvent {
    kFriendKingMoved // calculate full evaluation when own king moves
  };

  enum class Side {
    kFriend // side to move
  };

}  // namespace Eval::NNUE::Features

#endif // #ifndef NNUE_FEATURES_COMMON_H_INCLUDED
