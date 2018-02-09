/*
 McBrain, a UCI chess playing engine derived from Stockfish and Glaurung 2.1
 Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
 Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish Authors)
 Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (Stockfish Authors)
 Copyright (C) 2017-2018 Michael Byrne, Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (McBrain Authors)
 
 McBrain is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 McBrain is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
//polybook.h was wriiten by Thomas Zipproth
/*
polybook.h was written by Thomas Zipproth
BrainFish, a UCI chess playing engine derived from Stockfish
Copyright (C) 2016-2018 Thomas Zipproth

BrainFish is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

BrainFish is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef POLYBOOK_H_INCLUDED
#define POLYBOOK_H_INCLUDED

#include "bitboard.h"
#include "position.h"
#include "string.h"

typedef struct {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
} PolyHash;

class PolyBook
{
public:

    PolyBook();
    ~PolyBook();

    void init(const std::string& bookfile);
    void set_best_book_move(bool best_book_move);
    void set_book_depth(int book_depth);

    Move probe(Position& pos);

private:

    Key polyglot_key(const Position& pos);
    Move pg_move_to_sf_move(const Position & pos, unsigned short pg_move);

    int find_first_key(uint64_t key);
    int get_key_data();

    bool check_do_search(const Position & pos);
    bool check_draw(Move m, Position& pos);

    void byteswap_polyhash(PolyHash *ph);
    uint64_t rand64();

    bool is_little_endian();
    uint64_t swap_uint64(uint64_t d);
    uint32_t swap_uint32(uint32_t d);
    uint16_t swap_uint16(uint16_t d);

    int keycount;
    PolyHash *polyhash;

    bool use_best_book_move;
    int max_book_depth;
    int book_depth_count;

    int index_first;
    int index_count;
    int index_best;
    int index_rand;
    int index_weight_count;

    uint64_t sr;

    Bitboard last_position;
    Bitboard akt_position;
    int last_anz_pieces;
    int akt_anz_pieces;
    int search_counter;

    bool enabled, do_search;
};

extern PolyBook polybook;

#endif // #ifndef POLYBOOK_H_INCLUDED
