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
#include <cstdio>
#include <string>

#include "square.h"


////
//// Functions
////


/// Translating files, ranks and squares to/from characters and strings:

File file_from_char(char c) {
  return File(c - 'a') + FILE_A;
}


char file_to_char(File f) {
  return char(f - FILE_A) + 'a';
}


Rank rank_from_char(char c) {
  return Rank(c - '1') + RANK_1;
}


char rank_to_char(Rank r) {
  return char(r - RANK_1) + '1';
}


Square square_from_string(const std::string &str) {
  return make_square(file_from_char(str[0]), rank_from_char(str[1]));
}


const std::string square_to_string(Square s) {
  std::string str;
  str += file_to_char(square_file(s));
  str += rank_to_char(square_rank(s));
  return str;
}


/// file_is_ok(), rank_is_ok() and square_is_ok(), for debugging:

bool file_is_ok(File f) {
  return f >= FILE_A && f <= FILE_H;
}


bool rank_is_ok(Rank r) {
  return r >= RANK_1 && r <= RANK_8;
}


bool square_is_ok(Square s) {
  return file_is_ok(square_file(s)) && rank_is_ok(square_rank(s));
}
