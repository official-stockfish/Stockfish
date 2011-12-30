/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

#if !defined(PAWNS_H_INCLUDED)
#define PAWNS_H_INCLUDED

#include "position.h"
#include "tt.h"
#include "types.h"

const int PawnTableSize = 16384;

/// PawnInfo is a class which contains various information about a pawn
/// structure. Currently, it only includes a middle game and an end game
/// pawn structure evaluation, and a bitboard of passed pawns. We may want
/// to add further information in the future. A lookup to the pawn hash
/// table (performed by calling the pawn_info method in a PawnInfoTable
/// object) returns a pointer to a PawnInfo object.

class PawnInfo {

  friend class PawnInfoTable;

public:
  Score pawns_value() const;
  Bitboard pawn_attacks(Color c) const;
  Bitboard passed_pawns(Color c) const;
  int file_is_half_open(Color c, File f) const;
  int has_open_file_to_left(Color c, File f) const;
  int has_open_file_to_right(Color c, File f) const;

  template<Color Us>
  Score king_shelter(const Position& pos, Square ksq);

private:
  template<Color Us>
  Score updateShelter(const Position& pos, Square ksq);

  Key key;
  Bitboard passedPawns[2];
  Bitboard pawnAttacks[2];
  Square kingSquares[2];
  Score value;
  int halfOpenFiles[2];
  Score kingShelters[2];
};


/// The PawnInfoTable class represents a pawn hash table. The most important
/// method is pawn_info, which returns a pointer to a PawnInfo object.

class PawnInfoTable : public SimpleHash<PawnInfo, PawnTableSize> {
public:
  PawnInfo* pawn_info(const Position& pos) const;

private:
  template<Color Us>
  static Score evaluate_pawns(const Position& pos, Bitboard ourPawns, Bitboard theirPawns, PawnInfo* pi);
};


inline Score PawnInfo::pawns_value() const {
  return value;
}

inline Bitboard PawnInfo::pawn_attacks(Color c) const {
  return pawnAttacks[c];
}

inline Bitboard PawnInfo::passed_pawns(Color c) const {
  return passedPawns[c];
}

inline int PawnInfo::file_is_half_open(Color c, File f) const {
  return halfOpenFiles[c] & (1 << int(f));
}

inline int PawnInfo::has_open_file_to_left(Color c, File f) const {
  return halfOpenFiles[c] & ((1 << int(f)) - 1);
}

inline int PawnInfo::has_open_file_to_right(Color c, File f) const {
  return halfOpenFiles[c] & ~((1 << int(f+1)) - 1);
}

template<Color Us>
inline Score PawnInfo::king_shelter(const Position& pos, Square ksq) {
  return kingSquares[Us] == ksq ? kingShelters[Us] : updateShelter<Us>(pos, ksq);
}

#endif // !defined(PAWNS_H_INCLUDED)
