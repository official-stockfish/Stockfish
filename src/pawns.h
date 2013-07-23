/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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

/// Pawns::Entry contains various information about a pawn structure. Currently,
/// it only includes a middle game and end game pawn structure evaluation, and a
/// bitboard of passed pawns. We may want to add further information in the future.
/// A lookup to the pawn hash table (performed by calling the probe function)
/// returns a pointer to an Entry object.

struct Entry {

  Score pawns_value() const { return value; }
  Bitboard pawn_attacks(Color c) const { return pawnAttacks[c]; }
  Bitboard passed_pawns(Color c) const { return passedPawns[c]; }
  int pawns_on_same_color_squares(Color c, Square s) const { return pawnsOnSquares[c][!!(BlackSquares & s)]; }
  int semiopen(Color c, File f) const { return semiopenFiles[c] & (1 << int(f)); }
  int semiopen_on_side(Color c, File f, bool left) const {

    return semiopenFiles[c] & (left ? ((1 << int(f)) - 1) : ~((1 << int(f+1)) - 1));
  }

  template<Color Us>
  Score king_safety(const Position& pos, Square ksq)  {

    return kingSquares[Us] == ksq && castleRights[Us] == pos.can_castle(Us)
         ? kingSafety[Us] : update_safety<Us>(pos, ksq);
  }

  template<Color Us>
  Score update_safety(const Position& pos, Square ksq);

  template<Color Us>
  Value shelter_storm(const Position& pos, Square ksq);

  Key key;
  Bitboard passedPawns[COLOR_NB];
  Bitboard pawnAttacks[COLOR_NB];
  Square kingSquares[COLOR_NB];
  int minKPdistance[COLOR_NB];
  int castleRights[COLOR_NB];
  Score value;
  int semiopenFiles[COLOR_NB];
  Score kingSafety[COLOR_NB];
  int pawnsOnSquares[COLOR_NB][COLOR_NB];
};

typedef HashTable<Entry, 16384> Table;

Entry* probe(const Position& pos, Table& entries);

}

#endif // #ifndef PAWNS_H_INCLUDED
