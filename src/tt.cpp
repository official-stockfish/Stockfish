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

#include <cstring>   // For std::memset
#include <iostream>

#include "bitboard.h"
#include "tt.h"

TranspositionTable TT; // Our global transposition table


/// TranspositionTable::resize() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of TTClusterSize number of TTEntry.

void TranspositionTable::resize(size_t mbSize) {

  size_t newClusterCount = size_t(1) << msb((mbSize * 1024 * 1024) / sizeof(TTCluster));

  if (newClusterCount == clusterCount)
      return;

  clusterCount = newClusterCount;

  free(mem);
  mem = calloc(clusterCount * sizeof(TTCluster) + CACHE_LINE_SIZE - 1, 1);

  if (!mem)
  {
      std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
      exit(EXIT_FAILURE);
  }

  table = (TTCluster*)((uintptr_t(mem) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1));
}


/// TranspositionTable::clear() overwrites the entire transposition table
/// with zeros. It is called whenever the table is resized, or when the
/// user asks the program to clear the table (from the UCI interface).

void TranspositionTable::clear() {

  std::memset(table, 0, clusterCount * sizeof(TTCluster));
}


/// TranspositionTable::probe() looks up the current position in the transposition
/// table. It returns true and a pointer to the TTEntry if the position is found.
/// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
/// to be replaced later. A TTEntry t1 is considered to be more valuable than a
/// TTEntry t2 if t1 is from the current search and t2 is from a previous search,
/// or if the depth of t1 is bigger than the depth of t2.

TTEntry* TranspositionTable::probe(const Key key, bool& found) const {

  TTEntry* const tte = first_entry(key);
  const uint16_t key16 = key >> 48;  // Use the high 16 bits as key inside the cluster

  for (unsigned i = 0; i < TTClusterSize; ++i)
      if (!tte[i].key16 || tte[i].key16 == key16)
      {
          if (tte[i].key16)
              tte[i].genBound8 = uint8_t(generation8 | tte[i].bound()); // Refresh

          return found = (bool)tte[i].key16, &tte[i];
      }

  // Find an entry to be replaced according to the replacement strategy
  TTEntry* replace = tte;
  for (unsigned i = 1; i < TTClusterSize; ++i)
      if (  ((  tte[i].genBound8 & 0xFC) == generation8 || tte[i].bound() == BOUND_EXACT)
          - ((replace->genBound8 & 0xFC) == generation8)
          - (tte[i].depth8 < replace->depth8) < 0)
          replace = &tte[i];

  return found = false, replace;
}
