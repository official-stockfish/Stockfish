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

#include <algorithm> // For std::max
#include <cstring>   // For std::memset

#include "movegen.h"
#include "position.h"
#include "types.h"

/// A template struct, used to generate MoveStats and CounterMoveHistoryStats:
/// MoveStats store the move that refute a previous one.
/// CounterMoveHistoryStats is like HistoryStats, but with two consecutive moves.
/// HistoryStats records how often quiet moves have been successful or unsuccessful
/// during the current search, and is used for reduction and move ordering decisions.
/// Entries are stored using only the moving piece and destination square, hence
/// two moves with different origin but same destination and piece will be
/// considered identical.
template<typename T, int D1=PIECE_NB, int D2=SQUARE_NB>
struct Stats {
  static const Value Max = Value(1 << 28);

  const T* operator[](Piece pc) const { return table[pc]; }
  T* operator[](Piece pc) { return table[pc]; }
  const T* operator[](Move m) const { return table[fromto_bits(m)]; }
  void clear() { std::memset(table, 0, sizeof(table)); }
  void fill(const Value& v) { std::fill(&table[0][0], &table[PIECE_NB-1][SQUARE_NB-1]+1, v); };
  void update(Piece pc, Square to, Move m) { table[pc][to] = m; }
  template<int Denom>
  void update(int i, int j, Value v) {
      assert(abs(int(v)) <= Denom); // Needed for stability.

      table[i][j] -= table[i][j] * abs(int(v)) / Denom;
      table[i][j] += int(v) * 32;
  }
  void update(Piece pc, Square to, Value v) {

      update<936>(pc, to, v);
  }
  void update(Color c, Move m, Value v) {

      update<324>(fromto_bits(m), c, v);
  }

private:
  T table[D1][D2];
};

typedef Stats<Move> MoveStats;
typedef Stats<Value> CounterMoveStats;
typedef Stats<CounterMoveStats> CounterMoveHistoryStats;
typedef Stats<Value, 1<<12, COLOR_NB> HistoryStats;


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
