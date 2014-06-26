/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <cstring>
#include <iostream>

#include "bitboard.h"
#include "tt.h"

TranspositionTable TT; // Our global transposition table


/// TranspositionTable::resize() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of ClusterSize number of TTEntry.

void TranspositionTable::resize(uint64_t mbSize) {

  uint32_t newClusters = 1 << msb((mbSize << 20) / sizeof(Cluster));

  if (newClusters == clusters)
      return;
  clusters = newClusters;

  hashMask = (clusters - 1) * sizeof(Cluster);
  free(mem);
  mem = calloc(clusters * sizeof(Cluster) + CACHE_LINE_SIZE - 1, 1);

  if (!mem)
  {
      std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
      exit(EXIT_FAILURE);
  }

  table = (TTEntry*)((uintptr_t(mem) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1));
}


/// TranspositionTable::clear() overwrites the entire transposition table
/// with zeroes. It is called whenever the table is resized, or when the
/// user asks the program to clear the table (from the UCI interface).

void TranspositionTable::clear() {

  std::memset(table, 0, clusters * sizeof(Cluster));
}


/// TranspositionTable::probe() looks up the current position in the
/// transposition table. Returns a pointer to the TTEntry or NULL if
/// position is not found.

const TTEntry* TranspositionTable::probe(const Key key) const {

  TTEntry* tte = first_entry(key);
  const uint16_t key16 = key >> 48;

  if (tte->key != key16 && (++tte)->key != key16 && (++tte)->key != key16)
      return NULL;
  tte->genBound = generation | tte->genBound & 0x3; // Refresh
  return tte;
}


/// TranspositionTable::store() writes a new entry containing position key and
/// valuable information of current position. The lowest order bits of position
/// key are used to decide in which cluster the position will be placed.
/// When a new entry is written and there are no empty entries available in the
/// cluster, it replaces the least valuable of the entries. A TTEntry t1 is considered
/// to be more valuable than a TTEntry t2 if t1 is from the current search and t2
/// is from a previous search, or if the depth of t1 is bigger than the depth of t2.

void TranspositionTable::store(const Key key, Value v, Bound b, Depth d, Move m, Value statV) {

  TTEntry* tte = first_entry(key);
  const TTEntry* last = tte + ClusterSize - 1;
  TTEntry* replace = tte;
  uint16_t key16 = key >> 48; // Use the high 16 bits as key inside the cluster

  for (;;)
  {

      // Empty entry?
      if (!tte->key)
      {
          tte->key = key16;
          tte->move16 = (uint16_t)m;
          break;
      }

      // Overwrite old?
      if (tte->key == key16)
      {
          if (m)
              // Only store move if there is one
              tte->move16 = (uint16_t)m;
          break;
      }

      if (tte == last)
      {
          tte = replace;
          tte->key = key16;
          tte->move16 = (uint16_t)m;
          break;
      }

      ++tte;
      // Is the next entry a better candidate for replacement?
      if ((tte->gen() == generation || tte->bound() == BOUND_EXACT)
          - (replace->gen() == generation)
          - (tte->depth8 < replace->depth8) < 0)
          replace = tte;
  }

  tte->value16 = (int16_t)v;
  tte->evalValue = (int16_t)statV;
  tte->depth8 = (uint8_t)(d - DEPTH_NONE);
  tte->genBound = generation | (uint8_t)b;
}
