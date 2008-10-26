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


#if !defined(MOVEGEN_H_INCLUDED)
#define MOVEGEN_H_INCLUDED

////
//// Includes
////

#include "position.h"


////
//// Prototypes
////

extern int generate_captures(const Position &pos, MoveStack *mlist);
extern int generate_noncaptures(const Position &pos, MoveStack *mlist);
extern int generate_checks(const Position &pos, MoveStack *mlist, Bitboard dc);
extern int generate_evasions(const Position &pos, MoveStack *mlist);
extern int generate_legal_moves(const Position &pos, MoveStack *mlist);
extern bool move_is_legal(const Position &pos, const Move m, Bitboard pinned);


#endif // !defined(MOVEGEN_H_INCLUDED)
