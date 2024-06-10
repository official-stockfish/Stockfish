/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#include <cstddef>
#include <cstdint>
#include <tuple>

#include "memory.h"
#include "types.h"

namespace Stockfish {

class ThreadPool;
struct TTEntry;
struct Cluster;

// There is only one global hash table for the engine and all its threads. For chess in particular, we even allow racy
// updates between threads to and from the TT, as taking the time to synchronize access would cost thinking time and
// thus elo. As a hash table, collisions are possible and may cause chess playing issues (bizarre blunders, faulty mate
// reports, etc). Fixing these also loses elo; however such risk decreases quickly with larger TT size.
//
// `probe` is the primary method: given a board position, we lookup its entry in the table, and return a tuple of:
//   1) whether the entry already has this position
//   2) a copy of the prior data (if any) (may be inconsistent due to read races)
//   3) a writer object to this entry
// The copied data and the writer are separated to maintain clear boundaries between local vs global objects.


// A copy of the data already in the entry (possibly collided). `probe` may be racy, resulting in inconsistent data.
struct TTData {
    Move  move;
    Value value, eval;
    Depth depth;
    Bound bound;
    bool  is_pv;
};


// This is used to make racy writes to the global TT.
struct TTWriter {
   public:
    void write(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t generation8);

   private:
    friend class TranspositionTable;
    TTEntry* entry;
    TTWriter(TTEntry* tte);
};


class TranspositionTable {

   public:
    ~TranspositionTable() { aligned_large_pages_free(table); }

    void resize(size_t mbSize, ThreadPool& threads);  // Set TT size
    void clear(ThreadPool& threads);                  // Re-initialize memory, multithreaded
    int  hashfull()
      const;  // Approximate what fraction of entries (permille) have been written to during this root search

    void
    new_search();  // This must be called at the beginning of each root search to track entry aging
    uint8_t generation() const;  // The current age, used when writing new data to the TT
    std::tuple<bool, TTData, TTWriter>
    probe(const Key key) const;  // The main method, whose retvals separate local vs global objects
    TTEntry* first_entry(const Key key)
      const;  // This is the hash function; its only external use is memory prefetching.

   private:
    friend struct TTEntry;

    size_t   clusterCount;
    Cluster* table = nullptr;

    uint8_t generation8 = 0;  // Size must be not bigger than TTEntry::genBound8
};

}  // namespace Stockfish

#endif  // #ifndef TT_H_INCLUDED
