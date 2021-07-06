/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

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

#ifndef HISTORY_H_INCLUDED
#define HISTORY_H_INCLUDED

#include <array>
#include <limits>
#include <type_traits>

#include "types.h"

namespace Stockfish {

/// StatsEntry stores the stat table value. It is usually a number but could
/// be a move or even a nested history. We use a class instead of naked value
/// to directly call history update operator<<() on the entry so to use stats
/// tables at caller sites as simple multi-dim arrays.
template<typename T, int D>
class StatsEntry {

  T entry;

public:
  void operator=(const T& v) { entry = v; }
  T* operator&() { return &entry; }
  T* operator->() { return &entry; }
  operator const T&() const { return entry; }

  void operator<<(int bonus) {
    assert(abs(bonus) <= D); // Ensure range is [-D, D]
    static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");

    entry += bonus - entry * abs(bonus) / D;

    assert(abs(entry) <= D);
  }
};

/// Stats is a generic N-dimensional array used to store various statistics.
/// The first template parameter T is the base type of the array, the second
/// template parameter D limits the range of updates in [-D, D] when we update
/// values with the << operator, while the last parameters (Size and Sizes)
/// encode the dimensions of the array.
template <typename T, int D, int Size, int... Sizes>
struct Stats : public std::array<Stats<T, D, Sizes...>, Size>
{
  using Self = Stats<T, D, Size, Sizes...>;

  void fill(const T& v) {

    // For standard-layout 'this' points to first struct member
    assert(std::is_standard_layout<Self>::value);

    typedef StatsEntry<T, D> entry;
    entry* p = reinterpret_cast<entry*>(this);
    std::fill(p, p + sizeof(*this) / sizeof(entry), v);
  }
};

template <typename T, int D, int Size>
struct Stats<T, D, Size> : public std::array<StatsEntry<T, D>, Size> {};

/// In stats table, D=0 means that the template parameter is not used
enum StatsParams { NOT_USED = 0 };
enum StatsType { NoCaptures, Captures };

/// ButterflyHistory records how often quiet moves have been successful or
/// unsuccessful during the current search, and is used for reduction and move
/// ordering decisions. It uses 2 tables (one for each color) indexed by
/// the move's from and to squares, see www.chessprogramming.org/Butterfly_Boards
using ButterflyHistory      = Stats<       int16_t,    13365, COLOR_NB, int(SQUARE_NB) * int(SQUARE_NB)>;

/// At higher depths LowPlyHistory records successful quiet moves near the root
/// and quiet moves which are/were in the PV (ttPv). It is cleared with each new
/// search and filled during iterative deepening.
constexpr int MAX_LPH = 4;
using LowPlyHistory         = Stats<       int16_t,    10692, MAX_LPH, int(SQUARE_NB) * int(SQUARE_NB)>;

/// CounterMoveHistory stores counter moves indexed by [piece][to] of the previous
/// move, see www.chessprogramming.org/Countermove_Heuristic
using CounterMoveHistory    = Stats<          Move, NOT_USED, PIECE_NB, SQUARE_NB>;

/// CapturePieceToHistory is addressed by a move's [piece][to][captured piece type]
using CapturePieceToHistory = Stats<       int16_t,    10692, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;

/// PieceToHistory is like ButterflyHistory but is addressed by a move's [piece][to]
using PieceToHistory        = Stats<       int16_t,    29952, PIECE_NB, SQUARE_NB>;

/// ContinuationHistory is the combined history of a given pair of moves, usually
/// the current one given a previous one. The nested history table is based on
/// PieceToHistory instead of ButterflyBoards.
using ContinuationHistory   = Stats<PieceToHistory, NOT_USED, PIECE_NB, SQUARE_NB>;

}

#endif // #ifndef HISTORY_H_INCLUDED