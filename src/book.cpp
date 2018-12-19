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

#include "misc.h"
#include "book.h"
#include "movegen.h"
#include <algorithm>
#include <iostream>

Book Books;

Book::Book()
{
    max_book_ply = 400;
    NumBookEntries = 0;
    BookEntries = nullptr;
}

Book::~Book()
{
    if (BookEntries != nullptr)
        delete[] BookEntries;
}

void Book::init(const std::string& filename)
{
    if (filename.length() == 0) return;
    const char *fnam = filename.c_str();

    if (strcmp(fnam, "<empty>") == 0)
    {
        return;
    }

    FILE *fpt = fopen(fnam, "rb");
    if (fpt == NULL)
    {
        sync_cout << "info string Could not open " << filename << sync_endl;
        return;
    }

    if (BookEntries != nullptr)
    {
        free(BookEntries);
        BookEntries = nullptr;
    }

    fseek(fpt, 0L, SEEK_END);
    size_t filesize = ftell(fpt);
    fseek(fpt, 0L, SEEK_SET);

    NumBookEntries = filesize / sizeof(BookEntry);
    BookEntries = new BookEntry[NumBookEntries];

    size_t nRead = fread(BookEntries, 1, filesize, fpt);
    fclose(fpt);

    if (nRead != filesize)
    {
        free(BookEntries);
        BookEntries = nullptr;
        sync_cout << "info string Could not read " << filename << sync_endl;
        return;
    }
    for (size_t i = 0; i < NumBookEntries; i++)
        byteswap_bookentry(&BookEntries[i]);

    sync_cout << "info string Book loaded: " << filename << " (" << NumBookEntries << " entries)" << sync_endl;
}

void Book::set_max_ply(int new_max_ply)
{
    max_book_ply = new_max_ply;
}

Move Book::probe_root(Position& pos)
{
    if (BookEntries != nullptr && pos.game_ply() < max_book_ply)
    {
        int count = 0;
        size_t index = find_first_entry(pos.key(), count);
        if (count > 0)
        {
            PRNG rng(now());
            Move move = reconstruct_move(BookEntries[index + rng.rand<unsigned>() % count].move);

            // Add 'special move' flags and verify it is legal
            for (const auto& m : MoveList<LEGAL>(pos))
            {
                if (move == (m.move & (~(3 << 14))))  //  compare with MoveType (bit 14-15)  masked out
                    return m;
            }
        }
    }
    return MOVE_NONE;
}

void Book::probe(Position& pos, Depth depth, std::vector<Move>& bookmoves)
{
    if (BookEntries != nullptr && pos.game_ply() < max_book_ply)
    {
        int count = 0;
        size_t index = find_first_entry(pos.key(), count);
        if (count > 0)
        {
            for (int i = 0; i < count; i++)
            {
                if(40 / BookEntries[index + i].weight * ONE_PLY <= depth)
                    bookmoves.push_back(Move(BookEntries[index + i].move));
            }
        }
    }
}

Move Book::reconstruct_move(uint16_t book_move)
{
    Move move = Move(book_move);

    int pt = (move >> 12) & 7;
    if (pt)
        return make<PROMOTION>(from_sq(move), to_sq(move), PieceType(pt + 1));

    return move;
}

size_t Book::find_first_entry(uint64_t key, int& index_count)
{
    size_t start = 0;
    size_t end = NumBookEntries;

    for (;;)
    {
        size_t mid = (end + start) / 2;

        if (BookEntries[mid].key < key)
            start = mid;
        else
        {
            if (BookEntries[mid].key > key)
                end = mid;
            else
            {
                start = std::max(mid - 4, (size_t)0);
                end = std::min(mid + 4, NumBookEntries);
            }
        }

        if (end - start < 9)
            break;
    }

    for (size_t i = start; i < end; i++)
    {
        if (key == BookEntries[i].key)
        {
            while ((i > 0) && (key == BookEntries[i - 1].key))
                i--;
            index_count = 1;
            end = i;
            while ((++end < NumBookEntries) && (key == BookEntries[end].key))
                index_count++;
            return i;
        }
    }
    return 0;
}

void Book::byteswap_bookentry(BookEntry *be)
{
    be->key = number<uint64_t, BigEndian>(&be->key);
    be->move = number<uint16_t, BigEndian>(&be->move);
    be->weight = number<uint16_t, BigEndian>(&be->weight);
    be->depth = number<uint16_t, BigEndian>(&be->depth);
    be->score = number<uint16_t, BigEndian>(&be->score);
}
