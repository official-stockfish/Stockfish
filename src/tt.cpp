/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#include "tt.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "memory.h"
#include "misc.h"
#include "syzygy/tbprobe.h"
#include "thread.h"

namespace Stockfish {


// TTEntry struct is the 10 bytes transposition table entry, defined as:
//
// key        16 bit
// depth       8 bit
// pv node     1 bit
// bound type  2 bit
// generation  5 bit
// move       16 bit
// value      16 bit
// evaluation 16 bit
//
// These fields are in the same order as accessed by TT::probe(), since memory is fastest sequentially.
// Equally, the store order in save() matches this order.
//
// We use `bool(depth8)` as the cheap internal occupancy check, corresponding to `depth == DEPTH_NONE`
// externally, so we offset the internal depth by DEPTH_NONE.
//
// Pv, bound and generation are packed in a single byte.
static constexpr uint8_t GENERATION_BITS = 5;
static constexpr uint8_t GENERATION_MASK = (1 << GENERATION_BITS) - 1;
static constexpr uint8_t BOUND_SHIFT     = GENERATION_BITS;
static constexpr uint8_t BOUND_MASK      = 0b11 << BOUND_SHIFT;
static constexpr uint8_t PV_SHIFT        = BOUND_SHIFT + 2;
static constexpr uint8_t PV_MASK         = 1 << PV_SHIFT;

struct TTEntry {

    // Convert internal bitfields to external types
    TTData read() const {
        return TTData{Move(move16),
                      Value(value16),
                      Value(eval16),
                      Depth(DEPTH_NONE + depth8),
                      Bound((genBound8 & BOUND_MASK) >> BOUND_SHIFT),
                      bool(genBound8 & PV_MASK)};
    }

    bool is_occupied() const { return bool(depth8); };
    void save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t curr_generation);
    uint8_t relative_age(const uint8_t curr_generation) const;

   private:
    friend class TranspositionTable;

    uint16_t key16;
    uint8_t  depth8;
    uint8_t  genBound8;
    Move     move16;
    int16_t  value16;
    int16_t  eval16;
};

// Populates the TTEntry with a new node's data, possibly
// overwriting an old position. The update is non-atomic and can be racy.
void TTEntry::save(
  Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t curr_generation) {

    // Preserve the old ttmove if we don't have a new one
    if (m || uint16_t(k) != key16)
        move16 = m;

    // Overwrite less valuable entries (cheapest checks first)
    if (b == BOUND_EXACT || uint16_t(k) != key16 || d - DEPTH_NONE + 2 * pv > depth8 - 4
        || relative_age(curr_generation))
    {
        assert(d > DEPTH_NONE);
        assert(d - DEPTH_NONE < 256);
        assert(curr_generation <= GENERATION_MASK);  // TT::new_search() plays nice

        key16     = uint16_t(k);
        depth8    = uint8_t(d - DEPTH_NONE);
        genBound8 = uint8_t(curr_generation | b << BOUND_SHIFT | uint8_t(pv) << PV_SHIFT);
        value16   = int16_t(v);
        eval16    = int16_t(ev);
    }
}


uint8_t TTEntry::relative_age(const uint8_t curr_generation) const {
    // Returns this entry's age. We count generations like clocks count hours,
    // i.e. we require 0 - 1 == 31. Unsigned subtraction guarantees the required
    // borrowing regardless of the upper pv/bound bits.
    return (curr_generation - genBound8) & GENERATION_MASK;
}


// TTWriter is but a very thin wrapper around the pointer
TTWriter::TTWriter(TTEntry* tte) :
    entry(tte) {}

void TTWriter::write(
  Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t curr_generation) {
    entry->save(k, v, pv, b, d, m, ev, curr_generation);
}


// A TranspositionTable is an array of Cluster, of size clusterCount. Each cluster consists of ClusterSize number
// of TTEntry. Each non-empty TTEntry contains information on exactly one position. The size of a Cluster should
// divide the size of a cache line for best performance, as the cacheline is prefetched when possible.

static constexpr int ClusterSize = 3;

struct Cluster {
    TTEntry entry[ClusterSize];
    char    padding[2];  // Pad to 32 bytes
};

static_assert(sizeof(Cluster) == 32, "Suboptimal Cluster size");


// Sets the size of the transposition table,
// measured in megabytes. Transposition table consists
// of clusters and each cluster consists of ClusterSize number of TTEntry.
void TranspositionTable::resize(size_t mbSize, ThreadPool& threads) {
    aligned_large_pages_free(table);

    clusterCount = mbSize * 1024 * 1024 / sizeof(Cluster);

    table = static_cast<Cluster*>(aligned_large_pages_alloc(clusterCount * sizeof(Cluster)));

    if (!table)
    {
        std::cerr << "Failed to allocate " << mbSize << "MB for transposition table." << std::endl;
        exit(EXIT_FAILURE);
    }

    clear(threads);
}


// Initializes the entire transposition table to zero,
// in a multi-threaded way.
void TranspositionTable::clear(ThreadPool& threads) {
    generation8              = 0;
    const size_t threadCount = threads.num_threads();

    for (size_t i = 0; i < threadCount; ++i)
    {
        threads.run_on_thread(i, [this, i, threadCount]() {
            // Each thread will zero its part of the hash table
            const size_t stride = clusterCount / threadCount;
            const size_t start  = stride * i;
            const size_t len    = i + 1 != threadCount ? stride : clusterCount - start;

            std::memset(&table[start], 0, len * sizeof(Cluster));
        });
    }

    for (size_t i = 0; i < threadCount; ++i)
        threads.wait_on_thread(i);
}


// Returns an approximation of the hashtable
// occupation during a search. The hash is x permill full, as per UCI protocol.
// Only counts entries which are younger than maxAge.
int TranspositionTable::hashfull(int maxAge) const {
    int cnt = 0;
    for (int i = 0; i < 1000; ++i)
        for (int j = 0; j < ClusterSize; ++j)
            cnt += table[i].entry[j].is_occupied()
                && table[i].entry[j].relative_age(generation8) <= maxAge;

    return cnt / ClusterSize;
}


void TranspositionTable::new_search() {
    ++generation8;
    // Don't overflow into the other bits of TTEntry::genBound8
    generation8 &= GENERATION_MASK;
}


uint8_t TranspositionTable::generation() const { return generation8; }


// Looks up the current position in the transposition table.
// It returns true if the key is found (which may be a collision), and has non-null data.
// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
// to be replaced later. The value of an entry is its depth minus 8 times its relative age.
std::tuple<bool, TTData, TTWriter> TranspositionTable::probe(const Key key) const {

    TTEntry* const tte   = first_entry(key);
    const uint16_t key16 = uint16_t(key);  // Use the low 16 bits as key inside the cluster

    for (int i = 0; i < ClusterSize; ++i)
        if (tte[i].key16 == key16)
            // This gap is the main place for read races.
            // After `read()` completes that copy is final, but may be self-inconsistent.
            return {tte[i].is_occupied(), tte[i].read(), TTWriter(&tte[i])};

    // Find an entry to be replaced according to the replacement strategy
    TTEntry* replace = tte;
    for (int i = 1; i < ClusterSize; ++i)
        if (replace->depth8 - 8 * replace->relative_age(generation8)
            > tte[i].depth8 - 8 * tte[i].relative_age(generation8))
            replace = &tte[i];

    return {false, TTData{Move::none(), VALUE_NONE, VALUE_NONE, DEPTH_NONE, BOUND_NONE, false},
            TTWriter(replace)};
}


TTEntry* TranspositionTable::first_entry(const Key key) const {
    return &table[mul_hi64(key, clusterCount)].entry[0];
}

}  // namespace Stockfish
