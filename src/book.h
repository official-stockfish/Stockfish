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

class Book {

public:
  // Constructors
  Book();

  // Open and close book files
  void open(const std::string &fName);
  void close();

  // Testing if a book is opened
  bool is_open() const;

  // The file name of the currently active book
  const std::string file_name() const;

  // Get a book move for a given position
  Move get_move(const Position &pos) const;

private:
  int find_key(uint64_t key) const;
  void read_entry(BookEntry &entry, int n) const;

  std::string fileName;
  FILE *bookFile;
  int bookSize;
};


////
//// Global variables
////

extern Book OpeningBook;


#endif // !defined(BOOK_H_INCLUDED)
