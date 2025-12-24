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

#include "timeman.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "search.h"
#include "ucioption.h"

namespace Stockfish {

TimePoint TimeManagement::optimum() const { return optimumTime; }
TimePoint TimeManagement::maximum() const { return maximumTime; }

void TimeManagement::clear() {
    availableNodes = -1;  // When in 'nodes as time' mode
}

void TimeManagement::advance_nodes_time(std::int64_t nodes) {
    assert(useNodesTime);
    availableNodes = std::max(int64_t(0), availableNodes - nodes);
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply. We currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)
void TimeManagement::init(Search::LimitsType& limits,
                          Color               us,
                          int                 ply,
                          const OptionsMap&   options,
                          double&             originalTimeAdjust) {
    TimePoint npmsec = TimePoint(options["nodestime"]);

    // If we have no time, we don't need to fully initialize TM.
    // startTime is used by movetime and useNodesTime is used in elapsed calls.
    startTime    = limits.startTime;
    useNodesTime = npmsec != 0;

    if (limits.time[us] == 0)
        return;

    TimePoint moveOverhead = TimePoint(options["Move Overhead"]);

    // optScale is a percentage of available time to use for the current move.
    // maxScale is a multiplier applied to optimumTime.
    double optScale, maxScale;

    // If we have to play in 'nodes as time' mode, then convert from time
    // to nodes, and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given npmsec (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (useNodesTime)
    {
        if (availableNodes == -1)                       // Only once at game start
            availableNodes = npmsec * limits.time[us];  // Time is in msec

        // Convert from milliseconds to nodes
        limits.time[us] = TimePoint(availableNodes);
        limits.inc[us] *= npmsec;
        limits.npmsec = npmsec;
        moveOverhead *= npmsec;
    }

    // These numbers are used where multiplications, divisions or comparisons
    // with constants are involved.
    const int64_t   scaleFactor = useNodesTime ? npmsec : 1;
    const TimePoint scaledTime  = limits.time[us] / scaleFactor;
    const TimePoint scaledInc   = limits.inc[us] / scaleFactor;

    // Maximum move horizon of 50 moves
    int mtg = limits.movestogo ? std::min(limits.movestogo, 50) : 50;

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000 && double(mtg) / scaledInc > 0.05)
    {
        mtg = scaledTime * 0.05;
    }

    // Make sure timeLeft is > 0 since we may use it as a divisor
    TimePoint timeLeft = std::max(TimePoint(1), limits.time[us] + limits.inc[us] * (mtg - 1)
                                                  - moveOverhead * (2 + mtg));

    // x basetime (+ z increment)
    // If there is a healthy increment, timeLeft can exceed the actual available
    // game time for the current move, so also cap to a percentage of available game time.
    if (limits.movestogo == 0)
    {
        // Extra time according to timeLeft
        if (originalTimeAdjust < 0)
            originalTimeAdjust = 0.3285 * std::log10(timeLeft) - 0.4830;

        // Calculate time constants based on current time left.
        double logTimeInSec = std::log10(scaledTime / 1000.0);
        double optConstant  = std::min(0.00308 + 0.000319 * logTimeInSec, 0.00506);
        double maxConstant  = std::max(3.39 + 3.01 * logTimeInSec, 2.93);

        optScale = std::min(0.0122 + std::pow(ply + 2.95, 0.462) * optConstant,
                            0.213 * limits.time[us] / timeLeft)
                 * originalTimeAdjust;

        maxScale = std::min(6.64, maxConstant + ply / 12.0);
    }

    // x moves in y seconds (+ z increment)
    else
    {
        optScale = std::min((0.88 + ply / 116.4) / mtg, 0.88 * limits.time[us] / timeLeft);
        maxScale = std::min(6.3, 1.5 + 0.11 * mtg);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optScale * timeLeft);
    maximumTime =
      TimePoint(std::min(0.825 * limits.time[us] - moveOverhead, maxScale * optimumTime)) - 10;

    if (options["Ponder"])
        optimumTime += optimumTime / 4;
}

}  // namespace Stockfish
