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


/*
  The code in this file is based on the opening book code in PolyGlot
  by Fabien Letouzey.  PolyGlot is available under the GNU General
  Public License, and can be downloaded from http://wbec-ridderkerk.nl
*/


#if !defined(BOOK_H_INCLUDED)
#define BOOK_H_INCLUDED


////
//// Includes
////

#include <fstream>
#include <string>

#include "move.h"
#include "position.h"


////
//// Types
////

struct BookEntry {
  uint64_t key;
  uint16_t move;
  uint16_t count;
  uint16_t n;
  uint16_t sum;
};

class Book : private std::ifstream {
  Book(const Book&); // just decleared..
  Book& operator=(const Book&); // ..to avoid a warning
public:
  Book() {}
  ~Book();
  void open(const std::string& fName);
  void close();
  const std::string file_name();
  Move get_move(const Position& pos);

private:
  Book& operator>>(uint64_t& n) { n = read_integer(8); return *this; }
  Book& operator>>(uint16_t& n) { n = (uint16_t)read_integer(2); return *this; }
  void operator>>(BookEntry& e) { *this >> e.key >> e.move >> e.count >> e.n >> e.sum; }

  uint64_t read_integer(int size);
  void read_entry(BookEntry& e, int n);
  int find_key(uint64_t key);

  std::string fileName;
  int bookSize;
};


////
//// Global variables
////

extern Book OpeningBook;


#endif // !defined(BOOK_H_INCLUDED)
