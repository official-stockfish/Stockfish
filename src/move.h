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

/// A move needs 17 bits to be stored
///
/// bit  0- 5: destination square (from 0 to 63)
/// bit  6-11: origin square (from 0 to 63)
/// bit 12-14: promotion piece type
/// bit    15: en passant flag
/// bit    16: castle flag
///
/// Special cases are MOVE_NONE and MOVE_NULL. We can sneak these in
/// because in any normal move destination square is always different
/// from origin square while MOVE_NONE and MOVE_NULL have the same
/// origin and destination square, 0 and 1 respectively.

enum Move {
  MOVE_NONE = 0,
  MOVE_NULL = 65
};


struct MoveStack {
  Move move;
  int score;
};

// Note that operator< is set up such that std::sort() will sort in descending order
inline bool operator<(const MoveStack& f, const MoveStack& s) { return s.score < f.score; }


////
//// Inline functions
////

inline Square move_from(Move m) {
  return Square((int(m) >> 6) & 0x3F);
}

inline Square move_to(Move m) {
  return Square(m & 0x3F);
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
