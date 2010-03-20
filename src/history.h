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


#if !defined(HISTORY_H_INCLUDED)
#define HISTORY_H_INCLUDED

////
//// Includes
////

#include "depth.h"
#include "move.h"
#include "piece.h"
#include "value.h"


////
//// Types
////

/// The History class stores statistics about how often different moves
/// have been successful or unsuccessful during the current search. These
/// statistics are used for reduction and move ordering decisions. History
/// entries are stored according only to moving piece and destination square,
/// in particular two moves with different origin but same destination and
/// same piece will be considered identical.

class History {

public:
  History();
  void clear();
  void success(Piece p, Square to, Depth d);
  void failure(Piece p, Square to, Depth d);
  int move_ordering_score(Piece p, Square to) const;
  void set_gain(Piece p, Square to, Value delta);
  Value gain(Piece p, Square to) const;

private:
  int history[16][64];  // [piece][square]
  int maxStaticValueDelta[16][64];  // [piece][from_square][to_square]
};


////
//// Constants and variables
////

/// HistoryMax controls how often the history counters will be scaled down:
/// When the history score for a move gets bigger than HistoryMax, all
/// entries in the table are divided by 2. It is difficult to guess what
/// the ideal value of this constant is. Scaling down the scores often has
/// the effect that parts of the search tree which have been searched
/// recently have a bigger importance for move ordering than the moves which
/// have been searched a long time ago.

const int HistoryMax = 50000 * OnePly;


#endif // !defined(HISTORY_H_INCLUDED)
