/*
  Glaurung, a UCI chess playing engine.
  Copyright (C) 2004-2008 Tord Romstad

  Glaurung is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  Glaurung is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#if !defined(MOVE_H_INCLUDED)
#define MOVE_H_INCLUDED

////
//// Includes
////

#include <iostream>

#include "misc.h"
#include "piece.h"
#include "square.h"


////
//// Types
////

class Position;

enum Move {
  MOVE_NONE = 0,
  MOVE_NULL = 65, 
  MOVE_MAX = 0xFFFFFF
};


struct MoveStack {
  Move move;
  int score;
};


////
//// Inline functions
////

inline Square move_from(Move m) {
  return Square((int(m) >> 6) & 077);
}

inline Square move_to(Move m) {
  return Square(m & 077);
}

inline PieceType move_promotion(Move m) {
  return PieceType((int(m) >> 12) & 7);
}

inline bool move_is_ep(Move m) {
  return bool((int(m) >> 15) & 1);
}

inline bool move_is_castle(Move m) {
  return bool((int(m) >> 16) & 1);
}

inline bool move_is_short_castle(Move m) {
  return move_is_castle(m) && (move_to(m) > move_from(m));
}

inline bool move_is_long_castle(Move m) {
  return move_is_castle(m) && (move_to(m) < move_from(m));
}

inline Move make_promotion_move(Square from, Square to, PieceType promotion) {
  return Move(int(to) | (int(from) << 6) | (int(promotion) << 12));
}

inline Move make_move(Square from, Square to) {
  return Move(int(to) | (int(from) << 6));
}

inline Move make_castle_move(Square from, Square to) {
  return Move(int(to) | (int(from) << 6) | (1 << 16));
}

inline Move make_ep_move(Square from, Square to) {
  return Move(int(to) | (int(from) << 6) | (1 << 15));
}


////
//// Prototypes
////

extern std::ostream &operator << (std::ostream &os, Move m);
extern Move move_from_string(const Position &pos, const std::string &str);
extern const std::string move_to_string(Move m);
extern bool move_is_ok(Move m);


#endif // !defined(MOVE_H_INCLUDED)
