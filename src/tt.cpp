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


////
//// Includes
////

#include <cassert>
#include <cmath>
#include <cstring>
#if !(defined(__hpux) || defined(__ppc__) || defined(__ppc64__) || defined(__arm__))
#  include <xmmintrin.h>
#endif

#include "movegen.h"
#include "tt.h"

// The main transposition table
TranspositionTable TT;

////
//// Functions
////

TranspositionTable::TranspositionTable() {

  size = writes = 0;
  entries = 0;
  generation = 0;
}

TranspositionTable::~TranspositionTable() {

  delete [] entries;
}


/// TranspositionTable::set_size sets the size of the transposition table,
/// measured in megabytes.

void TranspositionTable::set_size(size_t mbSize) {

  size_t newSize = 1024;

  // We store a cluster of ClusterSize number of TTEntry for each position
  // and newSize is the maximum number of storable positions.
  while ((2 * newSize) * sizeof(TTCluster) <= (mbSize << 20))
      newSize *= 2;

  if (newSize != size)
  {
      size = newSize;
      delete [] entries;
      entries = new TTCluster[size];
      if (!entries)
      {
          std::cerr << "Failed to allocate " << mbSize
                    << " MB for transposition table." << std::endl;
          Application::exit_with_failure();
      }
      clear();
  }
}


/// TranspositionTable::clear overwrites the entire transposition table
/// with zeroes. It is called whenever the table is resized, or when the
/// user asks the program to clear the table (from the UCI interface).
/// Perhaps we should also clear it when the "ucinewgame" command is recieved?

void TranspositionTable::clear() {

  memset(entries, 0, size * sizeof(TTCluster));
}


/// TranspositionTable::first_entry returns a pointer to the first
/// entry of a cluster given a position. The low 32 bits of the key
/// are used to get the index in the table.

inline TTEntry* TranspositionTable::first_entry(const Key posKey) const {

  return entries[uint32_t(posKey) & (size - 1)].data;
}


/// TranspositionTable::store writes a new entry containing a position,
/// a value, a value type, a search depth, and a best move to the
/// transposition table. Transposition table is organized in clusters of
/// four TTEntry objects, and when a new entry is written, it replaces
/// the least valuable of the four entries in a cluster. A TTEntry t1 is
/// considered to be more valuable than a TTEntry t2 if t1 is from the
/// current search and t2 is from a previous search, or if the depth of t1
/// is bigger than the depth of t2. A TTEntry of type VALUE_TYPE_EVAL
/// never replaces another entry for the same position.

void TranspositionTable::store(const Key posKey, Value v, ValueType t, Depth d, Move m) {

  TTEntry *tte, *replace;
  uint32_t posKey32 = posKey >> 32; // Use the high 32 bits as key

  tte = replace = first_entry(posKey);
  for (int i = 0; i < ClusterSize; i++, tte++)
  {
      if (!tte->key() || tte->key() == posKey32) // empty or overwrite old
      {
          // Do not overwrite when new type is VALUE_TYPE_EV_LO
          if (tte->key() && t == VALUE_TYPE_EV_LO)
              return;

          // Preserve any exsisting ttMove
          if (m == MOVE_NONE)
              m = tte->move();

          *tte = TTEntry(posKey32, v, t, d, m, generation);
          return;
      }
      else if (i == 0)  // replace would be a no-op in this common case
          continue;

      int c1 = (replace->generation() == generation ?  2 : 0);
      int c2 = (tte->generation() == generation ? -2 : 0);
      int c3 = (tte->depth() < replace->depth() ?  1 : 0);

      if (c1 + c2 + c3 > 0)
          replace = tte;
  }
  *replace = TTEntry(posKey32, v, t, d, m, generation);
  writes++;
}


/// TranspositionTable::retrieve looks up the current position in the
/// transposition table. Returns a pointer to the TTEntry or NULL
/// if position is not found.

TTEntry* TranspositionTable::retrieve(const Key posKey) const {

  uint32_t posKey32 = posKey >> 32;
  TTEntry* tte = first_entry(posKey);

  for (int i = 0; i < ClusterSize; i++, tte++)
      if (tte->key() == posKey32)
          return tte;

  return NULL;
}


/// TranspositionTable::prefetch looks up the current position in the
/// transposition table and load it in L1/L2 cache. This is a non
/// blocking function and do not stalls the CPU waiting for data
/// to be loaded from RAM, that can be very slow. When we will
/// subsequently call retrieve() the TT data will be already
/// quickly accessible in L1/L2 CPU cache.
#if defined(__hpux) || defined(__ppc__) || defined(__ppc64__) || defined(__arm__)
void TranspositionTable::prefetch(const Key) const {} // Not supported on HP UX
#else

void TranspositionTable::prefetch(const Key posKey) const {

#if defined(__INTEL_COMPILER) || defined(__ICL)
   // This hack prevents prefetches to be optimized away by
   // Intel compiler. Both MSVC and gcc seems not affected.
   __asm__ ("");
#endif

   char const* addr = (char*)first_entry(posKey);
  _mm_prefetch(addr, _MM_HINT_T2);
  _mm_prefetch(addr+64, _MM_HINT_T2); // 64 bytes ahead
}

#endif

/// TranspositionTable::new_search() is called at the beginning of every new
/// search. It increments the "generation" variable, which is used to
/// distinguish transposition table entries from previous searches from
/// entries from the current search.

void TranspositionTable::new_search() {

  generation++;
  writes = 0;
}


/// TranspositionTable::insert_pv() is called at the end of a search
/// iteration, and inserts the PV back into the PV. This makes sure
/// the old PV moves are searched first, even if the old TT entries
/// have been overwritten.

void TranspositionTable::insert_pv(const Position& pos, Move pv[]) {

  StateInfo st;
  Position p(pos);

  for (int i = 0; pv[i] != MOVE_NONE; i++)
  {
      TTEntry *tte = retrieve(p.get_key());
      if (!tte || tte->move() != pv[i])
          store(p.get_key(), VALUE_NONE, VALUE_TYPE_NONE, Depth(-127*OnePly), pv[i]);
      p.do_move(pv[i], st);
  }
}


/// TranspositionTable::extract_pv() extends a PV by adding moves from the
/// transposition table at the end. This should ensure that the PV is almost
/// always at least two plies long, which is important, because otherwise we
/// will often get single-move PVs when the search stops while failing high,
/// and a single-move PV means that we don't have a ponder move.

void TranspositionTable::extract_pv(const Position& pos, Move pv[], const int PLY_MAX) {

  const TTEntry* tte;
  StateInfo st;
  Position p(pos);
  int ply = 0;

  // Update position to the end of current PV
  while (pv[ply] != MOVE_NONE)
      p.do_move(pv[ply++], st);

  // Try to add moves from TT while possible
  while (   (tte = retrieve(p.get_key())) != NULL
         && tte->move() != MOVE_NONE
         && move_is_legal(p, tte->move())
         && (!p.is_draw() || ply < 2)
         && ply < PLY_MAX)
  {
      pv[ply] = tte->move();
      p.do_move(pv[ply++], st);
  }
  pv[ply] = MOVE_NONE;
}


/// TranspositionTable::full() returns the permill of all transposition table
/// entries which have received at least one write during the current search.
/// It is used to display the "info hashfull ..." information in UCI.

int TranspositionTable::full() const {

  double N = double(size) * ClusterSize;
  return int(1000 * (1 - exp(writes * log(1.0 - 1.0/N))));
}
