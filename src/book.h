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

#if !defined(BOOK_H_INCLUDED)
#define BOOK_H_INCLUDED

#include <fstream>
#include <string>

#include "move.h"
#include "position.h"
#include "rkiss.h"


// A Polyglot book is a series of "entries" of 16 bytes. All integers are
// stored highest byte first (regardless of size). The entries are ordered
// according to key. Lowest key first.
struct BookEntry {
  uint64_t key;
  uint16_t move;
  uint16_t count;
  uint32_t learn;
};

class Book {
public:
  Book();
  ~Book();
  void open(const std::string& fileName);
  void close();
  Move get_move(const Position& pos, bool findBestMove);
  const std::string name() const { return bookName; }

private:
  // read n chars from the file stream and converts them in an
  // integer number. Integers are stored with highest byte first.
  template<int n> uint64_t get_int();

  template<typename T>
  Book& operator>>(T& n) { n = (T)get_int<sizeof(T)>(); return *this; }

  BookEntry read_entry(int idx);
  int find_entry(uint64_t key);

  std::ifstream bookFile;
  std::string bookName;
  int bookSize;
  RKISS RKiss;
};

// Yes, we indulge a bit here ;-)
template<int n> inline uint64_t Book::get_int() { return 256 * get_int<n-1>() + bookFile.get(); }
template<> inline uint64_t Book::get_int<1>() { return bookFile.get(); }

#endif // !defined(BOOK_H_INCLUDED)
