/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

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

/// TTEntry struct is the 10 bytes transposition table entry, defined as below:
///
/// depth       8 bit
/// generation  5 bit
/// pv node     1 bit
/// bound type  2 bit
/// move       16 bit
/// value      16 bit
/// eval value 16 bit
///
/// Key is stored separately

struct TTEntry {

  Move  move()  const { return (Move )move16; }
  Value value() const { return (Value)value16; }
  Value eval()  const { return (Value)eval16; }
  Depth depth() const { return (Depth)depth8 + DEPTH_OFFSET; }
  bool is_pv()  const { return (bool)(genBound8 & 0x4); }
  Bound bound() const { return (Bound)(genBound8 & 0x3); }
  void save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev);

private:
  friend class TranspositionTable;

  uint8_t  depth8;
  uint8_t  genBound8;
  uint16_t move16;
  int16_t  value16;
  int16_t  eval16;
};


/// A TranspositionTable is an array of Cluster, of size clusterCount. Each
/// cluster consists of ClusterSize number of TTEntry. Each non-empty TTEntry
/// contains information on exactly one position. The size of a Cluster should
/// divide the size of a cache line for best performance, as the cacheline is
/// prefetched when possible.

class TranspositionTable {

  static constexpr int ClusterSize = 4;

  struct Cluster {
    TTEntry entry[ClusterSize];
  };

  static_assert(sizeof(Cluster) == 32, "Unexpected Cluster size");

public:
 ~TranspositionTable() { aligned_large_pages_free(table); }
  void new_search() { generation8 += 8; } // Lower 3 bits are used by PV flag and Bound
  TTEntry* probe(const Key key, bool& found) const;
  int hashfull() const;
  void resize(size_t mbSize);
  void clear();

  inline void prefetch(const Key key) const {
    const size_t clusterIndex = getClusterIndex(key);
    ::prefetch(&table[clusterIndex]);
    ::prefetch(&hashes[ClusterSize * clusterIndex]);
  }

private:
  friend struct TTEntry;
  using EntryKey = uint16_t;

  size_t clusterCount;
  Cluster* table;
  EntryKey* hashes;
  uint8_t generation8; // Size must be not bigger than TTEntry::genBound8

  inline size_t getClusterIndex(const Key key) const {
    return mul_hi64(key, clusterCount);
  }
};

extern TranspositionTable TT;

#endif // #ifndef TT_H_INCLUDED
