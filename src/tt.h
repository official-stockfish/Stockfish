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
/// bit 17-18: not used
/// bit 19-22: value type
/// bit 23-31: generation

class TTEntry {

public:
  TTEntry() {}
  TTEntry(uint32_t k, Value v, ValueType t, Depth d, Move m, int generation)
        : key_ (k), data((m & 0x1FFFF) | (t << 19) | (generation << 23)),
          value_(int16_t(v)), depth_(int16_t(d)) {}

  uint32_t key() const { return key_; }
  Depth depth() const { return Depth(depth_); }
  Move move() const { return Move(data & 0x1FFFF); }
  Value value() const { return Value(value_); }
  ValueType type() const { return ValueType((data >> 19) & 0xF); }
  int generation() const { return (data >> 23); }

private:
  uint32_t key_;
  uint32_t data;
  int16_t value_;
  int16_t depth_;
};


/// This is the number of TTEntry slots for each position
const int ClusterSize = 5;

/// Each group of ClusterSize number of TTEntry form a TTCluster
/// that is indexed by a single position key. Cluster is padded
/// to a cache line size so to guarantee always aligned accesses.

struct TTCluster {
  TTEntry data[ClusterSize];
  char cache_line_padding[64 - sizeof(TTEntry[ClusterSize])];
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
  void store(const Key posKey, Value v, ValueType type, Depth d, Move m);
  TTEntry* retrieve(const Key posKey) const;
  void prefetch(const Key posKey) const;
  void new_search();
  void insert_pv(const Position& pos, Move pv[]);
  void extract_pv(const Position& pos, Move pv[], const int PLY_MAX);
  int full() const;

private:
  inline TTEntry* first_entry(const Key posKey) const;

  // Be sure 'writes' is at least one cache line away
  // from read only variables.
  unsigned char pad_before[64 - sizeof(unsigned)];
  unsigned writes; // heavy SMP read/write access here
  unsigned char pad_after[64];

  size_t size;
  TTCluster* entries;
  uint8_t generation;
};

extern TranspositionTable TT;

#endif // !defined(TT_H_INCLUDED)
