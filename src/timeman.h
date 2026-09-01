/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#ifndef TIMEMAN_H_INCLUDED
#define TIMEMAN_H_INCLUDED


#include <limits>

#include "misc.h"

namespace Stockfish {

class OptionsMap;
enum Color : u8;

namespace Search {
struct LimitsType;
}

// The TimeManagement class computes the optimal time to think depending on
// the maximum available time, the game move number, and other parameters.
class TimeManagement {
   public:
    void init(Search::LimitsType& limits,
              Color               us,
              int                 ply,
              const OptionsMap&   options,
              double&             originalTimeAdjust);

    TimePoint optimum() const;
    TimePoint maximum() const;
    template<typename FUNC>
    TimePoint elapsed(FUNC nodes) const {
        return useNodesTime ? TimePoint(nodes()) : elapsed_time();
    }
    TimePoint elapsed_time() const { return now() - startTime; };

    void clear();
    void advance_nodes_time(i64 nodes);

   private:
    static constexpr TimePoint NoBound = std::numeric_limits<TimePoint>::max() / 2;

    TimePoint startTime;
    TimePoint optimumTime = NoBound;
    TimePoint maximumTime = NoBound;

    // Related to 'nodes as time' mode:
    bool useNodesTime      = false;
    i64  availableNodes    = -1;
    int  previousMovesToGo = 0;
    i64  cyclicBudget      = 0;
};

}  // namespace Stockfish

#endif  // #ifndef TIMEMAN_H_INCLUDED
