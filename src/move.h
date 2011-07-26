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

#if !defined(MOVE_H_INCLUDED)
#define MOVE_H_INCLUDED

#include <string>

#include "misc.h"
#include "types.h"

// Maximum number of allowed moves per position
const int MAX_MOVES = 256;

/// A move needs 16 bits to be stored
///
/// bit  0- 5: destination square (from 0 to 63)
/// bit  6-11: origin square (from 0 to 63)
/// bit 12-13: promotion piece type - 2 (from KNIGHT-2 to QUEEN-2)
/// bit 14-15: special move flag: promotion (1), en passant (2), castle (3)
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

inline bool operator<(const MoveStack& f, const MoveStack& s) { return f.score < s.score; }

// An helper insertion sort implementation, works with pointers and iterators
template<typename T, typename K>
inline void sort(K firstMove, K lastMove)
{
    T value;
    K cur, p, d;

    if (firstMove != lastMove)
        for (cur = firstMove + 1; cur != lastMove; cur++)
        {
            p = d = cur;
            value = *p--;
            if (*p < value)
            {
                do *d = *p;
                while (--d != firstMove && *--p < value);
                *d = value;
            }
        }
}

inline Square move_from(Move m) {
  return Square((m >> 6) & 0x3F);
}

inline Square move_to(Move m) {
  return Square(m & 0x3F);
}

inline bool move_is_special(Move m) {
  return m & (3 << 14);
}

inline bool move_is_promotion(Move m) {
  return (m & (3 << 14)) == (1 << 14);
}

inline int move_is_ep(Move m) {
  return (m & (3 << 14)) == (2 << 14);
}

inline int move_is_castle(Move m) {
  return (m & (3 << 14)) == (3 << 14);
}

inline PieceType promotion_piece_type(Move m) {
  return PieceType(((m >> 12) & 3) + 2);
}

inline Move make_move(Square from, Square to) {
  return Move(to | (from << 6));
}

inline Move make_promotion_move(Square from, Square to, PieceType promotion) {
  return Move(to | (from << 6) | (1 << 14) | ((promotion - 2) << 12)) ;
}

inline Move make_ep_move(Square from, Square to) {
  return Move(to | (from << 6) | (2 << 14));
}

inline Move make_castle_move(Square from, Square to) {
  return Move(to | (from << 6) | (3 << 14));
}

inline bool move_is_ok(Move m) {
  return move_from(m) != move_to(m); // Catches also MOVE_NONE
}

class Position;

extern const std::string move_to_uci(Move m, bool chess960);
extern Move move_from_uci(const Position& pos, const std::string& str);
extern const std::string move_to_san(Position& pos, Move m);

#endif // !defined(MOVE_H_INCLUDED)
