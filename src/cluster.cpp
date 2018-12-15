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
static bool stop_signal = false;
static MPI_Request reqStop = MPI_REQUEST_NULL;

static MPI_Comm InputComm = MPI_COMM_NULL;
static MPI_Comm TTComm = MPI_COMM_NULL;
static MPI_Comm MoveComm = MPI_COMM_NULL;
static MPI_Comm StopComm = MPI_COMM_NULL;

static std::vector<KeyedTTEntry> TTBuff;

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

  TTBuff.resize(TTSendBufferSize * world_size);

  const std::array<MPI_Aint, 4> MIdisps = {offsetof(MoveInfo, move),
                                           offsetof(MoveInfo, depth),
                                           offsetof(MoveInfo, score),
                                           offsetof(MoveInfo, rank)};
  MPI_Type_create_hindexed_block(4, 1, MIdisps.data(), MPI_INT, &MIDatatype);
  MPI_Type_commit(&MIDatatype);

  MPI_Comm_dup(MPI_COMM_WORLD, &InputComm);
  MPI_Comm_dup(MPI_COMM_WORLD, &TTComm);
  MPI_Comm_dup(MPI_COMM_WORLD, &MoveComm);
  MPI_Comm_dup(MPI_COMM_WORLD, &StopComm);
}

void finalize() {

  MPI_Finalize();
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

  // Some MPI implementations use busy-wait pooling, while we need yielding
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

void sync_start() {

  stop_signal = false;

  // Start listening to stop signal
  if (!is_root())
      MPI_Ibarrier(StopComm, &reqStop);
}

void sync_stop() {

  if (is_root())
  {
      if (!stop_signal && Threads.stop)
      {
          // Signal the cluster about stopping
          stop_signal = true;
          MPI_Ibarrier(StopComm, &reqStop);
          MPI_Wait(&reqStop, MPI_STATUS_IGNORE);
      }
  }
  else
  {
      int flagStop;
      // Check if we've received any stop signal
      MPI_Test(&reqStop, &flagStop, MPI_STATUS_IGNORE);
      if (flagStop)
          Threads.stop = true;
  }
}

int size() {

  return world_size;
}

int rank() {

  return world_rank;
}

void save(Thread* thread, TTEntry* tte,
          Key k, Value v, Bound b, Depth d, Move m, Value ev) {

  tte->save(k, v, b, d, m, ev);

  if (d > 5 * ONE_PLY)
  {
     // Try to add to thread's send buffer
     {
         std::lock_guard<Mutex> lk(thread->ttBuffer.mutex);
         thread->ttBuffer.buffer.replace(KeyedTTEntry(k,*tte));
         ++thread->ttBuffer.counter;
     }

     // Communicate on main search thread
     if (thread == Threads.main() && thread->ttBuffer.counter * Threads.size() > TTSendBufferSize)
     {
         static MPI_Request req = MPI_REQUEST_NULL;
         static TTSendBuffer<TTSendBufferSize> send_buff = {};
         int flag;

         // Test communication status
         MPI_Test(&req, &flag, MPI_STATUS_IGNORE);

         // Current communication is complete
         if (flag)
         {
             // Save all received entries (except ours)
             for (size_t irank = 0; irank < size_t(size()) ; ++irank)
             {
                 if (irank == size_t(rank()))
                     continue;

                 for (size_t i = irank * TTSendBufferSize ; i < (irank + 1) * TTSendBufferSize; ++i)
                 {
                     auto&& e = TTBuff[i];
                     bool found;
                     TTEntry* replace_tte;
                     replace_tte = TT.probe(e.first, found);
                     replace_tte->save(e.first, e.second.value(), e.second.bound(), e.second.depth(),
                                       e.second.move(), e.second.eval());
                 }
             }

             // Reset send buffer
             send_buff = {};

             // Build up new send buffer: best 16 found across all threads
             for (auto&& th : Threads)
             {
                 std::lock_guard<Mutex> lk(th->ttBuffer.mutex);
                 for (auto&& e : th->ttBuffer.buffer)
                     send_buff.replace(e);
                 // Reset thread's send buffer
                 th->ttBuffer.buffer = {};
                 th->ttBuffer.counter = 0;
             }

             // Start next communication
             MPI_Iallgather(send_buff.data(), send_buff.size() * sizeof(KeyedTTEntry), MPI_BYTE,
                            TTBuff.data(), TTSendBufferSize * sizeof(KeyedTTEntry), MPI_BYTE,
                            TTComm, &req);
         }
     }
  }
}

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

void sum(uint64_t& val) {

  const uint64_t send = val;
  MPI_Reduce(&send, &val, 1, MPI_UINT64_T, MPI_SUM, 0, MoveComm);
}

}

#endif // USE_MPI
