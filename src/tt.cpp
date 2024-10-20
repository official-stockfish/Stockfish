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

struct TTEntry {

    TTData read() const {
        return TTData{Move(move16),
                      Value(value16),
                      Value(eval16),
                      Depth(depth8 + DEPTH_ENTRY_OFFSET),
                      Bound(genBound8 & 0x3),
                      bool(genBound8 & 0x4),
                      pv_signature};  // Include pv_signature in the read
    }

    bool is_occupied() const;
    void save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t generation8, uint16_t pv_sig);
    uint8_t relative_age(const uint8_t generation8) const;

   private:
    friend class TranspositionTable;

    uint16_t key16;
    uint8_t  depth8;
    uint8_t  genBound8;
    Move     move16;
    int16_t  value16;
    int16_t  eval16;
    uint16_t pv_signature;  // New field for PV tracking
};

static constexpr unsigned GENERATION_BITS = 3;
static constexpr int GENERATION_DELTA = (1 << GENERATION_BITS);
static constexpr int GENERATION_CYCLE = 255 + GENERATION_DELTA;
static constexpr int GENERATION_MASK = (0xFF << GENERATION_BITS) & 0xFF;

bool TTEntry::is_occupied() const { return bool(depth8); }

void TTEntry::save(
  Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t generation8, uint16_t pv_sig) {

    if (m || uint16_t(k) != key16)
        move16 = m;

    if (b == BOUND_EXACT || uint16_t(k) != key16 || d - DEPTH_ENTRY_OFFSET + 2 * pv > depth8 - 4
        || relative_age(generation8))
    {
        assert(d > DEPTH_ENTRY_OFFSET);
        assert(d < 256 + DEPTH_ENTRY_OFFSET);

        key16     = uint16_t(k);
        depth8    = uint8_t(d - DEPTH_ENTRY_OFFSET);
        genBound8 = uint8_t(generation8 | uint8_t(pv) << 2 | b);
        value16   = int16_t(v);
        eval16    = int16_t(ev);
        pv_signature = pv_sig;  // Save the PV signature
    }
}

uint8_t TTEntry::relative_age(const uint8_t generation8) const {
    return (GENERATION_CYCLE + generation8 - genBound8) & GENERATION_MASK;
}

TTWriter::TTWriter(TTEntry* tte) :
    entry(tte) {}

void TTWriter::write(
  Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, uint8_t generation8, uint16_t pv_signature) {
    entry->save(k, v, pv, b, d, m, ev, generation8, pv_signature);
}

static constexpr int ClusterSize = 3;

struct Cluster {
    TTEntry entry[ClusterSize];
    char    padding[2];  // Pad to 32 bytes
};

static_assert(sizeof(Cluster) == 32, "Suboptimal Cluster size");

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

void TranspositionTable::clear(ThreadPool& threads) {
    generation8              = 0;
    const size_t threadCount = threads.num_threads();

    for (size_t i = 0; i < threadCount; ++i)
    {
        threads.run_on_thread(i, [this, i, threadCount]() {
            const size_t stride = clusterCount / threadCount;
            const size_t start  = stride * i;
            const size_t len    = i + 1 != threadCount ? stride : clusterCount - start;

            std::memset(&table[start], 0, len * sizeof(Cluster));
        });
    }

    for (size_t i = 0; i < threadCount; ++i)
        threads.wait_on_thread(i);
}

int TranspositionTable::hashfull() const {

    int cnt = 0;
    for (int i = 0; i < 1000; ++i)
        for (int j = 0; j < ClusterSize; ++j)
            cnt += table[i].entry[j].is_occupied()
                && (table[i].entry[j].genBound8 & GENERATION_MASK) == generation8;

    return cnt / ClusterSize;
}

void TranspositionTable::new_search() {
    generation8 += GENERATION_DELTA;
}

uint8_t TranspositionTable::generation() const { return generation8; }

std::tuple<bool, TTData, TTWriter> TranspositionTable::probe(const Key key) const {

    TTEntry* const tte   = first_entry(key);
    const uint16_t key16 = uint16_t(key);

    for (int i = 0; i < ClusterSize; ++i)
        if (tte[i].key16 == key16)
            return {tte[i].is_occupied(), tte[i].read(), TTWriter(&tte[i])};

    TTEntry* replace = tte;
    for (int i = 1; i < ClusterSize; ++i)
        if (replace->depth8 - replace->relative_age(generation8) * 2
            > tte[i].depth8 - tte[i].relative_age(generation8) * 2)
            replace = &tte[i];

    return {false, TTData(), TTWriter(replace)};
}

TTEntry* TranspositionTable::first_entry(const Key key) const {
    return &table[mul_hi64(key, clusterCount)].entry[0];
}

}  // namespace Stockfish
