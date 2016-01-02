/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef PAWNS_H_INCLUDED
#define PAWNS_H_INCLUDED

#include "misc.h"
#include "position.h"
#include "types.h"

namespace Pawns {

/// Pawns::Entry contains various information about a pawn structure. A lookup
/// to the pawn hash table (performed by calling the probe function) returns a
/// pointer to an Entry object.

struct Entry {

  Score pawns_score() const { return score; }
  Bitboard pawn_attacks(Color c) const { return pawnAttacks[c]; }
  Bitboard passed_pawns(Color c) const { return passedPawns[c]; }
  Bitboard pawn_attacks_span(Color c) const { return pawnAttacksSpan[c]; }
  int pawn_span(Color c) const { return pawnSpan[c]; }
  int pawn_asymmetry() const { return asymmetry; }

  int semiopen_file(Color c, File f) const {
    return semiopenFiles[c] & (1 << f);
  }

  int semiopen_side(Color c, File f, bool leftSide) const {
    return semiopenFiles[c] & (leftSide ? (1 << f) - 1 : ~((1 << (f + 1)) - 1));
  }

  int pawns_on_same_color_squares(Color c, Square s) const {
    return pawnsOnSquares[c][!!(DarkSquares & s)];
  }

  template<Color Us>
  Score king_safety(const Position& pos, Square ksq)  {
    return  kingSquares[Us] == ksq && castlingRights[Us] == pos.can_castle(Us)
          ? kingSafety[Us] : (kingSafety[Us] = do_king_safety<Us>(pos, ksq));
  }

  template<Color Us>
  Score do_king_safety(const Position& pos, Square ksq);

  template<Color Us>
  Value shelter_storm(const Position& pos, Square ksq);

  Key key;
  Score score;
  Bitboard passedPawns[COLOR_NB];
  Bitboard pawnAttacks[COLOR_NB];
  Bitboard pawnAttacksSpan[COLOR_NB];
  Square kingSquares[COLOR_NB];
  Score kingSafety[COLOR_NB];
  int castlingRights[COLOR_NB];
  int semiopenFiles[COLOR_NB];
  int pawnSpan[COLOR_NB];
  int pawnsOnSquares[COLOR_NB][COLOR_NB]; // [color][light/dark squares]
  int asymmetry;
};

typedef HashTable<Entry, 16384> Table;

void init();
Entry* probe(const Position& pos);

} // namespace Pawns

#endif // #ifndef PAWNS_H_INCLUDED
