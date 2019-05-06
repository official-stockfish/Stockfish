/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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
#include <fstream>


#include "bitboard.h"
#include "misc.h"
#include "tt.h"
#include "uci.h"


using namespace std;

TranspositionTable EXP; // Our global transposition table

MCTSHashTable MCTS;

TranspositionTable TT; // Our global transposition table


Value value_to_tt(Value v, int ply);

/// TTEntry::save saves a TTEntry
void TTEntry::save(Key k, Value v, Bound b, Depth d, Move m, Value ev) {

  assert(d / ONE_PLY * ONE_PLY == d);

  // Preserve any existing move for the same position
  if (m || (k >> 48) != key16)
      move16 = (uint16_t)m;

  // Overwrite less valuable entries
  if (  (k >> 48) != key16
      || d / ONE_PLY > depth8 - 4
      || b == BOUND_EXACT)
  {
      key16     = (uint16_t)(k >> 48);
      value16   = (int16_t)v;
      eval16    = (int16_t)ev;
      genBound8 = (uint8_t)(TT.generation8 | b);
      depth8    = (int8_t)(d / ONE_PLY);
  }
}


/// TranspositionTable::resize() sets the size of the transposition table,
/// measured in megabytes. Transposition table consists of a power of 2 number
/// of clusters and each cluster consists of ClusterSize number of TTEntry.

void TranspositionTable::resize(size_t mbSize) {

  clusterCount = mbSize * 1024 * 1024 / sizeof(Cluster);

  free(mem);
  mem = malloc(clusterCount * sizeof(Cluster) + CacheLineSize - 1);

  if (!mem)
  {
      std::cerr << "Failed to allocate " << mbSize
                << "MB for transposition table." << std::endl;
      exit(EXIT_FAILURE);
  }

  table = (Cluster*)((uintptr_t(mem) + CacheLineSize - 1) & ~(CacheLineSize - 1));
  clear();
}


/// TranspositionTable::clear() initializes the entire transposition table to zero,
//  in a multi-threaded way.

void TranspositionTable::clear() {

  std::vector<std::thread> threads;

  for (size_t idx = 0; idx < Options["Threads"]; idx++)
  {
      threads.emplace_back([this, idx]() {

          // Thread binding gives faster search on systems with a first-touch policy
          if (Options["Threads"] > 8)
              WinProcGroup::bindThisThread(idx);

          // Each thread will zero its part of the hash table
          const size_t stride = clusterCount / Options["Threads"],
                       start  = stride * idx,
                       len    = idx != Options["Threads"] - 1 ?
                                stride : clusterCount - start;

          std::memset(&table[start], 0, len * sizeof(Cluster));
      });
  }

  for (std::thread& th: threads)
      th.join();
}

/// TranspositionTable::probe() looks up the current position in the transposition
/// table. It returns true and a pointer to the TTEntry if the position is found.
/// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
/// to be replaced later. The replace value of an entry is calculated as its depth
/// minus 8 times its relative age. TTEntry t1 is considered more valuable than
/// TTEntry t2 if its replace value is greater than that of t2.

TTEntry* TranspositionTable::probe(const Key key, bool& found) const {

  TTEntry* const tte = first_entry(key);
  const uint16_t key16 = key >> 48;  // Use the high 16 bits as key inside the cluster

  for (int i = 0; i < ClusterSize; ++i)
      if (!tte[i].key16 || tte[i].key16 == key16)
      {
          tte[i].genBound8 = uint8_t(generation8 | tte[i].bound()); // Refresh

          return found = (bool)tte[i].key16, &tte[i];
      }

  // Find an entry to be replaced according to the replacement strategy
  TTEntry* replace = tte;
  for (int i = 1; i < ClusterSize; ++i)
      // Due to our packed storage format for generation and its cyclic
      // nature we add 259 (256 is the modulus plus 3 to keep the lowest
      // two bound bits from affecting the result) to calculate the entry
      // age correctly even after generation8 overflows into the next cycle.
      if (  replace->depth8 - ((259 + generation8 - replace->genBound8) & 0xFC) * 2
          >   tte[i].depth8 - ((259 + generation8 -   tte[i].genBound8) & 0xFC) * 2)
          replace = &tte[i];

  return found = false, replace;
}


/// TranspositionTable::hashfull() returns an approximation of the hashtable
/// occupation during a search. The hash is x permill full, as per UCI protocol.

int TranspositionTable::hashfull() const {

  int cnt = 0;
  for (int i = 0; i < 1000 / ClusterSize; i++)
  {
      const TTEntry* tte = &table[i].entry[0];
      for (int j = 0; j < ClusterSize; j++)
          if ((tte[j].genBound8 & 0xFC) == generation8)
              cnt++;
  }
  return cnt;
}

Value value_to_tt(Value v, int ply) {

	assert(v != VALUE_NONE);

	return  v >= VALUE_MATE_IN_MAX_PLY ? v + ply
		: v <= VALUE_MATED_IN_MAX_PLY ? v - ply : v;
}

void EXPresize(size_t mbSize) {

	ifstream myFile("experience.bin", ios::in | ios::binary);


	int load = 1;
	while (load)
	{
		ExpEntry tempExpEntry;
		tempExpEntry.depth = Depth(0);
		tempExpEntry.hashkey = 0;
		tempExpEntry.move = Move(0);
		tempExpEntry.score = Value(0);

		myFile.read((char*)&tempExpEntry, sizeof(tempExpEntry));

		if (tempExpEntry.hashkey)
		{

			mctsInsert(tempExpEntry);
		}
		else
			load = 0;



		if (!tempExpEntry.hashkey)
			load = 0;
	}
	myFile.close();

}
void EXPawnresize() {

	ifstream myFile("pawngame.bin", ios::in | ios::binary);


	int load = 1;
	while (load)
	{
		ExpEntry tempExpEntry;
		tempExpEntry.depth = Depth(0);
		tempExpEntry.hashkey = 0;
		tempExpEntry.move = Move(0);
		tempExpEntry.score = Value(0);

		myFile.read((char*)&tempExpEntry, sizeof(tempExpEntry));

		if (tempExpEntry.hashkey)
		{
			mctsInsert(tempExpEntry);
		}
		else
			load = 0;


		if (!tempExpEntry.hashkey)
			load = 0;
	}
	myFile.close();

}
void EXPload(char* fen)
{

	ifstream myFile(fen, ios::in | ios::binary);




	int load = 1;

	while (load)
	{
		ExpEntry tempExpEntry;
		tempExpEntry.depth = Depth(0);
		tempExpEntry.hashkey = 0;
		tempExpEntry.move = Move(0);
		tempExpEntry.score = Value(0);
		myFile.read((char*)&tempExpEntry, sizeof(tempExpEntry));

		if (tempExpEntry.hashkey)
		{
			mctsInsert(tempExpEntry);
		}
		load = 0;


		if (!tempExpEntry.hashkey)
			load = 0;
	}
	myFile.close();
}

void mctsInsert(ExpEntry tempExpEntry)
{
	// If the node already exists in the hash table, we want to return it.
	// We search in the range of all the hash table entries with key "key1".
	auto range = MCTS.equal_range(tempExpEntry.hashkey);
	auto it1 = range.first;
	auto it2 = range.second;

	bool newNode = true;
	while (it1 != it2)
	{
		Node node = &(it1->second);

		if (node->hashkey == tempExpEntry.hashkey)
		{
			bool newChild = true;
			newNode = false;
			node->lateChild.move = tempExpEntry.move;
			node->lateChild.score = tempExpEntry.score;
			node->lateChild.depth = tempExpEntry.depth;
			for (int x = 0; x < node->sons; x++)
			{
				if (node->child[x].move == tempExpEntry.move)
				{
					newChild = false;
					node->child[x].move = tempExpEntry.move;
					node->child[x].depth = tempExpEntry.depth;
					node->child[x].score = tempExpEntry.score;
					node->child[x].Visits++;
					//	node->sons++;
					node->totalVisits++;
					break;
				}
			}
			if (newChild && node->sons < MAX_CHILDREN)
			{
				node->child[node->sons].move = tempExpEntry.move;
				node->child[node->sons].depth = tempExpEntry.depth;
				node->child[node->sons].score = tempExpEntry.score;
				node->child[node->sons].Visits++;
				node->sons++;
				node->totalVisits++;
			}



		}

		it1++;
	}

	if (newNode)
	{
		// Node was not found, so we have to create a new one
		NodeInfo infos;

		infos.hashkey = 0;        // Zobrist hash of pawns
		infos.sons = 0;

		infos.totalVisits = 0;// number of visits by the Monte-Carlo algorithm
		infos.child[0].move = MOVE_NONE;
		infos.child[0].depth = DEPTH_NONE;
		infos.child[0].score = VALUE_NONE;
		infos.child[0].Visits = 0;
		std::memset(infos.child, 0, sizeof(Child) * 20);
		infos.lateChild.move = MOVE_NONE;;
		infos.lateChild.score = VALUE_NONE;
		infos.lateChild.depth = DEPTH_NONE;

		infos.lateChild.move = tempExpEntry.move;;
		infos.lateChild.score = tempExpEntry.score;
		infos.lateChild.depth = tempExpEntry.depth;
		infos.hashkey = tempExpEntry.hashkey;        // Zobrist hash of pawns
		infos.sons = 1;       // number of visits by the Monte-Carlo algorithm
		infos.totalVisits = 1;
		infos.child[0].move = tempExpEntry.move;
		infos.child[0].depth = tempExpEntry.depth;
		infos.child[0].score = tempExpEntry.score;
		infos.child[0].Visits = 1;       // number of sons expanded by the Monte-Carlo algorithm
										 //infos.lastMove = MOVE_NONE; // the move between the parent and this node

										 //debug << "inserting into the hash table: key = " << key1 << endl;

		auto it = MCTS.insert(make_pair(tempExpEntry.hashkey, infos));
	}
}

/// get_node() probes the Monte-Carlo hash table to find the node with the given
/// position, creating a new entry if it doesn't exist yet in the table.
/// The returned node is always valid.
Node get_node(Key key) {


	// If the node already exists in the hash table, we want to return it.
	// We search in the range of all the hash table entries with key "key1".

	Node mynode;
	auto range = MCTS.equal_range(key);
	auto it1 = range.first;
	auto it2 = range.second;

	mynode = &(it1->second);

	while (it1 != it2)
	{
		Node node = &(it1->second);
		if (node->hashkey == key)
			return node;

		it1++;
	}

	return mynode;
}