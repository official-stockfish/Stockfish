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

#include "misc.h"
#include "types.h"

/// TTEntry struct is the 8 bytes transposition table entry, defined as below:
///
/// move       13 bit
/// reserved    3 bit
/// value      16 bit
/// eval value 16 bit
/// generation  5 bit
/// pv node     1 bit
/// bound type  2 bit
/// depth       8 bit

struct TTEntryPacked {

  using Move13     = BitFieldDesc< 0U, 13U, uint16_t>;
  using Value16    = BitFieldDesc<16U, 16U, Value, true>; // sign-extend
  using Eval16     = BitFieldDesc<32U, 16U, Value, true>; // sign-extend
  using Gen5       = BitFieldDesc<48U,  5U, uint8_t>;
  using Pv         = BitFieldDesc<53U,  1U, bool>;
  using Bound2     = BitFieldDesc<54U,  2U, Bound>;
  using Depth8     = BitFieldDesc<56U,  8U, uint8_t>;

  uint64_t bits;
};

struct TTEntry {

  void save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev);

  Move  move()  const { return m_move; }
  Value value() const { return m_value; }
  Value eval()  const { return m_eval; }
  Depth depth() const { return m_depth; }
  bool is_pv()  const { return m_pv; }
  Bound bound() const { return m_bound; }

private:
  friend class TranspositionTable;

  void load(uint64_t bits, size_t clusterIndex, uint8_t slotIndex);
  void reset(size_t clusterIndex, uint8_t slotIndex);

  Move m_move;
  Value m_value;
  Value m_eval;
  bool m_pv;
  Bound m_bound;
  Depth m_depth;

  size_t m_clusterIndex;
  uint8_t m_slotIndex;
};


/// A TranspositionTable is an array of Cluster, of size clusterCount. Each
/// cluster consists of ClusterSize number of TTEntry. Each non-empty TTEntry
/// contains information on exactly one position. The size of a Cluster should
/// divide the size of a cache line for best performance,
/// as the cacheline is prefetched when possible.

class TranspositionTable {

  static constexpr int ClusterSize = 3;

  struct Cluster {
    uint64_t keys;
    TTEntryPacked entry[ClusterSize];
  };

  static_assert(sizeof(Cluster) == 32, "Unexpected Cluster size");

public:
  using HashEntryKeyField = BitFieldDesc<0, 16, uint32_t>; // TODO: bump to 21 bits
  static_assert(ClusterSize * HashEntryKeyField::numBits < 64, "entry key bits overflow check");

 ~TranspositionTable() { aligned_ttmem_free(mem); }
  void new_search() { generation5 = (generation5 + 1) & 0x1FU; } // 5 bits, encoded with bound (2 bits) and pv (1 bit)
  bool probe(const Key key, TTEntry &entry) const;
  int hashfull() const;
  void resize(size_t mbSize);
  void clear();

  TTEntryPacked* first_entry(const Key key) const {
    return &table[mul_hi64(key, clusterCount)].entry[0];
  }

private:
  friend struct TTEntry;

  size_t clusterCount;
  Cluster* table;
  void* mem;
  uint8_t generation5; // Must be within 5 bits
};

extern TranspositionTable TT;

#endif // #ifndef TT_H_INCLUDED
