/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#if defined(_WIN64)
#include <intrin.h>
#endif
#include "misc.h"
#include "types.h"

/// TTEntry struct is the 10 bytes transposition table entry, defined as below:
///
/// key        16 bit
/// move       16 bit
/// value      16 bit
/// eval value 16 bit
/// generation  5 bit
/// pv node     1 bit
/// bound type  2 bit
/// depth       8 bit

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

  uint16_t key16;
  uint16_t move16;
  int16_t  value16;
  int16_t  eval16;
  uint8_t  genBound8;
  uint8_t  depth8;
};


/// A TranspositionTable is an array of Cluster, of size clusterCount. Each
/// cluster consists of ClusterSize number of TTEntry. Each non-empty TTEntry
/// contains information on exactly one position. The size of a Cluster should
/// divide the size of a cache line for best performance,
/// as the cacheline is prefetched when possible.

class TranspositionTable {

  static constexpr int ClusterSize = 3;

  struct Cluster {
    TTEntry entry[ClusterSize];
    char padding[2]; // Pad to 32 bytes
  };

  static_assert(sizeof(Cluster) == 32, "Unexpected Cluster size");

public:
 ~TranspositionTable() { aligned_ttmem_free(mem); }
  void new_search() { generation8 += 8; } // Lower 3 bits are used by PV flag and Bound
  TTEntry* probe(const Key key, bool& found) const;
  int hashfull() const;
  void resize(size_t mbSize);
  void clear();

  // The 32 lowest order bits of the key are used to get the index of the cluster
  TTEntry* first_entry(const Key key) const {
#if defined(_WIN64)
    uint64_t highProduct;
    _umul128(key, clusterCount, &highProduct);
    return &table[highProduct].entry[0];
#elif defined(__GNUC__) && defined(IS_64BIT)
    __extension__ typedef unsigned __int128 uint128;
    return &table[((uint128)key * (uint128)clusterCount) >> 64].entry[0];
#else
    uint64_t kL = (uint32_t)key, kH = key >> 32;
    uint64_t m00 = kL * ccLow;
    uint64_t m01 = kH * ccLow;
    uint64_t m10 = kL * ccHigh;
    uint64_t m11 = (m00 >> 32) + (uint32_t)m01 + (uint32_t)m10;
    uint64_t idx = kH * ccHigh + (m01 >> 32) + (m10 >> 32) + (m11 >> 32);
    return &table[idx].entry[0];
#endif
  }

private:
  friend struct TTEntry;

  size_t clusterCount, ccLow, ccHigh;
  Cluster* table;
  void* mem;
  uint8_t generation8; // Size must be not bigger than TTEntry::genBound8
};

extern TranspositionTable TT;

#endif // #ifndef TT_H_INCLUDED
