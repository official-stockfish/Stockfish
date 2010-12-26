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


#if !defined(SQUARE_H_INCLUDED)
#define SQUARE_H_INCLUDED

////
//// Includes
////

#include <cstdlib> // for abs()
#include <string>

#include "color.h"
#include "misc.h"


////
//// Types
////

enum Square {
  SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
  SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
  SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
  SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
  SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
  SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
  SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
  SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
  SQ_NONE
};

enum File {
  FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, FILE_NONE
};

enum Rank {
  RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_NONE
};

enum SquareDelta {

  DELTA_N = 8, DELTA_E = 1, DELTA_S = -8, DELTA_W = -1, DELTA_NONE = 0,

  DELTA_NN = DELTA_N + DELTA_N,
  DELTA_NE = DELTA_N + DELTA_E,
  DELTA_SE = DELTA_S + DELTA_E,
  DELTA_SS = DELTA_S + DELTA_S,
  DELTA_SW = DELTA_S + DELTA_W,
  DELTA_NW = DELTA_N + DELTA_W
};

ENABLE_OPERATORS_ON(Square);
ENABLE_OPERATORS_ON(File);
ENABLE_OPERATORS_ON(Rank);
ENABLE_OPERATORS_ON(SquareDelta);


////
//// Constants
////

const int FlipMask = 56;
const int FlopMask =  7;

////
//// Inline functions
////

inline Square operator+ (Square x, SquareDelta i) { return x + Square(i); }
inline void operator+= (Square& x, SquareDelta i) { x = x + Square(i); }
inline Square operator- (Square x, SquareDelta i) { return x - Square(i); }
inline void operator-= (Square& x, SquareDelta i) { x = x - Square(i); }

inline Square make_square(File f, Rank r) {
  return Square(int(f) | (int(r) << 3));
}

inline File square_file(Square s) {
  return File(int(s) & 7);
}

inline Rank square_rank(Square s) {
  return Rank(int(s) >> 3);
}

inline Square flip_square(Square s) {
  return Square(int(s) ^ FlipMask);
}

inline Square flop_square(Square s) {
  return Square(int(s) ^ FlopMask);
}

inline Square relative_square(Color c, Square s) {
  return Square(int(s) ^ (int(c) * FlipMask));
}

inline Rank relative_rank(Color c, Square s) {
  return square_rank(relative_square(c, s));
}

inline SquareColor square_color(Square s) {
  return SquareColor((int(square_file(s)) + int(square_rank(s))) & 1);
}

inline bool same_color_squares(Square s1, Square s2) {
  int s = int(s1) ^ int(s2);
  return (((s >> 3) ^ s) & 1) == 0;
}

inline int file_distance(File f1, File f2) {
  return abs(int(f1) - int(f2));
}

inline int file_distance(Square s1, Square s2) {
  return file_distance(square_file(s1), square_file(s2));
}

inline int rank_distance(Rank r1, Rank r2) {
  return abs(int(r1) - int(r2));
}

inline int rank_distance(Square s1, Square s2) {
  return rank_distance(square_rank(s1), square_rank(s2));
}

inline int square_distance(Square s1, Square s2) {
  return Max(file_distance(s1, s2), rank_distance(s1, s2));
}

inline File file_from_char(char c) {
  return File(c - 'a') + FILE_A;
}

inline char file_to_char(File f) {
  return char(f - FILE_A + int('a'));
}

inline Rank rank_from_char(char c) {
  return Rank(c - '1') + RANK_1;
}

inline char rank_to_char(Rank r) {
  return char(r - RANK_1 + int('1'));
}

inline const std::string square_to_string(Square s) {
  return  std::string(1, file_to_char(square_file(s)))
        + std::string(1, rank_to_char(square_rank(s)));
}

inline bool file_is_ok(File f) {
  return f >= FILE_A && f <= FILE_H;
}

inline bool rank_is_ok(Rank r) {
  return r >= RANK_1 && r <= RANK_8;
}

inline bool square_is_ok(Square s) {
  return file_is_ok(square_file(s)) && rank_is_ok(square_rank(s));
}

#endif // !defined(SQUARE_H_INCLUDED)
