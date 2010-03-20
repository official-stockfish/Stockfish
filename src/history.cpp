/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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


////
//// Includes
////

#include <cassert>
#include <cstring>

#include "history.h"


////
//// Functions
////


/// Constructor

History::History() { clear(); }


/// History::clear() clears the history tables

void History::clear() {
  memset(history, 0, 2 * 8 * 64 * sizeof(int));
  memset(maxStaticValueDelta, 0, 2 * 8 * 64 * sizeof(int));
}


/// History::success() registers a move as being successful. This is done
/// whenever a non-capturing move causes a beta cutoff in the main search.
/// The three parameters are the moving piece, the destination square, and
/// the search depth.

void History::success(Piece p, Square to, Depth d) {

  assert(piece_is_ok(p));
  assert(square_is_ok(to));

  history[p][to] += int(d) * int(d);

  // Prevent history overflow
  if (history[p][to] >= HistoryMax)
      for (int i = 0; i < 16; i++)
          for (int j = 0; j < 64; j++)
              history[i][j] /= 2;
}


/// History::failure() registers a move as being unsuccessful. The function is
/// called for each non-capturing move which failed to produce a beta cutoff
/// at a node where a beta cutoff was finally found.

void History::failure(Piece p, Square to, Depth d) {

  assert(piece_is_ok(p));
  assert(square_is_ok(to));

  history[p][to] -= int(d) * int(d);

  // Prevent history underflow
  if (history[p][to] <= -HistoryMax)
      for (int i = 0; i < 16; i++)
          for (int j = 0; j < 64; j++)
              history[i][j] /= 2;
}


/// History::move_ordering_score() returns an integer value used to order the
/// non-capturing moves in the MovePicker class.

int History::move_ordering_score(Piece p, Square to) const {

  assert(piece_is_ok(p));
  assert(square_is_ok(to));

  return history[p][to];
}


/// History::set_gain() and History::gain() store and retrieve the
/// gain of a move given the delta of the static position evaluations
/// before and after the move.

void History::set_gain(Piece p, Square to, Value delta)
{
  if (delta >= maxStaticValueDelta[p][to])
      maxStaticValueDelta[p][to] = delta;
  else
      maxStaticValueDelta[p][to]--;
}

Value History::gain(Piece p, Square to) const
{
  return Value(maxStaticValueDelta[p][to]);
}
