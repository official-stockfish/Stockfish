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

#ifndef SKILL_H_INCLUDED
#define SKILL_H_INCLUDED

#include "types.h"
#include "search.h"

namespace Stockfish {
namespace Search {

struct Skill {
    // Add static constants for the lowest and highest Elo ratings
    static constexpr int LowestElo  = 1320;
    static constexpr int HighestElo = 3190;

    Skill(int skill_level, int uci_elo) {
        if (uci_elo)
        {
            // Use the constants instead of hardcoded values
            double e = double(uci_elo - LowestElo) / (HighestElo - LowestElo);
            level = std::clamp((((37.2473 * e - 40.8525) * e + 22.2943) * e - 0.311438), 0.0, 19.0);
        }
        else
            level = double(skill_level);
    }
    bool enabled() const { return level < 20.0; }
    bool time_to_pick(Depth depth) const { return depth == 1 + int(level); }
    Move pick_best(const RootMoves&, size_t multiPV);

    double level;
    Move   best = Move::none();
};

} // namespace Search
} // namespace Stockfish

#endif // #ifndef SKILL_H_INCLUDED
