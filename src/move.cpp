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


////
//// Includes
////

#include <cassert>

#include "move.h"
#include "piece.h"
#include "position.h"
#include "ucioption.h"


////
//// Functions
////

/// move_from_string() takes a position and a string as input, and attempts to
/// convert the string to a move, using simple coordinate notation (g1f3,
/// a7a8q, etc.).  In order to correctly parse en passant captures and castling
/// moves, we need the position.  This function is not robust, and expects that
/// the input move is legal and correctly formatted.

Move move_from_string(const Position &pos, const std::string &str) {
  Square from, to;
  Piece piece;
  Color us = pos.side_to_move();

  if(str.length() < 4) return MOVE_NONE;

  // Read the from and to squares:
  from = square_from_string(str.substr(0, 2));
  to = square_from_string(str.substr(2, 4));

  // Find the moving piece:
  piece = pos.piece_on(from);

  // If the string has more than 4 characters, try to interpret the 5th
  // character as a promotion:
  if(type_of_piece(piece) == PAWN && str.length() >= 5) {
    switch(str[4]) {
    case 'n': case 'N': 
      return make_promotion_move(from, to, KNIGHT);
    case 'b': case 'B':
      return make_promotion_move(from, to, BISHOP);
    case 'r': case 'R':
      return make_promotion_move(from, to, ROOK);
    case 'q': case 'Q':
      return make_promotion_move(from, to, QUEEN);
    }
  }

  if(piece == king_of_color(us)) {
    // Is this a castling move?  A king move is assumed to be a castling
    // move if the destination square is occupied by a friendly rook, or
    // if the distance between the source and destination squares is more
    // than 1.
    if(pos.piece_on(to) == rook_of_color(us))
      return make_castle_move(from, to);
    else if(square_distance(from, to) > 1) {
      // This is a castling move, but we have to translate it to the
      // internal "king captures rook" representation.
      SquareDelta delta = (to > from)? DELTA_E : DELTA_W;
      Square s;
      for(s = from + delta;
          pawn_rank(us, s) == RANK_1 && pos.piece_on(s) != rook_of_color(us);
          s += delta);
      if(pawn_rank(us, s) == RANK_1 && pos.piece_on(s) == rook_of_color(us))
        return make_castle_move(from, s);
    }
  }
  else if(piece == pawn_of_color(us)) {
    // En passant move?  We assume that a pawn move is an en passant move
    // without further testing if the destination square is epSquare.
    if(to == pos.ep_square())
      return make_ep_move(from, to);
  }

  return make_move(from, to);
}


/// move_to_string() converts a move to a string in coordinate notation
/// (g1f3, a7a8q, etc.).  The only special case is castling moves, where we
/// print in the e1g1 notation in normal chess mode, and in e1h1 notation in
/// Chess960 mode.

const std::string move_to_string(Move move) {
  std::string str;

  if(move == MOVE_NONE)
    str = "(none)";
  else if(move == MOVE_NULL)
    str = "0000";
  else {
    if(!Chess960) {
      if(move_from(move) == SQ_E1 && move_is_short_castle(move)) {
        str = "e1g1"; return str;
      }
      else if(move_from(move) == SQ_E1 && move_is_long_castle(move)) {
        str = "e1c1"; return str;
      }
      if(move_from(move) == SQ_E8 && move_is_short_castle(move)) {
        str = "e8g8"; return str;
      }
      else if(move_from(move) == SQ_E8 && move_is_long_castle(move)) {
        str = "e8c8"; return str;
      }
    }
    str = square_to_string(move_from(move)) + square_to_string(move_to(move));
    if(move_promotion(move))
      str += piece_type_to_char(move_promotion(move), false);
  }
  return str;
}


/// Overload the << operator, to make it easier to print moves.

std::ostream &operator << (std::ostream &os, Move m) {
  return os << move_to_string(m);
}


/// move_is_ok(), for debugging.

bool move_is_ok(Move m) {
  return square_is_ok(move_from(m)) && square_is_ok(move_to(m));
}
