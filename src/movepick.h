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

#include "movegen.h"
#include "position.h"
#include "types.h"

/// StatBoards is a generic 2-dimensional array used to store various statistics
template<int Size1, int Size2, typename T = int16_t>
struct StatBoards : public std::array<std::array<T, Size2>, Size1> {

  void fill(const T& v) {
    T* p = &(*this)[0][0];
    std::fill(p, p + sizeof(*this) / sizeof(*p), v);
  }

  void update(T& entry, int bonus, const int D) {

    assert(abs(bonus) <= D); // Ensure range is [-32 * D, 32 * D]
    assert(abs(32 * D) < (std::numeric_limits<T>::max)()); // Ensure we don't overflow

    entry += bonus * 32 - entry * abs(bonus) / D;

    assert(abs(entry) <= 32 * D);
  }
};

/// StatCubes is a generic 3-dimensional array used to store various statistics
template<int Size1, int Size2, int Size3, typename T = int16_t>
struct StatCubes : public std::array<std::array<std::array<T, Size3>, Size2>, Size1> {

  void fill(const T& v) {
    T* p = &(*this)[0][0][0];
    std::fill(p, p + sizeof(*this) / sizeof(*p), v);
  }

  void update(T& entry, int bonus, const int D, const int W) {

    assert(abs(bonus) <= D); // Ensure range is [-W * D, W * D]
    assert(abs(W * D) < (std::numeric_limits<T>::max)()); // Ensure we don't overflow

    entry += bonus * W - entry * abs(bonus) / D;

    assert(abs(entry) <= W * D);
  }
};

/// ButterflyBoards are 2 tables (one for each color) indexed by the move's from
/// and to squares, see chessprogramming.wikispaces.com/Butterfly+Boards
typedef StatBoards<COLOR_NB, int(SQUARE_NB) * int(SQUARE_NB)> ButterflyBoards;

/// PieceToBoards are addressed by a move's [piece][to] information
typedef StatBoards<PIECE_NB, SQUARE_NB> PieceToBoards;

/// CapturePieceToBoards are addressed by a move's [piece][to][captured piece type] information
typedef StatCubes<PIECE_NB, SQUARE_NB, PIECE_TYPE_NB> CapturePieceToBoards;

/// ButterflyHistory records how often quiet moves have been successful or
/// unsuccessful during the current search, and is used for reduction and move
/// ordering decisions. It uses ButterflyBoards as backing store.
struct ButterflyHistory : public ButterflyBoards {

  void update(Color c, Move m, int bonus) {
    StatBoards::update((*this)[c][from_to(m)], bonus, 324);
  }
};

/// PieceToHistory is like ButterflyHistory, but is based on PieceToBoards
struct PieceToHistory : public PieceToBoards {

  void update(Piece pc, Square to, int bonus) {
    StatBoards::update((*this)[pc][to], bonus, 936);
  }
};

/// CapturePieceToHistory is like PieceToHistory, but is based on CapturePieceToBoards
struct CapturePieceToHistory : public CapturePieceToBoards {

  void update(Piece pc, Square to, PieceType captured, int bonus) {
    StatCubes::update((*this)[pc][to][captured], bonus, 324, 2);
  }
};

/// CounterMoveHistory stores counter moves indexed by [piece][to] of the previous
/// move, see chessprogramming.wikispaces.com/Countermove+Heuristic
typedef StatBoards<PIECE_NB, SQUARE_NB, Move> CounterMoveHistory;

/// ContinuationHistory is the history of a given pair of moves, usually the
/// current one given a previous one. History table is based on PieceToBoards
/// instead of ButterflyBoards.
typedef StatBoards<PIECE_NB, SQUARE_NB, PieceToHistory> ContinuationHistory;


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
  Move ttMove, countermove, killers[2];
  ExtMove *cur, *endMoves, *endBadCaptures;
  int stage;
  Square recaptureSquare;
  Value threshold;
  Depth depth;
  ExtMove moves[MAX_MOVES];
};

#endif // #ifndef MOVEPICK_H_INCLUDED
