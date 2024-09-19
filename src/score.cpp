/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#include "score.h"

#include <cassert>
#include <cmath>
#include <cstdlib>

#include "uci.h"

namespace Stockfish {

Score::Score(Value v, const Position& pos) {
    assert(-VALUE_INFINITE < v && v < VALUE_INFINITE);

    if (std::abs(v) < VALUE_TB_WIN_IN_MAX_PLY)
    {
        score = InternalUnits{UCIEngine::to_cp(v, pos)};
    }
    else if (std::abs(v) <= VALUE_TB)
    {
        auto distance = VALUE_TB - std::abs(v);
        score         = (v > 0) ? Tablebase{distance, true} : Tablebase{-distance, false};
    }
    else
    {
        auto distance = VALUE_MATE - std::abs(v);
        score         = (v > 0) ? Mate{distance} : Mate{-distance};
    }
}

}