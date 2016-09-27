/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

namespace {

  enum TimeType { OptimumTime, MaxTime };

  const int MoveHorizon   = 50;   // Plan time management at most this many moves ahead
  const double MaxRatio   = 7.09; // When in trouble, we can step over reserved time with this ratio
  const double StealRatio = 0.35; // However we must not steal time from remaining moves over this ratio


  // move_importance() is a skew-logistic function based on naive statistical
  // analysis of "how many games are still undecided after n half-moves". Game
  // is considered "undecided" as long as neither side has >275cp advantage.
  // Data was extracted from the CCRL game database with some simple filtering criteria.

  double move_importance(int ply) {

    const double XScale = 7.64;
    const double XShift = 58.4;
    const double Skew   = 0.183;

    return pow((1 + exp((ply - XShift) / XScale)), -Skew) + DBL_MIN; // Ensure non-zero
  }

  template<TimeType T>
  int remaining(int myTime, int movesToGo, int ply, int slowMover) {

    const double TMaxRatio   = (T == OptimumTime ? 1 : MaxRatio);
    const double TStealRatio = (T == OptimumTime ? 0 : StealRatio);

    double moveImportance = (move_importance(ply) * slowMover) / 100;
    double otherMovesImportance = 0;

    for (int i = 1; i < movesToGo; ++i)
        otherMovesImportance += move_importance(ply + 2 * i);

    double ratio1 = (TMaxRatio * moveImportance) / (TMaxRatio * moveImportance + otherMovesImportance);
    double ratio2 = (moveImportance + TStealRatio * otherMovesImportance) / (moveImportance + otherMovesImportance);

    return int(myTime * std::min(ratio1, ratio2)); // Intel C++ asks for an explicit cast
  }

} // namespace


/// init() is called at the beginning of the search and calculates the allowed
/// thinking time out of the time control and current game ply. We support four
/// different kinds of time controls, passed in 'limits':
///
///  inc == 0 && movestogo == 0 means: x basetime  [sudden death!]
///  inc == 0 && movestogo != 0 means: x moves in y minutes
///  inc >  0 && movestogo == 0 means: x basetime + z increment
///  inc >  0 && movestogo != 0 means: x moves in y minutes + z increment

void TimeManagement::init(Search::LimitsType& limits, Color us, int ply) {

  int minThinkingTime = Options["Minimum Thinking Time"];
  int moveOverhead    = Options["Move Overhead"];
  int slowMover       = Options["Slow Mover"];
  int npmsec          = Options["nodestime"];

  // If we have to play in 'nodes as time' mode, then convert from time
  // to nodes, and use resulting values in time management formulas.
  // WARNING: Given npms (nodes per millisecond) must be much lower then
  // the real engine speed to avoid time losses.
  if (npmsec)
  {
      if (!availableNodes) // Only once at game start
          availableNodes = npmsec * limits.time[us]; // Time is in msec

      // Convert from millisecs to nodes
      limits.time[us] = (int)availableNodes;
      limits.inc[us] *= npmsec;
      limits.npmsec = npmsec;
  }

  startTime = limits.startTime;
  optimumTime = maximumTime = std::max(limits.time[us], minThinkingTime);

  const int MaxMTG = limits.movestogo ? std::min(limits.movestogo, MoveHorizon) : MoveHorizon;

  // We calculate optimum time usage for different hypothetical "moves to go"-values
  // and choose the minimum of calculated search time values. Usually the greatest
  // hypMTG gives the minimum values.
  for (int hypMTG = 1; hypMTG <= MaxMTG; ++hypMTG)
  {
      // Calculate thinking time for hypothetical "moves to go"-value
      int hypMyTime =  limits.time[us]
                     + limits.inc[us] * (hypMTG - 1)
                     - moveOverhead * (2 + std::min(hypMTG, 40));

      hypMyTime = std::max(hypMyTime, 0);

      int t1 = minThinkingTime + remaining<OptimumTime>(hypMyTime, hypMTG, ply, slowMover);
      int t2 = minThinkingTime + remaining<MaxTime    >(hypMyTime, hypMTG, ply, slowMover);

      optimumTime = std::min(t1, optimumTime);
      maximumTime = std::min(t2, maximumTime);
  }

  if (Options["Ponder"])
      optimumTime += optimumTime / 4;
}
