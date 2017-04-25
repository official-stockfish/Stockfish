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

#ifndef MOVEPICK_H_INCLUDED
#define MOVEPICK_H_INCLUDED

#include <cstring>   // For std::memset

#include "movegen.h"
#include "position.h"
#include "types.h"


/// HistoryStats records how often quiet moves have been successful or unsuccessful
/// during the current search, and is used for reduction and move ordering decisions.
struct HistoryStats {

  static const int Max = 1 << 28;

  int get(Color c, Move m) const { return table[c][from_sq(m)][to_sq(m)]; }
  void clear() { std::memset(table, 0, sizeof(table)); }
  void update(Color c, Move m, int v) {

    Square from = from_sq(m);
    Square to = to_sq(m);

    const int D = 324;

    assert(abs(int(v)) <= D); // Consistency check for below formula

    table[c][from][to] -= table[c][from][to] * abs(int(v)) / D;
    table[c][from][to] += int(v) * 32;
  }

private:
  int table[COLOR_NB][SQUARE_NB][SQUARE_NB];
};


/// A template struct, used to generate MoveStats and CounterMoveHistoryStats:
/// MoveStats store the move that refute a previous one.
/// CounterMoveHistoryStats is like HistoryStats, but with two consecutive moves.
/// Entries are stored using only the moving piece and destination square, hence
/// two moves with different origin but same destination and piece will be
/// considered identical.
template<typename T>
struct Stats {
  const T* operator[](Piece pc) const { return table[pc]; }
  T* operator[](Piece pc) { return table[pc]; }
  void clear() { std::memset(table, 0, sizeof(table)); }
  void update(Piece pc, Square to, Move m) { table[pc][to] = m; }
  void update(Piece pc, Square to, int v) {

    const int D = 936;

    assert(abs(int(v)) <= D); // Consistency check for below formula

    table[pc][to] -= table[pc][to] * abs(int(v)) / D;
    table[pc][to] += int(v) * 32;
  }

private:
  T table[PIECE_NB][SQUARE_NB];
};

typedef Stats<Move> MoveStats;
typedef Stats<int> CounterMoveStats;
typedef Stats<CounterMoveStats> CounterMoveHistoryStats;


/// MovePicker class is used to pick one pseudo legal move at a time from the
/// current position. The most important method is next_move(), which returns a
/// new pseudo legal move each time it is called, until there are no moves left,
/// when MOVE_NONE is returned. In order to improve the efficiency of the alpha
/// beta algorithm, MovePicker attempts to return the moves which are most likely
/// to get a cut-off first.
namespace Search { struct Stack; }

class MovePicker {
public:
  MovePicker(const MovePicker&) = delete;
  MovePicker& operator=(const MovePicker&) = delete;

  MovePicker(const Position&, Move, Value);
  MovePicker(const Position&, Move, Depth, Square);
  MovePicker(const Position&, Move, Depth, Search::Stack*);

  Move next_move(bool skipQuiets = false);

private:
  template<GenType> void score();
  ExtMove* begin() { return cur; }
  ExtMove* end() { return endMoves; }

  const Position& pos;
  const Search::Stack* ss;
  Move countermove;
  Depth depth;
  Move ttMove;
  Square recaptureSquare;
  Value threshold;
  int stage;
  ExtMove *cur, *endMoves, *endBadCaptures;
  ExtMove moves[MAX_MOVES];
};

#endif // #ifndef MOVEPICK_H_INCLUDED
