/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2018 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include <array>
#include <limits>
#include <type_traits>

#include "movegen.h"
#include "position.h"
#include "types.h"

/// StatsEntry stores the stat table value. It is usually a number but could
/// be a move or even a nested history. We use a class instead of naked value
/// to directly call history update operator<<() on the entry so to use stats
/// tables at caller sites as simple multi-dim arrays.
template<typename T, int W, int D>
class StatsEntry {

  static const bool IsInt = std::is_integral<T>::value;
  typedef typename std::conditional<IsInt, int, T>::type TT;

  T entry;

public:
  T* get() { return &entry; }
  void operator=(const T& v) { entry = v; }
  operator TT() const { return entry; }

  void operator<<(int bonus) {

    assert(abs(bonus) <= D); // Ensure range is [-W * D, W * D]
    assert(abs(W * D) < std::numeric_limits<T>::max()); // Ensure we don't overflow

    entry += bonus * W - entry * abs(bonus) / D;

    assert(abs(entry) <= W * D);
  }
};

/// Stats is a generic N-dimensional array used to store various statistics.
/// The first template T parameter is the base type of the array, the W parameter
/// is the weight applied to the bonuses when we update values with the << operator,
/// the D parameter limits the range of updates (range is [-W * D, W * D]), and
/// the last parameters (Size and Sizes) encode the dimensions of the array.
template <typename T, int W, int D, int Size, int... Sizes>
struct Stats : public std::array<Stats<T, W, D, Sizes...>, Size>
{
  T* get() { return this->at(0).get(); }

  void fill(const T& v) {
    T* p = get();
    std::fill(p, p + sizeof(*this) / sizeof(*p), v);
  }
};

template <typename T, int W, int D, int Size>
struct Stats<T, W, D, Size> : public std::array<StatsEntry<T, W, D>, Size> {
  T* get() { return this->at(0).get(); }
};

/// Different tables use different W/D parameter, name them to ease readibility
enum StatsParams { W2 = 2, W32 = 32, D324 = 324, D936 = 936, NOT_USED = 0 };

/// ButterflyHistory records how often quiet moves have been successful or
/// unsuccessful during the current search, and is used for reduction and move
/// ordering decisions. It uses 2 tables (one for each color) indexed by
/// the move's from and to squares, see chessprogramming.wikispaces.com/Butterfly+Boards
typedef Stats<int16_t, W32, D324, COLOR_NB, int(SQUARE_NB) * int(SQUARE_NB)> ButterflyHistory;

/// CounterMoveHistory stores counter moves indexed by [piece][to] of the previous
/// move, see chessprogramming.wikispaces.com/Countermove+Heuristic
typedef Stats<Move, NOT_USED, NOT_USED, PIECE_NB, SQUARE_NB> CounterMoveHistory;

/// CapturePieceToHistory is addressed by a move's [piece][to][captured piece type]
typedef Stats<int16_t, W2, D324, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB> CapturePieceToHistory;

/// PieceToHistory is like ButterflyHistory but is addressed by a move's [piece][to]
typedef Stats<int16_t, W32, D936, PIECE_NB, SQUARE_NB> PieceToHistory;

/// ContinuationHistory is the combined history of a given pair of moves, usually
/// the current one given a previous one. The nested history table is based on
/// PieceToHistory instead of ButterflyBoards.
typedef Stats<PieceToHistory, W32, NOT_USED, PIECE_NB, SQUARE_NB> ContinuationHistory;


/// MovePicker class is used to pick one pseudo legal move at a time from the
/// current position. The most important method is next_move(), which returns a
/// new pseudo legal move each time it is called, until there are no moves left,
/// when MOVE_NONE is returned. In order to improve the efficiency of the alpha
/// beta algorithm, MovePicker attempts to return the moves which are most likely
/// to get a cut-off first.

class MovePicker {
public:
  MovePicker(const MovePicker&) = delete;
  MovePicker& operator=(const MovePicker&) = delete;
  MovePicker(const Position&, Move, Value, const CapturePieceToHistory*);
  MovePicker(const Position&, Move, Depth, const ButterflyHistory*,  const CapturePieceToHistory*, Square);
  MovePicker(const Position&, Move, Depth, const ButterflyHistory*, const CapturePieceToHistory*, const PieceToHistory**, Move, Move*);
  Move next_move(bool skipQuiets = false);

private:
  template<GenType> void score();
  ExtMove* begin() { return cur; }
  ExtMove* end() { return endMoves; }

  const Position& pos;
  const ButterflyHistory* mainHistory;
  const CapturePieceToHistory* captureHistory;
  const PieceToHistory** contHistory;
  Move ttMove, refutations[3];
  ExtMove *cur, *endMoves, *endBadCaptures;
  int stage;
  Square recaptureSquare;
  Value threshold;
  Depth depth;
  ExtMove moves[MAX_MOVES];
};

#endif // #ifndef MOVEPICK_H_INCLUDED
