/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2018 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifdef USE_MPI

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <mpi.h>
#include <string>
#include <vector>

#include "cluster.h"
#include "thread.h"
#include "tt.h"

namespace Cluster {

static int world_rank = MPI_PROC_NULL;
static int world_size = 0;

static MPI_Request reqSignals = MPI_REQUEST_NULL;
static uint64_t signalsCallCounter = 0;

enum Signals : int { SIG_NODES = 0, SIG_STOP = 1, SIG_TB = 2, SIG_NB = 3};
static uint64_t signalsSend[SIG_NB] = {};
static uint64_t signalsRecv[SIG_NB] = {};

static uint64_t nodesSearchedOthers = 0;
static uint64_t tbHitsOthers = 0;
static uint64_t stopSignalsPosted = 0;

static MPI_Comm InputComm = MPI_COMM_NULL;
static MPI_Comm TTComm = MPI_COMM_NULL;
static MPI_Comm MoveComm = MPI_COMM_NULL;
static MPI_Comm signalsComm = MPI_COMM_NULL;

static std::vector<KeyedTTEntry> TTRecvBuff;
static MPI_Request reqGather = MPI_REQUEST_NULL;
static uint64_t gathersPosted = 0;

static std::atomic<uint64_t> TTCacheCounter = {};

static MPI_Datatype MIDatatype = MPI_DATATYPE_NULL;


void init() {

  int thread_support;
  MPI_Init_thread(nullptr, nullptr, MPI_THREAD_MULTIPLE, &thread_support);
  if (thread_support < MPI_THREAD_MULTIPLE)
  {
      std::cerr << "Stockfish requires support for MPI_THREAD_MULTIPLE."
                << std::endl;
      std::exit(EXIT_FAILURE);
  }

  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  const std::array<MPI_Aint, 4> MIdisps = {offsetof(MoveInfo, move),
                                           offsetof(MoveInfo, depth),
                                           offsetof(MoveInfo, score),
                                           offsetof(MoveInfo, rank)};
  MPI_Type_create_hindexed_block(4, 1, MIdisps.data(), MPI_INT, &MIDatatype);
  MPI_Type_commit(&MIDatatype);

  MPI_Comm_dup(MPI_COMM_WORLD, &InputComm);
  MPI_Comm_dup(MPI_COMM_WORLD, &TTComm);
  MPI_Comm_dup(MPI_COMM_WORLD, &MoveComm);
  MPI_Comm_dup(MPI_COMM_WORLD, &signalsComm);
}

void finalize() {


  // free data tyes and communicators
  MPI_Type_free(&MIDatatype);

  MPI_Comm_free(&InputComm);
  MPI_Comm_free(&TTComm);
  MPI_Comm_free(&MoveComm);
  MPI_Comm_free(&signalsComm);

  MPI_Finalize();
}

int size() {

  return world_size;
}

int rank() {

  return world_rank;
}

void ttRecvBuff_resize(size_t nThreads) {

  TTRecvBuff.resize(TTCacheSize * world_size * nThreads);
  std::fill(TTRecvBuff.begin(), TTRecvBuff.end(), KeyedTTEntry());

}


bool getline(std::istream& input, std::string& str) {

  int size;
  std::vector<char> vec;
  bool state;

  if (is_root())
  {
      state = static_cast<bool>(std::getline(input, str));
      vec.assign(str.begin(), str.end());
      size = vec.size();
  }

  // Some MPI implementations use busy-wait polling, while we need yielding
  static MPI_Request reqInput = MPI_REQUEST_NULL;
  MPI_Ibcast(&size, 1, MPI_INT, 0, InputComm, &reqInput);
  if (is_root())
      MPI_Wait(&reqInput, MPI_STATUS_IGNORE);
  else
  {
      while (true)
      {
          int flag;
          MPI_Test(&reqInput, &flag, MPI_STATUS_IGNORE);
          if (flag)
              break;
          else
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
  }

  if (!is_root())
      vec.resize(size);
  MPI_Bcast(vec.data(), size, MPI_CHAR, 0, InputComm);
  if (!is_root())
      str.assign(vec.begin(), vec.end());
  MPI_Bcast(&state, 1, MPI_CXX_BOOL, 0, InputComm);

  return state;
}

void signals_send() {

  signalsSend[SIG_NODES] = Threads.nodes_searched();
  signalsSend[SIG_TB] = Threads.tb_hits();
  signalsSend[SIG_STOP] = Threads.stop;
  MPI_Iallreduce(signalsSend, signalsRecv, SIG_NB, MPI_UINT64_T,
                 MPI_SUM, signalsComm, &reqSignals);
  ++signalsCallCounter;
}

void signals_process() {

  nodesSearchedOthers = signalsRecv[SIG_NODES] - signalsSend[SIG_NODES];
  tbHitsOthers = signalsRecv[SIG_TB] - signalsSend[SIG_TB];
  stopSignalsPosted = signalsRecv[SIG_STOP];
  if (signalsRecv[SIG_STOP] > 0)
      Threads.stop = true;
}

void signals_sync() {

  while(stopSignalsPosted < uint64_t(size()))
      signals_poll();

  // finalize outstanding messages of the signal loops. We might have issued one call less than needed on some ranks.
  uint64_t globalCounter;
  MPI_Allreduce(&signalsCallCounter, &globalCounter, 1, MPI_UINT64_T, MPI_MAX, MoveComm); // MoveComm needed
  if (signalsCallCounter < globalCounter)
      signals_send();

  assert(signalsCallCounter == globalCounter);

  MPI_Wait(&reqSignals, MPI_STATUS_IGNORE);

  signals_process();

  // finalize outstanding messages in the gather loop
  MPI_Allreduce(&gathersPosted, &globalCounter, 1, MPI_UINT64_T, MPI_MAX, MoveComm);
  if (gathersPosted < globalCounter)
  {
     size_t recvBuffPerRankSize = Threads.size() * TTCacheSize;
     MPI_Iallgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                    TTRecvBuff.data(), recvBuffPerRankSize * sizeof(KeyedTTEntry), MPI_BYTE,
                    TTComm, &reqGather);
     ++gathersPosted;
  }
  assert(gathersPosted == globalCounter);

}

void signals_init() {

  stopSignalsPosted = tbHitsOthers = nodesSearchedOthers = 0;

  signalsSend[SIG_NODES] = signalsRecv[SIG_NODES] = 0;
  signalsSend[SIG_TB] = signalsRecv[SIG_TB] = 0;
  signalsSend[SIG_STOP] = signalsRecv[SIG_STOP] = 0;

}

void signals_poll() {

  int flag;
  MPI_Test(&reqSignals, &flag, MPI_STATUS_IGNORE);
  if (flag)
  {
     signals_process();
     signals_send();
  }
}

void save(Thread* thread, TTEntry* tte,
          Key k, Value v, Bound b, Depth d, Move m, Value ev) {

  tte->save(k, v, b, d, m, ev);

  if (d > 5 * ONE_PLY)
  {
     // Try to add to thread's send buffer
     {
         std::lock_guard<Mutex> lk(thread->ttCache.mutex);
         thread->ttCache.buffer.replace(KeyedTTEntry(k,*tte));
	 ++TTCacheCounter;
     }

     size_t recvBuffPerRankSize = Threads.size() * TTCacheSize;

     // Communicate on main search thread
     if (thread == Threads.main() && TTCacheCounter > size() * recvBuffPerRankSize)
     {
         // Test communication status
         int flag;
         MPI_Test(&reqGather, &flag, MPI_STATUS_IGNORE);

         // Current communication is complete
         if (flag)
         {
             // Save all received entries to TT, and store our TTCaches, ready for the next round of communication
             for (size_t irank = 0; irank < size_t(size()) ; ++irank)
             {
                 if (irank == size_t(rank()))
                 {
                    // Copy from the thread caches to the right spot in the buffer
                    size_t i = irank * recvBuffPerRankSize;
                    for (auto&& th : Threads)
                    {
                        std::lock_guard<Mutex> lk(th->ttCache.mutex);

                        for (auto&& e : th->ttCache.buffer)
                            TTRecvBuff[i++] = e;

                        // Reset thread's send buffer
                        th->ttCache.buffer = {};
                    }

	            TTCacheCounter = 0;
                 }
                 else
                    for (size_t i = irank * recvBuffPerRankSize; i < (irank + 1) * recvBuffPerRankSize; ++i)
                    {
                        auto&& e = TTRecvBuff[i];
                        bool found;
                        TTEntry* replace_tte;
                        replace_tte = TT.probe(e.first, found);
                        replace_tte->save(e.first, e.second.value(), e.second.bound(), e.second.depth(),
                                          e.second.move(), e.second.eval());
                    }
             }

             // Start next communication
             MPI_Iallgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                            TTRecvBuff.data(), recvBuffPerRankSize * sizeof(KeyedTTEntry), MPI_BYTE,
                            TTComm, &reqGather);
             ++gathersPosted;

	     // Force check of time on the next occasion.
             static_cast<MainThread*>(thread)->callsCnt = 0;

         }
     }
  }
}


// TODO update to the scheme in master.. can this use aggregation of votes?
void pick_moves(MoveInfo& mi) {

  MoveInfo* pMoveInfo = NULL;
  if (is_root())
  {
      pMoveInfo = (MoveInfo*)malloc(sizeof(MoveInfo) * size());
  }
  MPI_Gather(&mi, 1, MIDatatype, pMoveInfo, 1, MIDatatype, 0, MoveComm);

  if (is_root())
  {
      std::map<int, int> votes;
      int minScore = pMoveInfo[0].score;
      for (int i = 0; i < size(); ++i)
      {
          minScore = std::min(minScore, pMoveInfo[i].score);
          votes[pMoveInfo[i].move] = 0;
      }
      for (int i = 0; i < size(); ++i)
      {
          votes[pMoveInfo[i].move] += pMoveInfo[i].score - minScore + pMoveInfo[i].depth;
      }
      int bestVote = votes[pMoveInfo[0].move];
      for (int i = 0; i < size(); ++i)
      {
          if (votes[pMoveInfo[i].move] > bestVote)
          {
              bestVote = votes[pMoveInfo[i].move];
              mi = pMoveInfo[i];
          }
      }
      free(pMoveInfo);
  }
  MPI_Bcast(&mi, 1, MIDatatype, 0, MoveComm);
}

uint64_t nodes_searched() {

  return nodesSearchedOthers + Threads.nodes_searched();
}

uint64_t tb_hits() {

  return tbHitsOthers + Threads.tb_hits();
}

}

#else

#include "cluster.h"
#include "thread.h"

namespace Cluster {

uint64_t nodes_searched() {

  return Threads.nodes_searched();
}

uint64_t tb_hits() {

  return Threads.tb_hits();
}

}

#endif // USE_MPI
