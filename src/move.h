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
inline void insertion_sort(K firstMove, K lastMove)
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

// Our dedicated sort in range [firstMove, lastMove), first splits
// positive scores from ramining then order seaprately the two sets.
template<typename T>
inline void sort_moves(T* firstMove, T* lastMove, T** lastPositive)
{
    T tmp;
    T *p, *d;

    d = lastMove;
    p = firstMove - 1;

    d->score = -1; // right guard

    // Split positives vs non-positives
    do {
        while ((++p)->score > 0) {}

        if (p != d)
        {
            while (--d != p && d->score <= 0) {}

            tmp = *p;
            *p = *d;
            *d = tmp;
        }

    } while (p != d);

    // Sort just positive scored moves, remaining only when we get there
    insertion_sort<T, T*>(firstMove, p);
    *lastPositive = p;
}

// Picks up the best move in range [curMove, lastMove), one per cycle.
// It is faster then sorting all the moves in advance when moves are few,
// as normally are the possible captures. Note that is not a stable alghoritm.
template<typename T>
inline T pick_best(T* curMove, T* lastMove)
{
    T bestMove, tmp;

    bestMove = *curMove;
    while (++curMove != lastMove)
    {
        if (bestMove < *curMove)
        {
            tmp = *curMove;
            *curMove = bestMove;
            bestMove = tmp;
        }
    }
    return bestMove;
}


inline Square move_from(Move m) {
  return Square((int(m) >> 6) & 0x3F);
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

inline bool move_is_short_castle(Move m) {
  return move_is_castle(m) && (move_to(m) > move_from(m));
}

inline bool move_is_long_castle(Move m) {
  return move_is_castle(m) && (move_to(m) < move_from(m));
}

inline PieceType move_promotion_piece(Move m) {
  return move_is_promotion(m) ? PieceType(((int(m) >> 12) & 3) + 2) : PIECE_TYPE_NONE;
}

inline Move make_move(Square from, Square to) {
  return Move(int(to) | (int(from) << 6));
}

inline Move make_promotion_move(Square from, Square to, PieceType promotion) {
  return Move(int(to) | (int(from) << 6) | ((int(promotion) - 2) << 12) | (1 << 14));
}

inline Move make_ep_move(Square from, Square to) {
  return Move(int(to) | (int(from) << 6) | (2 << 14));
}

inline Move make_castle_move(Square from, Square to) {
  return Move(int(to) | (int(from) << 6) | (3 << 14));
}

inline bool move_is_ok(Move m) {
  return move_from(m) != move_to(m); // Catches also MOVE_NONE
}

class Position;

extern const std::string move_to_uci(Move m, bool chess960);
extern Move move_from_uci(const Position& pos, const std::string& str);
extern const std::string move_to_san(Position& pos, Move m);
extern const std::string pretty_pv(Position& pos, int depth, Value score, int time, Move pv[]);

#endif // !defined(MOVE_H_INCLUDED)
