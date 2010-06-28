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


#if !defined(TT_H_INCLUDED)
#define TT_H_INCLUDED

////
//// Includes
////

#include "depth.h"
#include "position.h"
#include "value.h"


////
//// Types
////

/// The TTEntry class is the class of transposition table entries
///
/// A TTEntry needs 96 bits to be stored
///
/// bit  0-31: key
/// bit 32-63: data
/// bit 64-79: value
/// bit 80-95: depth
///
/// the 32 bits of the data field are so defined
///
/// bit  0-16: move
/// bit 17-19: not used
/// bit 20-22: value type
/// bit 23-31: generation

class TTEntry {

public:
  void save(uint32_t k, Value v, ValueType t, Depth d, Move m, int g, Value statV, Value kd) {

      key32 = k;
      data = (m & 0x1FFFF) | (t << 20) | (g << 23);
      value16     = int16_t(v);
      depth16     = int16_t(d);
      staticValue = int16_t(statV);
      kingDanger  = int16_t(kd);
  }

  uint32_t key() const { return key32; }
  Depth depth() const { return Depth(depth16); }
  Move move() const { return Move(data & 0x1FFFF); }
  Value value() const { return Value(value16); }
  ValueType type() const { return ValueType((data >> 20) & 7); }
  int generation() const { return data >> 23; }
  Value static_value() const { return Value(staticValue); }
  Value king_danger() const { return Value(kingDanger); }

private:
  uint32_t key32;
  uint32_t data;
  int16_t value16;
  int16_t depth16;
  int16_t staticValue;
  int16_t kingDanger;
};


/// This is the number of TTEntry slots for each position
const int ClusterSize = 4;

/// Each group of ClusterSize number of TTEntry form a TTCluster
/// that is indexed by a single position key. TTCluster size must
///  be not bigger then a cache line size, in case it is less then
/// it should be padded to guarantee always aligned accesses.

struct TTCluster {
  TTEntry data[ClusterSize];
};


/// The transposition table class. This is basically just a huge array
/// containing TTEntry objects, and a few methods for writing new entries
/// and reading new ones.

class TranspositionTable {

public:
  TranspositionTable();
  ~TranspositionTable();
  void set_size(size_t mbSize);
  void clear();
  void store(const Key posKey, Value v, ValueType type, Depth d, Move m, Value statV, Value kingD);
  TTEntry* retrieve(const Key posKey) const;
  void new_search();
  void insert_pv(const Position& pos, Move pv[]);
  void extract_pv(const Position& pos, Move bestMove, Move pv[], const int PLY_MAX);
  int full() const;
  TTEntry* first_entry(const Key posKey) const;

private:
  // Be sure 'overwrites' is at least one cache line away
  // from read only variables.
  unsigned char pad_before[64 - sizeof(unsigned)];
  unsigned overwrites; // heavy SMP read/write access here
  unsigned char pad_after[64];

  size_t size;
  TTCluster* entries;
  uint8_t generation;
};

extern TranspositionTable TT;


/// TranspositionTable::first_entry returns a pointer to the first
/// entry of a cluster given a position. The low 32 bits of the key
/// are used to get the index in the table.

inline TTEntry* TranspositionTable::first_entry(const Key posKey) const {

  return entries[uint32_t(posKey) & (size - 1)].data;
}

#endif // !defined(TT_H_INCLUDED)
