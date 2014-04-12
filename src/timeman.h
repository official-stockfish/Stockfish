/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

/// The TimeManager class computes the optimal time to think depending on the
/// maximum available time, the game move number and other parameters.

class TimeManager {
public:
  void init(const Search::LimitsType& limits, int currentPly, Color us);
  void pv_instability(double bestMoveChanges) { unstablePvFactor = 1 + bestMoveChanges; }
  int available_time() const { return int(optimumSearchTime * unstablePvFactor * 0.71); }
  int maximum_time() const { return maximumSearchTime; }

private:
  int optimumSearchTime;
  int maximumSearchTime;
  double unstablePvFactor;
};

#endif // #ifndef TIMEMAN_H_INCLUDED
