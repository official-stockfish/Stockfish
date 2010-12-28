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

////
//// Includes
////

#include <iostream>

#include "misc.h"
#include "piece.h"
#include "square.h"

// Maximum number of allowed moves per position
const int MOVES_MAX = 256;

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

////
//// Inline functions
////

inline Square move_from(Move m) {
  return Square((int(m) >> 6) & 0x3F);
}

inline Square move_to(Move m) {
  return Square(m & 0x3F);
}

inline PieceType move_promotion_piece(Move m) {
  return PieceType((int(m) >> 12) & 7);
}

inline int move_is_special(Move m) {
  return m & (0x1F << 12);
}

inline int move_is_promotion(Move m) {
  return m & (7 << 12);
}

inline int move_is_ep(Move m) {
  return m & (1 << 15);
}

inline int move_is_castle(Move m) {
  return m & (1 << 16);
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

extern std::ostream& operator<<(std::ostream& os, Move m);
extern Move move_from_uci(const Position& pos, const std::string &str);
extern const std::string move_to_uci(Move m, bool chess960);
extern bool move_is_ok(Move m);


#endif // !defined(MOVE_H_INCLUDED)
