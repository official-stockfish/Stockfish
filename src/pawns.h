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


#if !defined(PAWNS_H_INCLUDED)
#define PAWNS_H_INCLUDED

////
//// Includes
////

#include "bitboard.h"
#include "value.h"

////
//// Types
////

/// PawnInfo is a class which contains various information about a pawn
/// structure. Currently, it only includes a middle game and an end game
/// pawn structure evaluation, and a bitboard of passed pawns. We may want
/// to add further information in the future. A lookup to the pawn hash table
/// (performed by calling the get_pawn_info method in a PawnInfoTable object)
/// returns a pointer to a PawnInfo object.
class Position;

class PawnInfo {

  friend class PawnInfoTable;

public:
  PawnInfo() { clear(); }

  Score pawns_value() const;
  Value kingside_storm_value(Color c) const;
  Value queenside_storm_value(Color c) const;
  Bitboard pawn_attacks(Color c) const;
  Bitboard passed_pawns() const;
  int file_is_half_open(Color c, File f) const;
  int has_open_file_to_left(Color c, File f) const;
  int has_open_file_to_right(Color c, File f) const;
  int get_king_shelter(const Position& pos, Color c, Square ksq);

private:
  void clear();
  int updateShelter(const Position& pos, Color c, Square ksq);

  Key key;
  Bitboard passedPawns;
  Bitboard pawnAttacks[2];
  Square kingSquares[2];
  Score value;
  int16_t ksStormValue[2], qsStormValue[2];
  uint8_t halfOpenFiles[2];
  uint8_t kingShelters[2];
};

/// The PawnInfoTable class represents a pawn hash table.  It is basically
/// just an array of PawnInfo objects and a few methods for accessing these
/// objects.  The most important method is get_pawn_info, which looks up a
/// position in the table and returns a pointer to a PawnInfo object.

class PawnInfoTable {

  enum SideType { KingSide, QueenSide };

public:
  PawnInfoTable(unsigned numOfEntries);
  ~PawnInfoTable();
  PawnInfo* get_pawn_info(const Position& pos) const;

private:
  template<Color Us>
  Score evaluate_pawns(const Position& pos, Bitboard ourPawns, Bitboard theirPawns, PawnInfo* pi) const;

  template<Color Us, SideType Side>
  int evaluate_pawn_storm(Square s, Rank r, File f, Bitboard theirPawns) const;

  unsigned size;
  PawnInfo* entries;
};


////
//// Inline functions
////

inline Score PawnInfo::pawns_value() const {
  return value;
}

inline Bitboard PawnInfo::passed_pawns() const {
  return passedPawns;
}

inline Bitboard PawnInfo::pawn_attacks(Color c) const {
  return pawnAttacks[c];
}

inline Value PawnInfo::kingside_storm_value(Color c) const {
  return Value(ksStormValue[c]);
}

inline Value PawnInfo::queenside_storm_value(Color c) const {
  return Value(qsStormValue[c]);
}

inline int PawnInfo::file_is_half_open(Color c, File f) const {
  return (halfOpenFiles[c] & (1 << int(f)));
}

inline int PawnInfo::has_open_file_to_left(Color c, File f) const {
  return halfOpenFiles[c] & ((1 << int(f)) - 1);
}

inline int PawnInfo::has_open_file_to_right(Color c, File f) const {
  return halfOpenFiles[c] & ~((1 << int(f+1)) - 1);
}

inline int PawnInfo::get_king_shelter(const Position& pos, Color c, Square ksq) {
  return (kingSquares[c] == ksq ? kingShelters[c] : updateShelter(pos, c, ksq));
}

#endif // !defined(PAWNS_H_INCLUDED)
