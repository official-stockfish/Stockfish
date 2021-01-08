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

// Input features and network structure used in NNUE evaluation function

#ifndef NNUE_ARCHITECTURE_H_INCLUDED
#define NNUE_ARCHITECTURE_H_INCLUDED

// Defines the network structure
#include "architectures/halfkp_256x2-32-32.h"

namespace Eval::NNUE {

  static_assert(kTransformedFeatureDimensions % kMaxSimdWidth == 0, "");
  static_assert(Network::kOutputDimensions == 1, "");
  static_assert(std::is_same<Network::OutputType, std::int32_t>::value, "");

  // Trigger for full calculation instead of difference calculation
  constexpr auto kRefreshTriggers = RawFeatures::kRefreshTriggers;

}  // namespace Eval::NNUE

#endif // #ifndef NNUE_ARCHITECTURE_H_INCLUDED
