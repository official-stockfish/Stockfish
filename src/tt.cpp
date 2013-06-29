/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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


/// TranspositionTable::set_size() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of ClusterSize number of TTEntry.

void TranspositionTable::set_size(size_t mbSize) {

  assert(msb((mbSize << 20) / sizeof(TTEntry)) < 32);

  uint32_t size = ClusterSize << msb((mbSize << 20) / sizeof(TTEntry[ClusterSize]));

  if (hashMask == size - ClusterSize)
      return;

  hashMask = size - ClusterSize;
  free(mem);
  mem = calloc(size * sizeof(TTEntry) + CACHE_LINE_SIZE - 1, 1);

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

  memset(table, 0, (hashMask + ClusterSize) * sizeof(TTEntry));
}


/// TranspositionTable::probe() looks up the current position in the
/// transposition table. Returns a pointer to the TTEntry or NULL if
/// position is not found.

const TTEntry* TranspositionTable::probe(const Key key) const {

  const TTEntry* tte = first_entry(key);
  uint32_t key32 = key >> 32;

  for (unsigned i = 0; i < ClusterSize; i++, tte++)
      if (tte->key() == key32)
          return tte;

  return NULL;
}


/// TranspositionTable::store() writes a new entry containing position key and
/// valuable information of current position. The lowest order bits of position
/// key are used to decide on which cluster the position will be placed.
/// When a new entry is written and there are no empty entries available in cluster,
/// it replaces the least valuable of entries. A TTEntry t1 is considered to be
/// more valuable than a TTEntry t2 if t1 is from the current search and t2 is from
/// a previous search, or if the depth of t1 is bigger than the depth of t2.

void TranspositionTable::store(const Key key, Value v, Bound t, Depth d, Move m, Value statV, Value kingD) {

  int c1, c2, c3;
  TTEntry *tte, *replace;
  uint32_t key32 = key >> 32; // Use the high 32 bits as key inside the cluster

  tte = replace = first_entry(key);

  for (unsigned i = 0; i < ClusterSize; i++, tte++)
  {
      if (!tte->key() || tte->key() == key32) // Empty or overwrite old
      {
          if (!m)
              m = tte->move(); // Preserve any existing ttMove

          replace = tte;
          break;
      }

      // Implement replace strategy
      c1 = (replace->generation() == generation ?  2 : 0);
      c2 = (tte->generation() == generation || tte->bound() == BOUND_EXACT ? -2 : 0);
      c3 = (tte->depth() < replace->depth() ?  1 : 0);

      if (c1 + c2 + c3 > 0)
          replace = tte;
  }

  replace->save(key32, v, t, d, m, generation, statV, kingD);
}
