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

#include <cstring>   // For std::memset
#include <iostream>
#include <thread>

#include "bitboard.h"
#include "misc.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

namespace {

constexpr inline uint8_t genFromGenBound8(uint8_t tteGenBound8)
{
  return tteGenBound8 >> 3;
}

constexpr inline uint8_t makeGenBound8(uint8_t gen5, bool pv, Bound bound)
{
  return (gen5 << 3) | uint8_t(pv) << 2 | uint8_t(bound);
}

inline void refreshGen5(uint8_t &tteGenBound8, uint8_t newGen5)
{
  tteGenBound8 = (newGen5 << 3) | (tteGenBound8 & 7U);
}

inline int32_t ageDepthByGen(uint8_t depth8, uint8_t curGen5, uint8_t prevGen5)
{
  constexpr uint8_t genDepthPenalty = 8; // every gen means depth reduction by 8
  const uint8_t aging = ((curGen5 - prevGen5) & 0x1FU) * genDepthPenalty;

  return int32_t(depth8) - int32_t(aging);
}

}

TranspositionTable TT; // Our global transposition table


void TTEntry::load(TTEntryPacked *e, size_t clusterIndex, uint8_t slotIndex)
{
  TTEntryPacked packedData = *e;

  m_move = (Move )packedData.move16;
  m_value = (Value)packedData.value16;
  m_eval = (Value)packedData.eval16;
  m_depth = (Depth)packedData.depth8 + DEPTH_OFFSET;
  m_pv = (bool)(packedData.genBound8 & 0x4);
  m_bound = (Bound)(packedData.genBound8 & 0x3);

  m_clusterIndex = clusterIndex;
  m_slotIndex = slotIndex;
}

/// TTEntry::save populates the TTEntry with a new node's data, possibly
/// overwriting an old position. Update is not atomic and can be racy.
void TTEntry::save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev) {

  bool doStore = false;

  // reload the TT entry, it may have been changed by recursive search since
  // we loaded it
  TTEntryPacked packedData = TT.table[m_clusterIndex].entry[m_slotIndex];

  // Preserve any existing move for the same position
  if (m || (uint16_t)k != packedData.key16)
  {
      packedData.move16 = (uint16_t)m;
      doStore = true;
  }

  // Overwrite less valuable entries
  if (((uint16_t)k != packedData.key16
       || d - DEPTH_OFFSET > packedData.depth8 - 4
       || b == BOUND_EXACT))
  {
      assert(d >= DEPTH_OFFSET);

      packedData.key16     = (uint16_t)k;
      packedData.value16   = (int16_t)v;
      packedData.eval16    = (int16_t)ev;
      packedData.genBound8 = makeGenBound8(TT.generation5, pv, b);
      packedData.depth8    = (uint8_t)(d - DEPTH_OFFSET);
      doStore = true;
  }

  // store only if we changed something to save memory BW
  if (doStore)
      TT.table[m_clusterIndex].entry[m_slotIndex] = packedData;
}


/// TranspositionTable::resize() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of ClusterSize number of TTEntry.

void TranspositionTable::resize(size_t mbSize) {

  Threads.main()->wait_for_search_finished();

  aligned_ttmem_free(mem);

  clusterCount = mbSize * 1024 * 1024 / sizeof(Cluster);
  table = static_cast<Cluster*>(aligned_ttmem_alloc(clusterCount * sizeof(Cluster), mem));
  if (!mem)
  {
      std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
      exit(EXIT_FAILURE);
  }

  clear();
}


/// TranspositionTable::clear() initializes the entire transposition table to zero,
//  in a multi-threaded way.

void TranspositionTable::clear() {

  std::vector<std::thread> threads;

  for (size_t idx = 0; idx < Options["Threads"]; ++idx)
  {
      threads.emplace_back([this, idx]() {

          // Thread binding gives faster search on systems with a first-touch policy
          if (Options["Threads"] > 8)
              WinProcGroup::bindThisThread(idx);

          // Each thread will zero its part of the hash table
          const size_t stride = size_t(clusterCount / Options["Threads"]),
                       start  = size_t(stride * idx),
                       len    = idx != Options["Threads"] - 1 ?
                                stride : clusterCount - start;

          std::memset(&table[start], 0, len * sizeof(Cluster));
      });
  }

  for (std::thread& th : threads)
      th.join();
}

/// TranspositionTable::probe() looks up the current position in the transposition
/// table. It returns true and a pointer to the TTEntry if the position is found.
/// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
/// to be replaced later. The replace value of an entry is calculated as its depth
/// minus 8 times its relative age. TTEntry t1 is considered more valuable than
/// TTEntry t2 if its replace value is greater than that of t2.
bool TranspositionTable::probe(const Key key, TTEntry &entry) const {

  const size_t clusterIndex = mul_hi64(key, clusterCount);
  TTEntryPacked* const tte = &TT.table[clusterIndex].entry[0];
  const uint16_t key16 = (uint16_t)key;  // Use the low 16 bits as key inside the cluster

  for (int i = 0; i < ClusterSize; ++i)
      if (!tte[i].key16 || tte[i].key16 == key16)
      {
          refreshGen5(tte[i].genBound8, generation5); // Refresh gen

          entry.load(&tte[i], clusterIndex, i);
          return (bool)tte[i].key16;
      }

  // Find an entry to be replaced according to the replacement strategy
  TTEntryPacked* replace = tte;
  uint8_t slotIndex = 0;

  for (int i = 1; i < ClusterSize; ++i)
      if (ageDepthByGen(replace->depth8, generation5, genFromGenBound8(replace->genBound8)) >
          ageDepthByGen(tte[i].depth8, generation5, genFromGenBound8(tte[i].genBound8)))
      {
          replace = &tte[i];
          slotIndex = i;
      }

  entry.load(replace, clusterIndex, slotIndex);
  return false;
}


/// TranspositionTable::hashfull() returns an approximation of the hashtable
/// occupation during a search. The hash is x permill full, as per UCI protocol.

int TranspositionTable::hashfull() const {

  int cnt = 0;
  for (int i = 0; i < 1000; ++i)
      for (int j = 0; j < ClusterSize; ++j)
          cnt += genFromGenBound8(table[i].entry[j].genBound8) == generation5;

  return cnt / ClusterSize;
}
