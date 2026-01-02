/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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
    #include <map>
    #include <mpi.h>
    #include <string>
    #include <vector>

    #include "cluster.h"
    #include "thread.h"
    #include "timeman.h"
    #include "tt.h"
    #include "search.h"

namespace Stockfish {
namespace Distributed {

// Total number of ranks and rank within the communicator
static int world_rank = MPI_PROC_NULL;
static int world_size = 0;

// Signals between ranks exchange basic info using a dedicated communicator
static MPI_Comm    signalsComm        = MPI_COMM_NULL;
static MPI_Request reqSignals         = MPI_REQUEST_NULL;
static uint64_t    signalsCallCounter = 0;

// Signals are the number of nodes searched, stop, table base hits, transposition table saves
enum Signals : int {
    SIG_NODES = 0,
    SIG_STOP  = 1,
    SIG_TB    = 2,
    SIG_TTS   = 3,
    SIG_NB    = 4
};
static uint64_t signalsSend[SIG_NB] = {};
static uint64_t signalsRecv[SIG_NB] = {};
static uint64_t nodesSearchedOthers = 0;
static uint64_t tbHitsOthers        = 0;
static uint64_t TTsavesOthers       = 0;
static uint64_t stopSignalsPosted   = 0;

// The UCI threads of each rank exchange use a dedicated communicator
static MPI_Comm InputComm = MPI_COMM_NULL;

// bestMove requires MoveInfo communicators and data types
static MPI_Comm     MoveComm   = MPI_COMM_NULL;
static MPI_Datatype MIDatatype = MPI_DATATYPE_NULL;

// TT entries are communicated with a dedicated communicator.
// The receive buffer is used to gather information from all ranks.
// THe TTCacheCounter tracks the number of local elements that are ready to be sent.
static MPI_Comm                                 TTComm = MPI_COMM_NULL;
static std::array<std::vector<KeyedTTEntry>, 2> TTSendRecvBuffs;
static std::array<MPI_Request, 2> reqsTTSendRecv = {MPI_REQUEST_NULL, MPI_REQUEST_NULL};
static uint64_t                   sendRecvPosted = 0;
static std::atomic<uint64_t>      TTCacheCounter = {};

/// Initialize MPI and associated data types. Note that the MPI library must be configured
/// to support MPI_THREAD_MULTIPLE, since multiple threads access MPI simultaneously.
void init() {

    int thread_support;
    MPI_Init_thread(nullptr, nullptr, MPI_THREAD_MULTIPLE, &thread_support);
    if (thread_support < MPI_THREAD_MULTIPLE)
    {
        std::cerr << "Stockfish requires support for MPI_THREAD_MULTIPLE." << std::endl;
        std::exit(EXIT_FAILURE);
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    const std::array<MPI_Aint, 5> MIdisps = {offsetof(MoveInfo, move), offsetof(MoveInfo, ponder),
                                             offsetof(MoveInfo, depth), offsetof(MoveInfo, score),
                                             offsetof(MoveInfo, rank)};
    MPI_Type_create_hindexed_block(5, 1, MIdisps.data(), MPI_INT, &MIDatatype);
    MPI_Type_commit(&MIDatatype);

    MPI_Comm_dup(MPI_COMM_WORLD, &InputComm);
    MPI_Comm_dup(MPI_COMM_WORLD, &TTComm);
    MPI_Comm_dup(MPI_COMM_WORLD, &MoveComm);
    MPI_Comm_dup(MPI_COMM_WORLD, &signalsComm);
}

/// Finalize MPI and free the associated data types.
void finalize() {

    MPI_Type_free(&MIDatatype);

    MPI_Comm_free(&InputComm);
    MPI_Comm_free(&TTComm);
    MPI_Comm_free(&MoveComm);
    MPI_Comm_free(&signalsComm);

    MPI_Finalize();
}

/// Return the total number of ranks
int size() { return world_size; }

/// Return the rank (index) of the process
int rank() { return world_rank; }

/// The receive buffer depends on the number of MPI ranks and threads, resize as needed
void ttSendRecvBuff_resize(size_t nThreads) {

    for (int i : {0, 1})
    {
        TTSendRecvBuffs[i].resize(TTCacheSize * world_size * nThreads);
        std::fill(TTSendRecvBuffs[i].begin(), TTSendRecvBuffs[i].end(), KeyedTTEntry());
    }
}

/// As input is only received by the root (rank 0) of the cluster, this input must be relayed
/// to the UCI threads of all ranks, in order to setup the position, etc. We do this with a
/// dedicated getline implementation, where the root broadcasts to all other ranks the received
/// information.
bool getline(std::istream& input, std::string& str) {

    int               size;
    std::vector<char> vec;
    int               state;

    if (is_root())
    {
        state = static_cast<bool>(std::getline(input, str));
        vec.assign(str.begin(), str.end());
        size = vec.size();
    }

    // Some MPI implementations use busy-wait polling, while we need yielding as otherwise
    // the UCI thread on the non-root ranks would be consuming resources.
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

    // Broadcast received string
    if (!is_root())
        vec.resize(size);
    MPI_Bcast(vec.data(), size, MPI_CHAR, 0, InputComm);
    if (!is_root())
        str.assign(vec.begin(), vec.end());
    MPI_Bcast(&state, 1, MPI_INT, 0, InputComm);

    return state;
}

/// Sending part of the signal communication loop
namespace {
void signals_send(const ThreadPool& threads) {

    signalsSend[SIG_NODES] = threads.nodes_searched();
    signalsSend[SIG_TB]    = threads.tb_hits();
    signalsSend[SIG_TTS]   = threads.TT_saves();
    signalsSend[SIG_STOP]  = threads.stop;
    MPI_Iallreduce(signalsSend, signalsRecv, SIG_NB, MPI_UINT64_T, MPI_SUM, signalsComm,
                   &reqSignals);
    ++signalsCallCounter;
}


/// Processing part of the signal communication loop.
/// For some counters (e.g. nodes) we only keep their sum on the other nodes
/// allowing to add local counters at any time for more fine grained process,
/// which is useful to indicate progress during early iterations, and to have
/// node counts that exactly match the non-MPI code in the single rank case.
/// This call also propagates the stop signal between ranks.
void signals_process(ThreadPool& threads) {

    nodesSearchedOthers = signalsRecv[SIG_NODES] - signalsSend[SIG_NODES];
    tbHitsOthers        = signalsRecv[SIG_TB] - signalsSend[SIG_TB];
    TTsavesOthers       = signalsRecv[SIG_TTS] - signalsSend[SIG_TTS];
    stopSignalsPosted   = signalsRecv[SIG_STOP];
    if (signalsRecv[SIG_STOP] > 0)
        threads.stop = true;
}

void sendrecv_post() {

    ++sendRecvPosted;
    MPI_Irecv(TTSendRecvBuffs[sendRecvPosted % 2].data(),
              TTSendRecvBuffs[sendRecvPosted % 2].size() * sizeof(KeyedTTEntry), MPI_BYTE,
              (rank() + size() - 1) % size(), 42, TTComm, &reqsTTSendRecv[0]);
    MPI_Isend(TTSendRecvBuffs[(sendRecvPosted + 1) % 2].data(),
              TTSendRecvBuffs[(sendRecvPosted + 1) % 2].size() * sizeof(KeyedTTEntry), MPI_BYTE,
              (rank() + 1) % size(), 42, TTComm, &reqsTTSendRecv[1]);
}
}

/// During search, most message passing is asynchronous, but at the end of
/// search it makes sense to bring them to a common, finalized state.
void signals_sync(ThreadPool& threads) {

    while (stopSignalsPosted < uint64_t(size()))
        signals_poll(threads);

    // Finalize outstanding messages of the signal loops.
    // We might have issued one call less than needed on some ranks.
    uint64_t globalCounter;
    MPI_Allreduce(&signalsCallCounter, &globalCounter, 1, MPI_UINT64_T, MPI_MAX, MoveComm);
    if (signalsCallCounter < globalCounter)
    {
        MPI_Wait(&reqSignals, MPI_STATUS_IGNORE);
        signals_send(threads);
    }
    assert(signalsCallCounter == globalCounter);
    MPI_Wait(&reqSignals, MPI_STATUS_IGNORE);
    signals_process(threads);

    // Finalize outstanding messages in the sendRecv loop
    MPI_Allreduce(&sendRecvPosted, &globalCounter, 1, MPI_UINT64_T, MPI_MAX, MoveComm);
    while (sendRecvPosted < globalCounter)
    {
        MPI_Waitall(reqsTTSendRecv.size(), reqsTTSendRecv.data(), MPI_STATUSES_IGNORE);
        sendrecv_post();
    }
    assert(sendRecvPosted == globalCounter);
    MPI_Waitall(reqsTTSendRecv.size(), reqsTTSendRecv.data(), MPI_STATUSES_IGNORE);
}

/// Initialize signal counters to zero.
void signals_init() {

    stopSignalsPosted = tbHitsOthers = TTsavesOthers = nodesSearchedOthers = 0;

    signalsSend[SIG_NODES] = signalsRecv[SIG_NODES] = 0;
    signalsSend[SIG_TB] = signalsRecv[SIG_TB] = 0;
    signalsSend[SIG_TTS] = signalsRecv[SIG_TTS] = 0;
    signalsSend[SIG_STOP] = signalsRecv[SIG_STOP] = 0;
}

/// Poll the signal loop, and start next round as needed.
void signals_poll(ThreadPool& threads) {

    int flag;
    MPI_Test(&reqSignals, &flag, MPI_STATUS_IGNORE);
    if (flag)
    {
        signals_process(threads);
        signals_send(threads);
    }
}

/// Provide basic info related the cluster performance, in particular, the number of signals send,
/// signals per sounds (sps), the number of gathers, the number of positions gathered (per node and per second, gpps)
/// The number of TT saves and TT saves per second. If gpps equals approximately TTSavesps the gather loop has enough bandwidth.
void cluster_info(const ThreadPool& threads, Depth depth, TimePoint elapsed) {

    // TimePoint elapsed = Time.elapsed() + 1;
    uint64_t TTSaves = TT_saves(threads);

    sync_cout << "info depth " << depth << " cluster "
              << " signals " << signalsCallCounter << " sps " << signalsCallCounter * 1000 / elapsed
              << " sendRecvs " << sendRecvPosted << " srpps "
              << TTSendRecvBuffs[0].size() * sendRecvPosted * 1000 / elapsed << " TTSaves "
              << TTSaves << " TTSavesps " << TTSaves * 1000 / elapsed << sync_endl;
}

/// When a TT entry is saved, additional steps are taken if the entry is of sufficient depth.
/// If sufficient entries has been collected, a communication is initiated.
/// If a communication has been completed, the received results are saved to the TT.
void save(TranspositionTable& TT,
          ThreadPool&         threads,
          Search::Worker*     thread,
          TTWriter            ttWriter,
          Key                 k,
          Value               v,
          bool                PvHit,
          Bound               b,
          Depth               d,
          Move                m,
          Value               ev,
          uint8_t             generation8) {

    // Standard save to the TT
    ttWriter.write(k, v, PvHit, b, d, m, ev, generation8);

    // If the entry is of sufficient depth to be worth communicating, take action.
    if (d > 3)
    {
        // count the TTsaves to information: this should be relatively similar
        // to the number of entries we can send/recv.
        thread->TTsaves.fetch_add(1, std::memory_order_relaxed);

        // Add to thread's send buffer, the locking here avoids races when the master thread
        // prepares the send buffer.
        {
            std::lock_guard<std::mutex> lk(thread->ttCache.mutex);
            thread->ttCache.buffer.replace(KeyedTTEntry(k, TTData(m, v, ev, d, b, PvHit)));
            ++TTCacheCounter;
        }

        size_t recvBuffPerRankSize = threads.size() * TTCacheSize;

        // Communicate on main search thread, as soon the threads combined have collected
        // sufficient data to fill the send buffers.
        if (thread == threads.main_thread()->worker.get() && TTCacheCounter > recvBuffPerRankSize)
        {
            // Test communication status
            int flag;
            MPI_Testall(reqsTTSendRecv.size(), reqsTTSendRecv.data(), &flag, MPI_STATUSES_IGNORE);

            // Current communication is complete
            if (flag)
            {
                // Save all received entries to TT, and store our TTCaches, ready for the next round of communication
                for (size_t irank = 0; irank < size_t(size()); ++irank)
                {
                    if (irank
                        == size_t(
                          rank()))  // this is our part, fill the part of the buffer for sending
                    {
                        // Copy from the thread caches to the right spot in the buffer
                        size_t i = irank * recvBuffPerRankSize;
                        for (auto&& th : threads)
                        {
                            std::lock_guard<std::mutex> lk(th->worker->ttCache.mutex);

                            for (auto&& e : th->worker->ttCache.buffer)
                                TTSendRecvBuffs[sendRecvPosted % 2][i++] = e;

                            // Reset thread's send buffer
                            th->worker->ttCache.buffer = {};
                        }

                        TTCacheCounter = 0;
                    }
                    else  // process data received from the corresponding rank.
                        for (size_t i = irank * recvBuffPerRankSize;
                             i < (irank + 1) * recvBuffPerRankSize; ++i)
                        {
                            auto&& e = TTSendRecvBuffs[sendRecvPosted % 2][i];
                            auto [ttHit, ttData, ttWriterForRecvd] = TT.probe(e.first);
                            ttWriterForRecvd.write(e.first, e.second.value, e.second.is_pv,
                                                   e.second.bound, e.second.depth, e.second.move,
                                                   e.second.eval, TT.generation());
                        }
                }

                // Start next communication
                sendrecv_post();

                // Force check of time on the next occasion, the above actions might have taken some time.
                thread->main_manager()->callsCnt = 0;
            }
        }
    }
}

/// Picks the bestMove across ranks, and send the associated info and PV to the root of the cluster.
/// Note that this bestMove and PV must be output by the root, the guarantee proper ordering of output.
/// TODO update to the scheme in master.. can this use aggregation of votes?
void pick_moves(MoveInfo& mi, std::vector<std::vector<char>>& serializedInfo) {

    MoveInfo* pMoveInfo = NULL;
    if (is_root())
    {
        pMoveInfo = (MoveInfo*) malloc(sizeof(MoveInfo) * size());
    }
    MPI_Gather(&mi, 1, MIDatatype, pMoveInfo, 1, MIDatatype, 0, MoveComm);

    if (is_root())
    {
        std::map<int, int> votes;
        int                minScore = pMoveInfo[0].score;
        for (int i = 0; i < size(); ++i)
        {
            minScore                 = std::min(minScore, pMoveInfo[i].score);
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
                mi       = pMoveInfo[i];
            }
        }
        free(pMoveInfo);
    }

    // Send around the final result
    MPI_Bcast(&mi, 1, MIDatatype, 0, MoveComm);

    // Send PV line to root as needed
    if (mi.rank != 0 && mi.rank == rank())
    {
        int numLines = serializedInfo.size();
        MPI_Send(&numLines, 1, MPI_INT, 0, 42, MoveComm);

        for (const auto& serializedInfoOne : serializedInfo)
        {
            int size;
            size = serializedInfoOne.size();
            MPI_Send(&size, 1, MPI_INT, 0, 42, MoveComm);
            MPI_Send(serializedInfoOne.data(), size, MPI_CHAR, 0, 42, MoveComm);
        }
    }
    if (mi.rank != 0 && is_root())
    {
        serializedInfo.clear();

        int numLines;
        MPI_Recv(&numLines, 1, MPI_INT, mi.rank, 42, MoveComm, MPI_STATUS_IGNORE);

        for (int i = 0; i < numLines; ++i)
        {
            int               size;
            std::vector<char> vec;
            MPI_Recv(&size, 1, MPI_INT, mi.rank, 42, MoveComm, MPI_STATUS_IGNORE);
            vec.resize(size);
            MPI_Recv(vec.data(), size, MPI_CHAR, mi.rank, 42, MoveComm, MPI_STATUS_IGNORE);
            serializedInfo.push_back(std::move(vec));
        }
    }
}

/// Return nodes searched (lazily updated cluster wide in the signal loop)
uint64_t nodes_searched(const ThreadPool& threads) {
    return nodesSearchedOthers + threads.nodes_searched();
}

/// Return table base hits (lazily updated cluster wide in the signal loop)
uint64_t tb_hits(const ThreadPool& threads) { return tbHitsOthers + threads.tb_hits(); }

/// Return the number of saves to the TT buffers, (lazily updated cluster wide in the signal loop)
uint64_t TT_saves(const ThreadPool& threads) { return TTsavesOthers + threads.TT_saves(); }


}
}

#else

    #include "cluster.h"
    #include "thread.h"

namespace Stockfish {
namespace Distributed {

uint64_t nodes_searched(const ThreadPool& threads) { return threads.nodes_searched(); }

uint64_t tb_hits(const ThreadPool& threads) { return threads.tb_hits(); }

uint64_t TT_saves(const ThreadPool& threads) { return threads.TT_saves(); }

}
}

#endif  // USE_MPI
