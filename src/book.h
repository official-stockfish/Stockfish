/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef BOOK_H_INCLUDED
#define BOOK_H_INCLUDED

#include "position.h"
#include <vector>

class Book
{
    typedef struct {
        uint64_t key;
        uint16_t move;
        uint16_t weight;
        uint16_t depth;
        uint16_t score;
    } BookEntry;
public:
    Book();
    ~Book();
    void init(const std::string& filename);
    void set_max_ply(int new_max_ply);
    Move probe_root(Position& pos);
    void probe(Position& pos, Depth depth, std::vector<Move>& bookmoves);
private:
    size_t find_first_entry(uint64_t key, int& index_count);
    Move reconstruct_move(uint16_t book_move);
    size_t NumBookEntries;
    BookEntry *BookEntries;
    void byteswap_bookentry(BookEntry *be);
    int max_book_ply;
};

extern Book Books;

#endif // #ifndef BOOK_H_INCLUDED
