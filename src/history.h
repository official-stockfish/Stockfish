/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

#if !defined(HISTORY_H_INCLUDED)
#define HISTORY_H_INCLUDED

#include <algorithm>
#include <cstring>

#include "types.h"

/// The History class stores statistics about how often different moves
/// have been successful or unsuccessful during the current search. These
/// statistics are used for reduction and move ordering decisions. History
/// entries are stored according only to moving piece and destination square,
/// in particular two moves with different origin but same destination and
/// same piece will be considered identical.

class History {

public:
  void clear();
  Value value(Piece p, Square to) const;
  void add(Piece p, Square to, Value bonus);
  Value gain(Piece p, Square to) const;
  void update_gain(Piece p, Square to, Value g);

  static const Value MaxValue = Value(2000);

private:
  Value history[16][64];  // [piece][to_square]
  Value maxGains[16][64]; // [piece][to_square]
};

inline void History::clear() {
  memset(history,  0, 16 * 64 * sizeof(Value));
  memset(maxGains, 0, 16 * 64 * sizeof(Value));
}

inline Value History::value(Piece p, Square to) const {
  return history[p][to];
}

inline void History::add(Piece p, Square to, Value bonus) {
  if (abs(history[p][to] + bonus) < MaxValue) history[p][to] += bonus;
}

inline Value History::gain(Piece p, Square to) const {
  return maxGains[p][to];
}

inline void History::update_gain(Piece p, Square to, Value g) {
  maxGains[p][to] = std::max(g, maxGains[p][to] - 1);
}

#endif // !defined(HISTORY_H_INCLUDED)
