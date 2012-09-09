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

#if !defined MOVEPICK_H_INCLUDED
#define MOVEPICK_H_INCLUDED

#include "history.h"
#include "position.h"
#include "search.h"
#include "types.h"


/// MovePicker class is used to pick one pseudo legal move at a time from the
/// current position. The most important method is next_move(), which returns a
/// new pseudo legal move each time it is called, until there are no moves left,
/// when MOVE_NONE is returned. In order to improve the efficiency of the alpha
/// beta algorithm, MovePicker attempts to return the moves which are most likely
/// to get a cut-off first.

class MovePicker {

  MovePicker& operator=(const MovePicker&); // Silence a warning under MSVC

public:
  MovePicker(const Position&, Move, Depth, const History&, Search::Stack*, Value);
  MovePicker(const Position&, Move, Depth, const History&, Square);
  MovePicker(const Position&, Move, const History&, PieceType);
  template<bool SpNode> Move next_move();

private:
  void score_captures();
  void score_noncaptures();
  void score_evasions();
  void generate_next();

  const Position& pos;
  const History& H;
  Search::Stack* ss;
  Depth depth;
  Move ttMove;
  MoveStack killers[2];
  Square recaptureSquare;
  int captureThreshold, phase;
  MoveStack *cur, *end, *endQuiets, *endBadCaptures;
  MoveStack moves[MAX_MOVES];
};

#endif // !defined(MOVEPICK_H_INCLUDED)
