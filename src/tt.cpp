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

inline void refreshGen5(TTEntryPacked &tte, uint8_t newGen5)
{
  setBitField<TTEntryPacked::Gen5>(tte.bits, newGen5);
}

inline int32_t ageDepthByGen(uint8_t depth8, uint8_t curGen5, uint8_t prevGen5)
{
  constexpr uint8_t genDepthPenalty = 8; // every gen means depth reduction by 8
  const uint8_t aging = ((curGen5 - prevGen5) & 0x1FU) * genDepthPenalty;

  return int32_t(depth8) - int32_t(aging);
}

inline uint32_t getClusterEntryKey(uint64_t clusterKeys, uint8_t slotIndex)
{
  constexpr unsigned fieldBits = TranspositionTable::HashEntryKeyField::numBits;;
  return bitExtract(clusterKeys, slotIndex * fieldBits, fieldBits);
}

inline void setClusterEntryKey(uint64_t &clusterKeys, uint8_t slotIndex, uint32_t newKey)
{
  constexpr unsigned fieldBits = TranspositionTable::HashEntryKeyField::numBits;;
  clusterKeys = bitFieldSet(clusterKeys, slotIndex * fieldBits, fieldBits, newKey);
}

inline uint32_t entryKeyFromZobrist(Key k)
{
  return extractBitField<TranspositionTable::HashEntryKeyField>(k);
}

inline uint32_t extraKeyFromZobrist(Key k)
{
  return extractBitField<TranspositionTable::HashExtraKeyField>(k);
}

Move decodeTTMove(uint32_t encodedMove)
{
  encodedMove -= static_cast<bool>(encodedMove);

  if (encodedMove & 0x1000U)
  {
      // special move type, need to figure out which one by from source rank

      uint32_t ret;
      uint32_t promotionBits;

      encodedMove &= 0xFFFU;

      switch ((encodedMove & 07000U) >> 9)
      {
          case RANK_1: // 0, 7
          case RANK_8:
              ret = encodedMove | CASTLING;
              break;

          case RANK_2: // black promotion
              promotionBits = PROMOTION | 00000U | (encodedMove & 00070) << 9;
              ret = (encodedMove & 07707U) | promotionBits;
              break;

          case RANK_7: // write promotion
              promotionBits = PROMOTION | 00070U | (encodedMove & 00070) << 9;
              ret = (encodedMove & 07707U) | promotionBits;
              break;

              // rank 4/5
          default:
              ret = ENPASSANT | encodedMove;
              break;
      }

      return Move(ret);
  }
  else
      return Move(encodedMove);
}

uint32_t encodeTTMove(Move m)
{
  // ensure that no one uses move H8->H8 for any special purpose
#if !defined(NDEBUG)
  if ((m & 0xFFFU) == 0xFFFU)
      fprintf(stderr, "Move: 0x%04x From=%c%u To=%c%u\n",
              m,  file_of(from_sq(m))+'A', 1 + rank_of(from_sq(m)), 'A'+file_of(to_sq(m)), 1+rank_of(to_sq(m)));
  assert((m & 0xFFFU) != 0xFFFU);
#endif

  uint32_t encodedMove;

  if (type_of(m) == PROMOTION)
  {
      uint32_t promotionBits = (m >> 12) & 3;
      encodedMove = m & 07707U; // remove destination rank and move type bits
      encodedMove |= promotionBits << 3; // add promotion piece to destination rank
      encodedMove |= 0x1000U; // set the special bit
  }
  else
  {
      bool special = (m >> 12); // we want also the promotion bits
      encodedMove = m & 07777U;
      encodedMove |= uint32_t(special) << 12;
  }

  // ensure that MOVE_NONE does not get encoded as 0. This is for slot occupancy
  // test.
  encodedMove++;

#if !defined(NDEBUG)
  if (decodeTTMove(encodedMove) != m)
  {
      fprintf(stderr, "Move: 0x%04x  Encoded: 0x%04x  Decoded: 0x%04x From=%c%u To=%c%u\n",
              m, encodedMove, decodeTTMove(encodedMove), file_of(from_sq(m))+'A', 1 + rank_of(from_sq(m)), 'A'+file_of(to_sq(m)), 1+rank_of(to_sq(m)));
      assert(decodeTTMove(encodedMove) == m);
  }
  // range check
  assert(encodedMove > 0U);
  assert(encodedMove < (1U << 13));
#endif

  return encodedMove;
}

}

TranspositionTable TT; // Our global transposition table


void TTEntry::load(uint64_t bits, size_t clusterIndex, uint8_t slotIndex)
{
  m_move = decodeTTMove(extractBitField<TTEntryPacked::Move13>(bits));
  m_value = extractBitField<TTEntryPacked::Value16>(bits); // stored as int16_t for sign extension
  m_eval = extractBitField<TTEntryPacked::Eval16>(bits);   // stored as int16_t for sign extension
  m_depth = (Depth)(extractBitField<TTEntryPacked::Depth8>(bits) + DEPTH_OFFSET);
  m_pv = extractBitField<TTEntryPacked::Pv>(bits);
  m_bound = extractBitField<TTEntryPacked::Bound2>(bits);

  m_clusterIndex = clusterIndex;
  m_slotIndex = slotIndex;
}

void TTEntry::reset(size_t clusterIndex, uint8_t slotIndex)
{
  m_move = MOVE_NONE;
  m_value = Value(0);
  m_eval = Value(0);
  m_depth = Depth(0);
  m_pv = false;
  m_bound = BOUND_NONE;

  m_clusterIndex = clusterIndex;
  m_slotIndex = slotIndex;
}

/// TTEntry::save populates the TTEntry with a new node's data, possibly
/// overwriting an old position. Update is not atomic and can be racy.
void TTEntry::save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev) {

  bool doStore = false;
  TranspositionTable::Cluster &cluster = TT.table[m_clusterIndex];

  // reload the TT entry, it may have been changed by recursive search since
  // we loaded it
  TTEntryPacked packedData = cluster.entry[m_slotIndex];
  const uint32_t entryKey = getClusterEntryKey(cluster.keys, m_slotIndex);
  const uint8_t extraKey = extractBitField<TTEntryPacked::ExtraHash3>(packedData.bits);

  // Preserve any existing move for the same position
  if (m || entryKeyFromZobrist(k) != entryKey || extraKeyFromZobrist(k) != extraKey)
  {
      setBitField<TTEntryPacked::Move13>(packedData.bits, encodeTTMove(m));
      doStore = true;
  }

  // Overwrite less valuable entries
  if (entryKeyFromZobrist(k) != entryKey || extraKeyFromZobrist(k) != extraKey
       || d - DEPTH_OFFSET > extractBitField<TTEntryPacked::Depth8>(packedData.bits) - 4
       || b == BOUND_EXACT)
  {
      assert(d >= DEPTH_OFFSET);

      setClusterEntryKey(cluster.keys, m_slotIndex, k);
      setBitField<TTEntryPacked::ExtraHash3>(packedData.bits, extraKeyFromZobrist(k));
      setBitField<TTEntryPacked::Value16>(packedData.bits, v);
      setBitField<TTEntryPacked::Eval16>(packedData.bits, ev);
      setBitField<TTEntryPacked::Gen5>(packedData.bits, TT.generation5);
      setBitField<TTEntryPacked::Pv>(packedData.bits, pv);
      setBitField<TTEntryPacked::Bound2>(packedData.bits, b);
      setBitField<TTEntryPacked::Depth8>(packedData.bits, d - DEPTH_OFFSET);
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
bool TranspositionTable::probe(const Key fullKey, TTEntry &entry) const {

  const size_t clusterIndex = mul_hi64(fullKey, clusterCount);
  Cluster& cluster = TT.table[clusterIndex];
  TTEntryPacked* const tte = &cluster.entry[0];
  const uint32_t key = entryKeyFromZobrist(fullKey);
  const uint8_t extraKey = extraKeyFromZobrist(fullKey);

  for (int i = 0; i < ClusterSize; ++i)
  {
      const uint32_t tteKey = getClusterEntryKey(cluster.keys, i);
      const uint32_t tteExtraKey = extractBitField<TTEntryPacked::ExtraHash3>(tte[i].bits);
      const bool occupied = (extractBitField<TTEntryPacked::Move13>(tte[i].bits) != 0);

      if (!occupied || (tteKey == key && tteExtraKey == extraKey))
      {
          refreshGen5(tte[i], generation5); // Refresh gen

          entry.load(tte[i].bits, clusterIndex, i);
          return occupied;
      }
  }

  // Find an entry to be replaced according to the replacement strategy
  uint8_t slotIndex = 0;
  int32_t replaceAgedDepth =
          ageDepthByGen(extractBitField<TTEntryPacked::Depth8>(tte[0].bits), generation5, extractBitField<TTEntryPacked::Gen5>(tte[0].bits));

  for (int i = 1; i < ClusterSize; ++i)
  {
      const int32_t tteAgedDepth = ageDepthByGen(extractBitField<TTEntryPacked::Depth8>(tte[i].bits),
                                                 generation5, extractBitField<TTEntryPacked::Gen5>(tte[i].bits));

      if (replaceAgedDepth > tteAgedDepth)
      {
          replaceAgedDepth = tteAgedDepth;
          slotIndex = i;
      }
  }

  entry.reset(clusterIndex, slotIndex);
  return false;
}


/// TranspositionTable::hashfull() returns an approximation of the hashtable
/// occupation during a search. The hash is x permill full, as per UCI protocol.

int TranspositionTable::hashfull() const {

  int cnt = 0;
  for (int i = 0; i < 1000; ++i)
      for (int j = 0; j < ClusterSize; ++j)
          cnt += extractBitField<TTEntryPacked::Gen5>(table[i].entry[j].bits) == generation5;

  return cnt / ClusterSize;
}
