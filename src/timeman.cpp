/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "search.h"
#include "timeman.h"
#include "uci.h"

TimeManagement Time; // Our global time management object

/// init() is called at the beginning of the search and calculates the allowed
/// thinking time out of the time control and current game ply. We support four
/// different kinds of time controls, passed in 'limits':
///
///  inc == 0 && movestogo == 0 means: x basetime  [sudden death!]
///  inc == 0 && movestogo != 0 means: x moves in y minutes
///  inc >  0 && movestogo == 0 means: x basetime + z increment
///  inc >  0 && movestogo != 0 means: x moves in y minutes + z increment

void TimeManagement::init(Search::LimitsType& limits, Color us, int ply) {

  TimePoint minThinkingTime = Options["Minimum Thinking Time"];
  TimePoint moveOverhead    = Options["Move Overhead"];
  TimePoint slowMover       = Options["Slow Mover"];
  TimePoint npmsec          = Options["nodestime"];
  double scale;

  // If we have to play in 'nodes as time' mode, then convert from time
  // to nodes, and use resulting values in time management formulas.
  // WARNING: to avoid time losses, the given npmsec (nodes per millisecond)
  // must be much lower than the real engine speed.
  if (npmsec)
  {
      if (!availableNodes) // Only once at game start
          availableNodes = npmsec * limits.time[us]; // Time is in msec

      // Convert from milliseconds to nodes
      limits.time[us] = TimePoint(availableNodes);
      limits.inc[us] *= npmsec;
      limits.npmsec = npmsec;
  }

  startTime = limits.startTime;

  int mtg = limits.movestogo ? std::min(limits.movestogo, 50) : 50;

  TimePoint timeLeft =  std::max(TimePoint(0),
      limits.time[us] + limits.inc[us] * (mtg - 1) - moveOverhead * (2 + mtg));

  timeLeft = slowMover * timeLeft / 100;

  // For crazy fast games with little-to-no increment
  if (limits.time[us] < 11000 && limits.inc[us] < moveOverhead)
  {
      scale = std::max(6.0 * (9.0 - std::log2(ply + 1)), 2.0);
      optimumTime = std::max<int>(2 * minThinkingTime, timeLeft / scale);

      scale = std::max(1.7 * (8.0 - std::log2(ply + 1)), 0.5);
      if (3*minThinkingTime < timeLeft / scale)
          maximumTime = timeLeft / scale;
      else if (3 * minThinkingTime > 0.8 * limits.time[us] - moveOverhead)
          maximumTime = 0.8 * limits.time[us] - moveOverhead;
      else
          maximumTime = 3 * optimumTime / 2;
  }
  else  //all other games
  {
      double scale1 = std::max(2.0, 8.2 * (9.0 - std::log2(ply + 1)));
      optimumTime = std::min<int>(0.2 * limits.time[us], timeLeft / scale1);
      optimumTime = std::max<int>(minThinkingTime, optimumTime);

      double scale2 = std::max(0.5, 1.7 * (8.0 - std::log2(ply + 1)));
      maximumTime = std::min<int>(0.8 * limits.time[us] - moveOverhead, timeLeft / scale2);
      maximumTime = std::max<int>(minThinkingTime, maximumTime);
  }

  if (Options["Ponder"])
      optimumTime += optimumTime / 4;
}
