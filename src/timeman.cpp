/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <cmath>
#include <algorithm>

#include "search.h"
#include "timeman.h"
#include "ucioption.h"

namespace {

  /// Constants

  const int MoveHorizon  = 50;    // Plan time management at most this many moves ahead
  const float MaxRatio   = 3.0f;  // When in trouble, we can step over reserved time with this ratio
  const float StealRatio = 0.33f; // However we must not steal time from remaining moves over this ratio


  // MoveImportance[] is based on naive statistical analysis of "how many games are still undecided
  // after n half-moves". Game is considered "undecided" as long as neither side has >275cp advantage.
  // Data was extracted from CCRL game database with some simple filtering criteria.
  const int MoveImportance[512] = {
    7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780, 7780,
    7780, 7780, 7780, 7780, 7778, 7778, 7776, 7776, 7776, 7773, 7770, 7768, 7766, 7763, 7757, 7751,
    7743, 7735, 7724, 7713, 7696, 7689, 7670, 7656, 7627, 7605, 7571, 7549, 7522, 7493, 7462, 7425,
    7385, 7350, 7308, 7272, 7230, 7180, 7139, 7094, 7055, 7010, 6959, 6902, 6841, 6778, 6705, 6651,
    6569, 6508, 6435, 6378, 6323, 6253, 6152, 6085, 5995, 5931, 5859, 5794, 5717, 5646, 5544, 5462,
    5364, 5282, 5172, 5078, 4988, 4901, 4831, 4764, 4688, 4609, 4536, 4443, 4365, 4293, 4225, 4155,
    4085, 4005, 3927, 3844, 3765, 3693, 3634, 3560, 3479, 3404, 3331, 3268, 3207, 3146, 3077, 3011,
    2947, 2894, 2828, 2776, 2727, 2676, 2626, 2589, 2538, 2490, 2442, 2394, 2345, 2302, 2243, 2192,
    2156, 2115, 2078, 2043, 2004, 1967, 1922, 1893, 1845, 1809, 1772, 1736, 1702, 1674, 1640, 1605,
    1566, 1536, 1509, 1479, 1452, 1423, 1388, 1362, 1332, 1304, 1289, 1266, 1250, 1228, 1206, 1180,
    1160, 1134, 1118, 1100, 1080, 1068, 1051, 1034, 1012, 1001, 980, 960, 945, 934, 916, 900, 888,
    878, 865, 852, 828, 807, 787, 770, 753, 744, 731, 722, 706, 700, 683, 676, 671, 664, 652, 641,
    634, 627, 613, 604, 591, 582, 568, 560, 552, 540, 534, 529, 519, 509, 495, 484, 474, 467, 460,
    450, 438, 427, 419, 410, 406, 399, 394, 387, 382, 377, 372, 366, 359, 353, 348, 343, 337, 333,
    328, 321, 315, 309, 303, 298, 293, 287, 284, 281, 277, 273, 265, 261, 255, 251, 247, 241, 240,
    235, 229, 218, 217, 213, 212, 208, 206, 197, 193, 191, 189, 185, 184, 180, 177, 172, 170, 170,
    170, 166, 163, 159, 158, 156, 155, 151, 146, 141, 138, 136, 132, 130, 128, 125, 123, 122, 118,
    118, 118, 117, 115, 114, 108, 107, 105, 105, 105, 102, 97, 97, 95, 94, 93, 91, 88, 86, 83, 80,
    80, 79, 79, 79, 78, 76, 75, 72, 72, 71, 70, 68, 65, 63, 61, 61, 59, 59, 59, 58, 56, 55, 54, 54,
    52, 49, 48, 48, 48, 48, 45, 45, 45, 44, 43, 41, 41, 41, 41, 40, 40, 38, 37, 36, 34, 34, 34, 33,
    31, 29, 29, 29, 28, 28, 28, 28, 28, 28, 28, 27, 27, 27, 27, 27, 24, 24, 23, 23, 22, 21, 20, 20,
    19, 19, 19, 19, 19, 18, 18, 18, 18, 17, 17, 17, 17, 17, 16, 16, 15, 15, 14, 14, 14, 12, 12, 11,
    9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 2, 2, 2, 2,
    2, 1, 1, 1, 1, 1, 1, 1 };

  int move_importance(int ply) { return MoveImportance[std::min(ply, 511)]; }


  /// Function Prototypes

  enum TimeType { OptimumTime, MaxTime };

  template<TimeType>
  int remaining(int myTime, int movesToGo, int fullMoveNumber, int slowMover);
}


void TimeManager::pv_instability(int curChanges, int prevChanges) {

  unstablePVExtraTime =  curChanges  * (optimumSearchTime / 2)
                       + prevChanges * (optimumSearchTime / 3);
}


void TimeManager::init(const Search::LimitsType& limits, int currentPly, Color us)
{
  /* We support four different kind of time controls:

      increment == 0 && movesToGo == 0 means: x basetime  [sudden death!]
      increment == 0 && movesToGo != 0 means: x moves in y minutes
      increment >  0 && movesToGo == 0 means: x basetime + z increment
      increment >  0 && movesToGo != 0 means: x moves in y minutes + z increment

    Time management is adjusted by following UCI parameters:

      emergencyMoveHorizon: Be prepared to always play at least this many moves
      emergencyBaseTime   : Always attempt to keep at least this much time (in ms) at clock
      emergencyMoveTime   : Plus attempt to keep at least this much time for each remaining emergency move
      minThinkingTime     : No matter what, use at least this much thinking before doing the move
  */

  int hypMTG, hypMyTime, t1, t2;

  // Read uci parameters
  int emergencyMoveHorizon = Options["Emergency Move Horizon"];
  int emergencyBaseTime    = Options["Emergency Base Time"];
  int emergencyMoveTime    = Options["Emergency Move Time"];
  int minThinkingTime      = Options["Minimum Thinking Time"];
  int slowMover            = Options["Slow Mover"];

  // Initialize to maximum values but unstablePVExtraTime that is reset
  unstablePVExtraTime = 0;
  optimumSearchTime = maximumSearchTime = limits.time[us];

  // We calculate optimum time usage for different hypothetic "moves to go"-values and choose the
  // minimum of calculated search time values. Usually the greatest hypMTG gives the minimum values.
  for (hypMTG = 1; hypMTG <= (limits.movestogo ? std::min(limits.movestogo, MoveHorizon) : MoveHorizon); hypMTG++)
  {
      // Calculate thinking time for hypothetic "moves to go"-value
      hypMyTime =  limits.time[us]
                 + limits.inc[us] * (hypMTG - 1)
                 - emergencyBaseTime
                 - emergencyMoveTime * std::min(hypMTG, emergencyMoveHorizon);

      hypMyTime = std::max(hypMyTime, 0);

      t1 = minThinkingTime + remaining<OptimumTime>(hypMyTime, hypMTG, currentPly, slowMover);
      t2 = minThinkingTime + remaining<MaxTime>(hypMyTime, hypMTG, currentPly, slowMover);

      optimumSearchTime = std::min(optimumSearchTime, t1);
      maximumSearchTime = std::min(maximumSearchTime, t2);
  }

  if (Options["Ponder"])
      optimumSearchTime += optimumSearchTime / 4;

  // Make sure that maxSearchTime is not over absoluteMaxSearchTime
  optimumSearchTime = std::min(optimumSearchTime, maximumSearchTime);
}


namespace {

  template<TimeType T>
  int remaining(int myTime, int movesToGo, int currentPly, int slowMover)
  {
    const float TMaxRatio   = (T == OptimumTime ? 1 : MaxRatio);
    const float TStealRatio = (T == OptimumTime ? 0 : StealRatio);

    int thisMoveImportance = move_importance(currentPly) * slowMover / 100;
    int otherMovesImportance = 0;

    for (int i = 1; i < movesToGo; i++)
        otherMovesImportance += move_importance(currentPly + 2 * i);

    float ratio1 = (TMaxRatio * thisMoveImportance) / float(TMaxRatio * thisMoveImportance + otherMovesImportance);
    float ratio2 = (thisMoveImportance + TStealRatio * otherMovesImportance) / float(thisMoveImportance + otherMovesImportance);

    return int(floor(myTime * std::min(ratio1, ratio2)));
  }
}
