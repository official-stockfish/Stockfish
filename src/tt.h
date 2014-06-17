/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#ifndef TT_H_INCLUDED
#define TT_H_INCLUDED

#include "misc.h"
#include "types.h"

struct TTEntry {

  Move  move()  const      { return (Move )move16; }
  Bound bound() const      { return (Bound)(genBound & 0x3); }
  Value value() const      { return (Value)value16; }
  Depth depth() const      { return (Depth)(depth8 + DEPTH_NONE); }
  Value eval_value() const { return (Value)evalValue; }

private:
  friend class TranspositionTable;

  uint8_t gen() const      { return genBound & 0xfc; }

  uint16_t key;
  uint16_t move16;
  int16_t value16;
  int16_t evalValue;
  uint8_t depth8;
  uint8_t genBound;
};

/// A TranspositionTable consists of a power of 2 number of clusters and each
/// cluster consists of ClusterSize number of TTEntry. Each non-empty entry
/// contains information of exactly one position. The size of a cluster should
/// not be bigger than a cache line size. In case it is less, it should be padded
/// to guarantee always aligned accesses.

class TranspositionTable {

  static const unsigned ClusterSize = 3;

public:
 ~TranspositionTable() { free(mem); }
  void new_search() { generation += 4; }

  const TTEntry* probe(const Key key) const;
  TTEntry* first_entry(const Key key) const;
  void resize(uint64_t mbSize);
  void clear();
  void store(const Key key, Value v, Bound type, Depth d, Move m, Value statV);

private:
  uint64_t hashMask;
  TTEntry* table;
  void* mem;
  uint32_t clusters;
  uint8_t generation;

  struct Cluster {
      TTEntry entry[ClusterSize];
      char pad[2];
  };
};

extern TranspositionTable TT;


/// TranspositionTable::first_entry() returns a pointer to the first entry of
/// a cluster given a position. The lowest order bits of the key are used to
/// get the index of the cluster.

inline TTEntry* TranspositionTable::first_entry(const Key key) const {

  return (TTEntry*)((char*)table + (key & hashMask));
}

#endif // #ifndef TT_H_INCLUDED
