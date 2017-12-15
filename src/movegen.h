/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef MOVEGEN_H_INCLUDED
#define MOVEGEN_H_INCLUDED

#include <algorithm>

#include "types.h"

class Position;

enum GenType {
  CAPTURES,
  QUIETS,
  QUIET_CHECKS,
  EVASIONS,
  NON_EVASIONS,
  LEGAL
};

struct ExtMove {
  Move move;
  int value;

  operator Move() const { return move; }
  void operator=(Move m) { move = m; }

  // Inhibit unwanted implicit conversions to Move
  // with an ambiguity that yields to a compile error.
  operator float() const = delete;
};

inline bool operator<(const ExtMove& f, const ExtMove& s) {
  return f.value < s.value;
}

template<GenType>
ExtMove* generate(const Position& pos, ExtMove* moveList);

/// The MoveList struct is a simple wrapper around generate(). It sometimes comes
/// in handy to use this class instead of the low level generate() function.
template<GenType T>
struct MoveList {

  explicit MoveList(const Position& pos) : last(generate<T>(pos, moveList)) {}
  const ExtMove* begin() const { return moveList; }
  const ExtMove* end() const { return last; }
  size_t size() const { return last - moveList; }
  bool contains(Move move) const {
    return std::find(begin(), end(), move) != end();
  }

private:
  ExtMove moveList[MAX_MOVES], *last;
};

#endif // #ifndef MOVEGEN_H_INCLUDED
