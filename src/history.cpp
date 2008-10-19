/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008 Marco Costalba

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

#include "history.h"


////
//// Functions
////

/// Constructor

History::History() {
  this->clear();
}


/// History::clear() clears the history tables.

void History::clear() {
  memset(history, 0, 2 * 8 * 64 * sizeof(int));
  memset(successCount, 0, 2 * 8 * 64 * sizeof(int));
  memset(failureCount, 0, 2 * 8 * 64 * sizeof(int));
}


/// History::success() registers a move as being successful.  This is done
/// whenever a non-capturing move causes a beta cutoff in the main search.
/// The three parameters are the moving piece, the move itself, and the
/// search depth.

void History::success(Piece p, Move m, Depth d) {
  assert(piece_is_ok(p));
  assert(move_is_ok(m));

  history[p][move_to(m)] += int(d) * int(d);
  successCount[p][move_to(m)]++;

  // Prevent history overflow:
  if(history[p][move_to(m)] >= HistoryMax)
    for(int i = 0; i < 16; i++)
      for(int j = 0; j < 64; j++)
        history[i][j] /= 2;
}


/// History::failure() registers a move as being unsuccessful.  The function is
/// called for each non-capturing move which failed to produce a beta cutoff
/// at a node where a beta cutoff was finally found.

void History::failure(Piece p, Move m) {
  assert(piece_is_ok(p));
  assert(move_is_ok(m));

  failureCount[p][move_to(m)]++;
}


/// History::move_ordering_score() returns an integer value used to order the
/// non-capturing moves in the MovePicker class.

int History::move_ordering_score(Piece p, Move m) const {
  assert(piece_is_ok(p));
  assert(move_is_ok(m));

  return history[p][move_to(m)];
}


/// History::ok_to_prune() decides whether a move has been sufficiently
/// unsuccessful that it makes sense to prune it entirely. 

bool History::ok_to_prune(Piece p, Move m, Depth d) const {
  assert(piece_is_ok(p));
  assert(move_is_ok(m));

  return (int(d) * successCount[p][move_to(m)] < failureCount[p][move_to(m)]);
}
