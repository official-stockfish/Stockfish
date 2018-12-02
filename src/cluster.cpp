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

static MPI_Datatype TTEntryDatatype = MPI_DATATYPE_NULL;
static std::vector<TTEntry> TTBuff;

static MPI_Op BestMoveOp = MPI_OP_NULL;
static MPI_Datatype MIDatatype = MPI_DATATYPE_NULL;

static void BestMove(void* in, void* inout, int* len, MPI_Datatype* datatype) {
  if (*datatype != MIDatatype)
      MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  MoveInfo* l = static_cast<MoveInfo*>(in);
  MoveInfo* r = static_cast<MoveInfo*>(inout);
  for (int i=0; i < *len; ++i)
  {
      if (l[i].depth >= r[i].depth && l[i].score >= r[i].score)
          r[i] = l[i];
  }
}

void init() {
  int thread_support;
  constexpr std::array<int, 6> TTblocklens = {1, 1, 1, 1, 1, 1};
  const std::array<MPI_Aint, 6> TTdisps = {offsetof(TTEntry, key16),
                                           offsetof(TTEntry, move16),
                                           offsetof(TTEntry, value16),
                                           offsetof(TTEntry, eval16),
                                           offsetof(TTEntry, genBound8),
                                           offsetof(TTEntry, depth8)};
  const std::array<MPI_Datatype, 6> TTtypes = {MPI_UINT16_T,
                                               MPI_UINT16_T,
                                               MPI_INT16_T,
                                               MPI_INT16_T,
                                               MPI_UINT8_T,
                                               MPI_INT8_T};
  const std::array<MPI_Aint, 3> MIdisps = {offsetof(MoveInfo, depth),
                                           offsetof(MoveInfo, score),
                                           offsetof(MoveInfo, rank)};

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

  MPI_Type_create_struct(6, TTblocklens.data(), TTdisps.data(), TTtypes.data(),
                         &TTEntryDatatype);
  MPI_Type_commit(&TTEntryDatatype);

  MPI_Type_create_hindexed_block(3, 1, MIdisps.data(), MPI_INT, &MIDatatype);
  MPI_Type_commit(&MIDatatype);
  MPI_Op_create(BestMove, false, &BestMoveOp);

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
  else {
      while (true) {
          int flag;
          MPI_Test(&reqInput, &flag, MPI_STATUS_IGNORE);
          if (flag)
              break;
          else {
              std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
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
  if (is_root()) {
      if (!stop_signal && Threads.stop) {
          // Signal the cluster about stopping
          stop_signal = true;
          MPI_Ibarrier(StopComm, &reqStop);
          MPI_Wait(&reqStop, MPI_STATUS_IGNORE);
      }
  }
  else {
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
          Key k, Value v, Bound b, Depth d, Move m, Value ev, uint8_t g) {
  tte->save(k, v, b, d, m, ev, g);
  // Try to add to thread's send buffer
  {
      std::lock_guard<Mutex> lk(thread->ttBuffer.mutex);
      thread->ttBuffer.buffer.replace(*tte);
  }

  // Communicate on main search thread
  if (thread == Threads.main()) {
      static MPI_Request req = MPI_REQUEST_NULL;
      static TTSendBuffer<TTSendBufferSize> send_buff = {};
      int flag;
      bool found;
      TTEntry* replace_tte;

      // Test communication status
      MPI_Test(&req, &flag, MPI_STATUS_IGNORE);

      // Current communication is complete
      if (flag) {
          // Save all recieved entries
          for (auto&& e : TTBuff) {
              replace_tte = TT.probe(e.key(), found);
              replace_tte->save(e.key(), e.value(), e.bound(), e.depth(),
                                e.move(), e.eval(), e.gen());
          }

          // Reset send buffer
          send_buff = {};

          // Build up new send buffer: best 16 found across all threads
          for (auto&& th : Threads) {
              std::lock_guard<Mutex> lk(th->ttBuffer.mutex);
              for (auto&& e : th->ttBuffer.buffer)
                  send_buff.replace(e);
              // Reset thread's send buffer
              th->ttBuffer.buffer = {};
          }

          // Start next communication
          MPI_Iallgather(send_buff.data(), send_buff.size(), TTEntryDatatype,
                         TTBuff.data(), TTSendBufferSize, TTEntryDatatype,
                         TTComm, &req);
      }
  }
}

void reduce_moves(MoveInfo& mi) {
  MPI_Allreduce(MPI_IN_PLACE, &mi, 1, MIDatatype, BestMoveOp, MoveComm);
}

}

#endif // USE_MPI
