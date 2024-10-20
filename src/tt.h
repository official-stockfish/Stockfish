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

// A copy of the data already in the entry (possibly collided). `probe` may be racy, resulting in inconsistent data.
struct TTData {
    Move  move;
    Value value, eval;
    Depth depth;
    Bound bound;
    bool  is_pv;
    uint16_t pv_signature;  // New field for PV tracking
};

// This is used to make racy writes to the global TT.
struct TTWriter {
   public:
    void write(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t generation8, uint16_t pv_signature);

   private:
    friend class TranspositionTable;
    TTEntry* entry;
    TTWriter(TTEntry* tte);
};

class TranspositionTable {

   public:
    ~TranspositionTable() { aligned_large_pages_free(table); }

    void resize(size_t mbSize, ThreadPool& threads);
    void clear(ThreadPool& threads);
    int  hashfull() const;

    void new_search();
    uint8_t generation() const;
    std::tuple<bool, TTData, TTWriter> probe(const Key key) const;
    TTEntry* first_entry(const Key key) const;

   private:
    friend struct TTEntry;

    size_t   clusterCount;
    Cluster* table = nullptr;

    uint8_t generation8 = 0;
};

}  // namespace Stockfish

#endif  // #ifndef TT_H_INCLUDED
