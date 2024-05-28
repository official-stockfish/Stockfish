/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#ifndef NUMA_H_INCLUDED
#define NUMA_H_INCLUDED

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// We support linux very well, but we explicitly do NOT support Android, partially because
// there are potential issues with `lscpu`, `popen` availability, and partially because
// there's no NUMA environments running Android and there probably won't be.
#if defined(__linux__) && !defined(__ANDROID__)
    #if !defined(_GNU_SOURCE)
        #define _GNU_SOURCE
    #endif
    #include <sched.h>
#elif defined(_WIN32)

// On Windows each processor group can have up to 64 processors.
// https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups
static constexpr size_t WIN_PROCESSOR_GROUP_SIZE = 64;

    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>
    #if defined small
        #undef small
    #endif

// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadselectedcpusetmasks
using SetThreadSelectedCpuSetMasks_t = BOOL (*)(HANDLE, PGROUP_AFFINITY, USHORT);

// https://learn.microsoft.com/en-us/windows/win32/api/processtopologyapi/nf-processtopologyapi-setthreadgroupaffinity
using SetThreadGroupAffinity_t = BOOL (*)(HANDLE, const GROUP_AFFINITY*, PGROUP_AFFINITY);

#endif

#include "misc.h"

namespace Stockfish {

using CpuIndex  = size_t;
using NumaIndex = size_t;

inline const CpuIndex SYSTEM_THREADS_NB =
  std::max<CpuIndex>(1, std::thread::hardware_concurrency());

// We want to abstract the purpose of storing the numa node index somewhat.
// Whoever is using this does not need to know the specifics of the replication
// machinery to be able to access NUMA replicated memory.
class NumaReplicatedAccessToken {
   public:
    NumaReplicatedAccessToken() :
        n(0) {}

    explicit NumaReplicatedAccessToken(NumaIndex idx) :
        n(idx) {}

    NumaIndex get_numa_index() const { return n; }

   private:
    NumaIndex n;
};

// Designed as immutable, because there is no good reason to alter an already existing config
// in a way that doesn't require recreating it completely, and it would be complex and expensive
// to maintain class invariants.
// The CPU (processor) numbers always correspond to the actual numbering used by the system.
//     NOTE: the numbering is only valid within the process, as for example on Windows
//           every process gets a "virtualized" set of processors that respects the current affinity
// The NUMA node numbers MAY NOT correspond to the system's numbering of the NUMA nodes.
// In particular, empty nodes may be removed, or the user may create custom nodes.
// It is guaranteed that NUMA nodes are NOT empty, i.e. every node exposed by NumaConfig
// has at least one processor assigned.
//
// Until Stockfish doesn't support exceptions all places where an exception should be thrown
// are replaced by std::exit.
class NumaConfig {
   public:
    NumaConfig() :
        highestCpuIndex(0),
        customAffinity(false) {
        const auto numCpus = SYSTEM_THREADS_NB;
        add_cpu_range_to_node(NumaIndex{0}, CpuIndex{0}, numCpus - 1);
    }

    static std::set<CpuIndex> get_process_affinity() {
        std::set<CpuIndex> cpus;

        // For unsupported systems, or in case of a soft error, we may assume all processors
        // are available for use.
        [[maybe_unused]] auto set_to_all_cpus = [&]() {
            for (CpuIndex c = 0; c < SYSTEM_THREADS_NB; ++c)
                cpus.insert(c);
        };

#if defined(__linux__) && !defined(__ANDROID__)

        // cpu_set_t by default holds 1024 entries. This may not be enough soon,
        // but there is no easy way to determine how many threads there actually is.
        // In this case we just choose a reasonable upper bound.
        static constexpr CpuIndex MaxNumCpus = 1024 * 64;

        cpu_set_t* mask = CPU_ALLOC(MaxNumCpus);
        if (mask == nullptr)
            std::exit(EXIT_FAILURE);

        const size_t masksize = CPU_ALLOC_SIZE(MaxNumCpus);

        CPU_ZERO_S(masksize, mask);

        const int status = sched_getaffinity(0, masksize, mask);

        if (status != 0)
        {
            CPU_FREE(mask);
            std::exit(EXIT_FAILURE);
        }

        for (CpuIndex c = 0; c < MaxNumCpus; ++c)
            if (CPU_ISSET_S(c, masksize, mask))
                cpus.insert(c);

        CPU_FREE(mask);

#elif defined(_WIN32)

        // Windows is problematic and weird due to multiple ways of setting affinity, processor groups,
        // and behaviour changes between versions. It's unclear if we can support this feature
        // on Windows in the same way we do on Linux.
        // Apparently when affinity is set via either start /affinity or msys2 taskset
        // the function GetNumaProcessorNodeEx completely disregards the processors that we do not
        // have affinity more. Moreover, the indices are shifted to start from 0, indicating that Windows
        // is providing a whole new mapping of processors to this process. This is problematic in some cases
        // but it at least allows us to [probably] support this affinity restriction feature by default.
        // So overall, Windows appears to "virtualize" a set of processors and processor groups for every
        // process. It's unclear if this assignment can change while the process is running.
        // std::thread::hardware_concurrency() returns the number of processors that's consistent
        // with GetNumaProcessorNodeEx, so we can just add all of them.

        set_to_all_cpus();

#else

        // For other systems we assume the process is allowed to execute on all processors.
        set_to_all_cpus();

#endif

        return cpus;
    }

    // This function queries the system for the mapping of processors to NUMA nodes.
    // On Linux we utilize `lscpu` to avoid libnuma.
    // On Windows we utilize GetNumaProcessorNodeEx, which has its quirks, see
    // comment for Windows implementation of get_process_affinity
    static NumaConfig from_system(bool respectProcessAffinity = true) {
        NumaConfig cfg = empty();

        std::set<CpuIndex> allowedCpus;

        if (respectProcessAffinity)
            allowedCpus = get_process_affinity();
        else
        {
            for (CpuIndex c = 0; c < SYSTEM_THREADS_NB; ++c)
                allowedCpus.insert(c);
        }

        auto is_cpu_allowed = [&](CpuIndex c) { return allowedCpus.count(c) == 1; };

#if defined(__linux__) && !defined(__ANDROID__)

        // On Linux things are straightforward, since there's no processor groups and
        // any thread can be scheduled on all processors.
        // This command produces output in the following form
        // CPU NODE
        //   0    0
        //   1    0
        //   2    1
        //   3    1
        //
        // On some systems it may use '-' to signify no NUMA node, in which case we assume it's in node 0.
        auto lscpuOpt = get_system_command_output("lscpu -e=cpu,node");
        if (lscpuOpt.has_value())
        {

            std::istringstream ss(*lscpuOpt);

            // skip the list header
            ss.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            while (true)
            {
                CpuIndex  c;
                NumaIndex n;

                ss >> c;

                if (!ss)
                    break;

                ss >> n;

                if (!ss)
                {
                    ss.clear();
                    std::string dummy;
                    ss >> dummy;
                    n = 0;
                }

                if (is_cpu_allowed(c))
                    cfg.add_cpu_to_node(n, c);
            }
        }
        else
        {
            for (CpuIndex c = 0; c < SYSTEM_THREADS_NB; ++c)
                if (is_cpu_allowed(c))
                    cfg.add_cpu_to_node(NumaIndex{0}, c);
        }

#elif defined(_WIN32)

        // Since Windows 11 and Windows Server 2022 thread affinities can span
        // processor groups and can be set as such by a new WinAPI function.
        static const bool CanAffinitySpanProcessorGroups = []() {
            HMODULE k32                            = GetModuleHandle(TEXT("Kernel32.dll"));
            auto    SetThreadSelectedCpuSetMasks_f = SetThreadSelectedCpuSetMasks_t(
              (void (*)()) GetProcAddress(k32, "SetThreadSelectedCpuSetMasks"));
            return SetThreadSelectedCpuSetMasks_f != nullptr;
        }();

        WORD numProcGroups = GetActiveProcessorGroupCount();
        for (WORD procGroup = 0; procGroup < numProcGroups; ++procGroup)
        {
            for (BYTE number = 0; number < WIN_PROCESSOR_GROUP_SIZE; ++number)
            {
                PROCESSOR_NUMBER procnum;
                procnum.Group    = procGroup;
                procnum.Number   = number;
                procnum.Reserved = 0;
                USHORT nodeNumber;

                // When start /affinity or taskset was used to run this process with restricted affinity
                // GetNumaProcessorNodeEx will NOT correspond to the system's processor setup, instead
                // it appears to follow a completely new processor assignment, made specifically for this process,
                // in which processors that this process has affinity for are remapped, and only those are remapped,
                // to form a new set of processors. In other words, we can only get processors
                // which we have affinity for this way. This means that the behaviour for
                // `respectProcessAffinity == false` may be unexpected when affinity is set from outside,
                // while the behaviour for `respectProcessAffinity == true` is given by default.
                const BOOL     status = GetNumaProcessorNodeEx(&procnum, &nodeNumber);
                const CpuIndex c      = static_cast<CpuIndex>(procGroup) * WIN_PROCESSOR_GROUP_SIZE
                                 + static_cast<CpuIndex>(number);
                if (status != 0 && nodeNumber != std::numeric_limits<USHORT>::max()
                    && is_cpu_allowed(c))
                {
                    cfg.add_cpu_to_node(nodeNumber, c);
                }
            }
        }

        // Split the NUMA nodes to be contained within a group if necessary.
        // This is needed between Windows 10 Build 20348 and Windows 11, because
        // the new NUMA allocation behaviour was introduced while there was
        // still no way to set thread affinity spanning multiple processor groups.
        // See https://learn.microsoft.com/en-us/windows/win32/procthread/numa-support
        if (!CanAffinitySpanProcessorGroups)
        {
            NumaConfig splitCfg = empty();

            NumaIndex splitNodeIndex = 0;
            for (const auto& cpus : cfg.nodes)
            {
                if (cpus.empty())
                    continue;

                size_t lastProcGroupIndex = *(cpus.begin()) / WIN_PROCESSOR_GROUP_SIZE;
                for (CpuIndex c : cpus)
                {
                    const size_t procGroupIndex = c / WIN_PROCESSOR_GROUP_SIZE;
                    if (procGroupIndex != lastProcGroupIndex)
                    {
                        splitNodeIndex += 1;
                        lastProcGroupIndex = procGroupIndex;
                    }
                    splitCfg.add_cpu_to_node(splitNodeIndex, c);
                }
                splitNodeIndex += 1;
            }

            cfg = std::move(splitCfg);
        }

#else

        // Fallback for unsupported systems.
        for (CpuIndex c = 0; c < SYSTEM_THREADS_NB; ++c)
            if (is_cpu_allowed(c))
                cfg.add_cpu_to_node(NumaIndex{0}, c);

#endif

        // We have to ensure no empty NUMA nodes persist.
        cfg.remove_empty_numa_nodes();

        return cfg;
    }

    // ':'-separated numa nodes
    // ','-separated cpu indices
    // supports "first-last" range syntax for cpu indices
    // For example "0-15,128-143:16-31,144-159:32-47,160-175:48-63,176-191"
    static NumaConfig from_string(const std::string& s) {
        NumaConfig cfg = empty();

        NumaIndex n = 0;
        for (auto&& nodeStr : split(s, ":"))
        {
            bool addedAnyCpuInThisNode = false;

            for (const std::string& cpuStr : split(nodeStr, ","))
            {
                if (cpuStr.empty())
                    continue;

                auto parts = split(cpuStr, "-");
                if (parts.size() == 1)
                {
                    const CpuIndex c = CpuIndex{str_to_size_t(parts[0])};
                    if (!cfg.add_cpu_to_node(n, c))
                        std::exit(EXIT_FAILURE);
                }
                else if (parts.size() == 2)
                {
                    const CpuIndex cfirst = CpuIndex{str_to_size_t(parts[0])};
                    const CpuIndex clast  = CpuIndex{str_to_size_t(parts[1])};

                    if (!cfg.add_cpu_range_to_node(n, cfirst, clast))
                        std::exit(EXIT_FAILURE);
                }
                else
                {
                    std::exit(EXIT_FAILURE);
                }

                addedAnyCpuInThisNode = true;
            }

            if (addedAnyCpuInThisNode)
                n += 1;
        }

        cfg.customAffinity = true;

        return cfg;
    }

    NumaConfig(const NumaConfig&)            = delete;
    NumaConfig(NumaConfig&&)                 = default;
    NumaConfig& operator=(const NumaConfig&) = delete;
    NumaConfig& operator=(NumaConfig&&)      = default;

    bool is_cpu_assigned(CpuIndex n) const { return nodeByCpu.count(n) == 1; }

    NumaIndex num_numa_nodes() const { return nodes.size(); }

    CpuIndex num_cpus_in_numa_node(NumaIndex n) const {
        assert(n < nodes.size());
        return nodes[n].size();
    }

    CpuIndex num_cpus() const { return nodeByCpu.size(); }

    bool requires_memory_replication() const { return customAffinity || nodes.size() > 1; }

    std::string to_string() const {
        std::string str;

        bool isFirstNode = true;
        for (auto&& cpus : nodes)
        {
            if (!isFirstNode)
                str += ":";

            bool isFirstSet = true;
            auto rangeStart = cpus.begin();
            for (auto it = cpus.begin(); it != cpus.end(); ++it)
            {
                auto next = std::next(it);
                if (next == cpus.end() || *next != *it + 1)
                {
                    // cpus[i] is at the end of the range (may be of size 1)
                    if (!isFirstSet)
                        str += ",";

                    const CpuIndex last = *it;

                    if (it != rangeStart)
                    {
                        const CpuIndex first = *rangeStart;

                        str += std::to_string(first);
                        str += "-";
                        str += std::to_string(last);
                    }
                    else
                        str += std::to_string(last);

                    rangeStart = next;
                    isFirstSet = false;
                }
            }

            isFirstNode = false;
        }

        return str;
    }

    bool suggests_binding_threads(CpuIndex numThreads) const {
        // If we can reasonably determine that the threads can't be contained
        // by the OS within the first NUMA node then we advise distributing
        // and binding threads. When the threads are not bound we can only use
        // NUMA memory replicated objects from the first node, so when the OS
        // has to schedule on other nodes we lose performance.
        // We also suggest binding if there's enough threads to distribute among nodes
        // with minimal disparity.
        // We try to ignore small nodes, in particular the empty ones.

        // If the affinity set by the user does not match the affinity given by the OS
        // then binding is necessary to ensure the threads are running on correct processors.
        if (customAffinity)
            return true;

        // We obviously can't distribute a single thread, so a single thread should never be bound.
        if (numThreads <= 1)
            return false;

        size_t largestNodeSize = 0;
        for (auto&& cpus : nodes)
            if (cpus.size() > largestNodeSize)
                largestNodeSize = cpus.size();

        auto is_node_small = [largestNodeSize](const std::set<CpuIndex>& node) {
            static constexpr double SmallNodeThreshold = 0.6;
            return static_cast<double>(node.size()) / static_cast<double>(largestNodeSize)
                <= SmallNodeThreshold;
        };

        size_t numNotSmallNodes = 0;
        for (auto&& cpus : nodes)
            if (!is_node_small(cpus))
                numNotSmallNodes += 1;

        return (numThreads > largestNodeSize / 2 || numThreads >= numNotSmallNodes * 4)
            && nodes.size() > 1;
    }

    std::vector<NumaIndex> distribute_threads_among_numa_nodes(CpuIndex numThreads) const {
        std::vector<NumaIndex> ns;

        if (nodes.size() == 1)
        {
            // special case for when there's no NUMA nodes
            // doesn't buy us much, but let's keep the default path simple
            ns.resize(numThreads, NumaIndex{0});
        }
        else
        {
            std::vector<size_t> occupation(nodes.size(), 0);
            for (CpuIndex c = 0; c < numThreads; ++c)
            {
                NumaIndex bestNode{0};
                float     bestNodeFill = std::numeric_limits<float>::max();
                for (NumaIndex n = 0; n < nodes.size(); ++n)
                {
                    float fill =
                      static_cast<float>(occupation[n] + 1) / static_cast<float>(nodes[n].size());
                    // NOTE: Do we want to perhaps fill the first available node up to 50% first before considering other nodes?
                    //       Probably not, because it would interfere with running multiple instances. We basically shouldn't
                    //       favor any particular node.
                    if (fill < bestNodeFill)
                    {
                        bestNode     = n;
                        bestNodeFill = fill;
                    }
                }
                ns.emplace_back(bestNode);
                occupation[bestNode] += 1;
            }
        }

        return ns;
    }

    NumaReplicatedAccessToken bind_current_thread_to_numa_node(NumaIndex n) const {
        if (n >= nodes.size() || nodes[n].size() == 0)
            std::exit(EXIT_FAILURE);

#if defined(__linux__) && !defined(__ANDROID__)

        cpu_set_t* mask = CPU_ALLOC(highestCpuIndex + 1);
        if (mask == nullptr)
            std::exit(EXIT_FAILURE);

        const size_t masksize = CPU_ALLOC_SIZE(highestCpuIndex + 1);

        CPU_ZERO_S(masksize, mask);

        for (CpuIndex c : nodes[n])
            CPU_SET_S(c, masksize, mask);

        const int status = sched_setaffinity(0, masksize, mask);

        CPU_FREE(mask);

        if (status != 0)
            std::exit(EXIT_FAILURE);

        // We yield this thread just to be sure it gets rescheduled.
        // This is defensive, allowed because this code is not performance critical.
        sched_yield();

#elif defined(_WIN32)

        // Requires Windows 11. No good way to set thread affinity spanning processor groups before that.
        HMODULE k32                            = GetModuleHandle(TEXT("Kernel32.dll"));
        auto    SetThreadSelectedCpuSetMasks_f = SetThreadSelectedCpuSetMasks_t(
          (void (*)()) GetProcAddress(k32, "SetThreadSelectedCpuSetMasks"));
        auto SetThreadGroupAffinity_f =
          SetThreadGroupAffinity_t((void (*)()) GetProcAddress(k32, "SetThreadGroupAffinity"));

        if (SetThreadSelectedCpuSetMasks_f != nullptr)
        {
            // Only available on Windows 11 and Windows Server 2022 onwards.
            const USHORT numProcGroups = USHORT(
              ((highestCpuIndex + 1) + WIN_PROCESSOR_GROUP_SIZE - 1) / WIN_PROCESSOR_GROUP_SIZE);
            auto groupAffinities = std::make_unique<GROUP_AFFINITY[]>(numProcGroups);
            std::memset(groupAffinities.get(), 0, sizeof(GROUP_AFFINITY) * numProcGroups);
            for (WORD i = 0; i < numProcGroups; ++i)
                groupAffinities[i].Group = i;

            for (CpuIndex c : nodes[n])
            {
                const size_t procGroupIndex     = c / WIN_PROCESSOR_GROUP_SIZE;
                const size_t idxWithinProcGroup = c % WIN_PROCESSOR_GROUP_SIZE;
                groupAffinities[procGroupIndex].Mask |= KAFFINITY(1) << idxWithinProcGroup;
            }

            HANDLE hThread = GetCurrentThread();

            const BOOL status =
              SetThreadSelectedCpuSetMasks_f(hThread, groupAffinities.get(), numProcGroups);
            if (status == 0)
                std::exit(EXIT_FAILURE);

            // We yield this thread just to be sure it gets rescheduled.
            // This is defensive, allowed because this code is not performance critical.
            SwitchToThread();
        }
        else if (SetThreadGroupAffinity_f != nullptr)
        {
            // On earlier windows version (since windows 7) we can't run a single thread
            // on multiple processor groups, so we need to restrict the group.
            // We assume the group of the first processor listed for this node.
            // Processors from outside this group will not be assigned for this thread.
            // Normally this won't be an issue because windows used to assign NUMA nodes
            // such that they can't span processor groups. However, since Windows 10 Build 20348
            // the behaviour changed, so there's a small window of versions between this and Windows 11
            // that might exhibit problems with not all processors being utilized.
            // We handle this in NumaConfig::from_system by manually splitting the nodes when
            // we detect that there's no function to set affinity spanning processor nodes.
            // This is required because otherwise our thread distribution code may produce
            // suboptimal results.
            // See https://learn.microsoft.com/en-us/windows/win32/procthread/numa-support
            GROUP_AFFINITY affinity;
            std::memset(&affinity, 0, sizeof(GROUP_AFFINITY));
            affinity.Group = static_cast<WORD>(n);
            // We use an ordered set so we're guaranteed to get the smallest cpu number here.
            const size_t forcedProcGroupIndex = *(nodes[n].begin()) / WIN_PROCESSOR_GROUP_SIZE;
            for (CpuIndex c : nodes[n])
            {
                const size_t procGroupIndex     = c / WIN_PROCESSOR_GROUP_SIZE;
                const size_t idxWithinProcGroup = c % WIN_PROCESSOR_GROUP_SIZE;
                // We skip processors that are not in the same proccessor group.
                // If everything was set up correctly this will never be an issue,
                // but we have to account for bad NUMA node specification.
                if (procGroupIndex != forcedProcGroupIndex)
                    continue;

                affinity.Mask |= KAFFINITY(1) << idxWithinProcGroup;
            }

            HANDLE hThread = GetCurrentThread();

            const BOOL status = SetThreadGroupAffinity_f(hThread, &affinity, nullptr);
            if (status == 0)
                std::exit(EXIT_FAILURE);

            // We yield this thread just to be sure it gets rescheduled.
            // This is defensive, allowed because this code is not performance critical.
            SwitchToThread();
        }

#endif

        return NumaReplicatedAccessToken(n);
    }

    template<typename FuncT>
    void execute_on_numa_node(NumaIndex n, FuncT&& f) const {
        std::thread th([this, &f, n]() {
            bind_current_thread_to_numa_node(n);
            std::forward<FuncT>(f)();
        });

        th.join();
    }

   private:
    std::vector<std::set<CpuIndex>> nodes;
    std::map<CpuIndex, NumaIndex>   nodeByCpu;
    CpuIndex                        highestCpuIndex;

    bool customAffinity;

    static NumaConfig empty() { return NumaConfig(EmptyNodeTag{}); }

    struct EmptyNodeTag {};

    NumaConfig(EmptyNodeTag) :
        highestCpuIndex(0),
        customAffinity(false) {}

    void remove_empty_numa_nodes() {
        std::vector<std::set<CpuIndex>> newNodes;
        for (auto&& cpus : nodes)
            if (!cpus.empty())
                newNodes.emplace_back(std::move(cpus));
        nodes = std::move(newNodes);
    }

    // Returns true if successful
    // Returns false if failed, i.e. when the cpu is already present
    //                          strong guarantee, the structure remains unmodified
    bool add_cpu_to_node(NumaIndex n, CpuIndex c) {
        if (is_cpu_assigned(c))
            return false;

        while (nodes.size() <= n)
            nodes.emplace_back();

        nodes[n].insert(c);
        nodeByCpu[c] = n;

        if (c > highestCpuIndex)
            highestCpuIndex = c;

        return true;
    }

    // Returns true if successful
    // Returns false if failed, i.e. when any of the cpus is already present
    //                          strong guarantee, the structure remains unmodified
    bool add_cpu_range_to_node(NumaIndex n, CpuIndex cfirst, CpuIndex clast) {
        for (CpuIndex c = cfirst; c <= clast; ++c)
            if (is_cpu_assigned(c))
                return false;

        while (nodes.size() <= n)
            nodes.emplace_back();

        for (CpuIndex c = cfirst; c <= clast; ++c)
        {
            nodes[n].insert(c);
            nodeByCpu[c] = n;
        }

        if (clast > highestCpuIndex)
            highestCpuIndex = clast;

        return true;
    }
};

class NumaReplicationContext;

// Instances of this class are tracked by the NumaReplicationContext instance
// NumaReplicationContext informs all tracked instances whenever NUMA configuration changes.
class NumaReplicatedBase {
   public:
    NumaReplicatedBase(NumaReplicationContext& ctx);

    NumaReplicatedBase(const NumaReplicatedBase&) = delete;
    NumaReplicatedBase(NumaReplicatedBase&& other) noexcept;

    NumaReplicatedBase& operator=(const NumaReplicatedBase&) = delete;
    NumaReplicatedBase& operator=(NumaReplicatedBase&& other) noexcept;

    virtual void on_numa_config_changed() = 0;
    virtual ~NumaReplicatedBase();

    const NumaConfig& get_numa_config() const;

   private:
    NumaReplicationContext* context;
};

// We force boxing with a unique_ptr. If this becomes an issue due to added indirection we
// may need to add an option for a custom boxing type.
// When the NUMA config changes the value stored at the index 0 is replicated to other nodes.
template<typename T>
class NumaReplicated: public NumaReplicatedBase {
   public:
    using ReplicatorFuncType = std::function<T(const T&)>;

    NumaReplicated(NumaReplicationContext& ctx) :
        NumaReplicatedBase(ctx) {
        replicate_from(T{});
    }

    NumaReplicated(NumaReplicationContext& ctx, T&& source) :
        NumaReplicatedBase(ctx) {
        replicate_from(std::move(source));
    }

    NumaReplicated(const NumaReplicated&) = delete;
    NumaReplicated(NumaReplicated&& other) noexcept :
        NumaReplicatedBase(std::move(other)),
        instances(std::exchange(other.instances, {})) {}

    NumaReplicated& operator=(const NumaReplicated&) = delete;
    NumaReplicated& operator=(NumaReplicated&& other) noexcept {
        NumaReplicatedBase::operator=(*this, std::move(other));
        instances = std::exchange(other.instances, {});

        return *this;
    }

    NumaReplicated& operator=(T&& source) {
        replicate_from(std::move(source));

        return *this;
    }

    ~NumaReplicated() override = default;

    const T& operator[](NumaReplicatedAccessToken token) const {
        assert(token.get_numa_index() < instances.size());
        return *(instances[token.get_numa_index()]);
    }

    const T& operator*() const { return *(instances[0]); }

    const T* operator->() const { return instances[0].get(); }

    template<typename FuncT>
    void modify_and_replicate(FuncT&& f) {
        auto source = std::move(instances[0]);
        std::forward<FuncT>(f)(*source);
        replicate_from(std::move(*source));
    }

    void on_numa_config_changed() override {
        // Use the first one as the source. It doesn't matter which one we use, because they all must
        // be identical, but the first one is guaranteed to exist.
        auto source = std::move(instances[0]);
        replicate_from(std::move(*source));
    }

   private:
    std::vector<std::unique_ptr<T>> instances;

    void replicate_from(T&& source) {
        instances.clear();

        const NumaConfig& cfg = get_numa_config();
        if (cfg.requires_memory_replication())
        {
            for (NumaIndex n = 0; n < cfg.num_numa_nodes(); ++n)
            {
                cfg.execute_on_numa_node(
                  n, [this, &source]() { instances.emplace_back(std::make_unique<T>(source)); });
            }
        }
        else
        {
            assert(cfg.num_numa_nodes() == 1);
            // We take advantage of the fact that replication is not required
            // and reuse the source value, avoiding one copy operation.
            instances.emplace_back(std::make_unique<T>(std::move(source)));
        }
    }
};

class NumaReplicationContext {
   public:
    NumaReplicationContext(NumaConfig&& cfg) :
        config(std::move(cfg)) {}

    NumaReplicationContext(const NumaReplicationContext&) = delete;
    NumaReplicationContext(NumaReplicationContext&&)      = delete;

    NumaReplicationContext& operator=(const NumaReplicationContext&) = delete;
    NumaReplicationContext& operator=(NumaReplicationContext&&)      = delete;

    ~NumaReplicationContext() {
        // The context must outlive replicated objects
        if (!trackedReplicatedObjects.empty())
            std::exit(EXIT_FAILURE);
    }

    void attach(NumaReplicatedBase* obj) {
        assert(trackedReplicatedObjects.count(obj) == 0);
        trackedReplicatedObjects.insert(obj);
    }

    void detach(NumaReplicatedBase* obj) {
        assert(trackedReplicatedObjects.count(obj) == 1);
        trackedReplicatedObjects.erase(obj);
    }

    // oldObj may be invalid at this point
    void move_attached([[maybe_unused]] NumaReplicatedBase* oldObj, NumaReplicatedBase* newObj) {
        assert(trackedReplicatedObjects.count(oldObj) == 1);
        assert(trackedReplicatedObjects.count(newObj) == 0);
        trackedReplicatedObjects.erase(oldObj);
        trackedReplicatedObjects.insert(newObj);
    }

    void set_numa_config(NumaConfig&& cfg) {
        config = std::move(cfg);
        for (auto&& obj : trackedReplicatedObjects)
            obj->on_numa_config_changed();
    }

    const NumaConfig& get_numa_config() const { return config; }

   private:
    NumaConfig config;

    // std::set uses std::less by default, which is required for pointer comparison to be defined.
    std::set<NumaReplicatedBase*> trackedReplicatedObjects;
};

inline NumaReplicatedBase::NumaReplicatedBase(NumaReplicationContext& ctx) :
    context(&ctx) {
    context->attach(this);
}

inline NumaReplicatedBase::NumaReplicatedBase(NumaReplicatedBase&& other) noexcept :
    context(std::exchange(other.context, nullptr)) {
    context->move_attached(&other, this);
}

inline NumaReplicatedBase& NumaReplicatedBase::operator=(NumaReplicatedBase&& other) noexcept {
    context = std::exchange(other.context, nullptr);

    context->move_attached(&other, this);

    return *this;
}

inline NumaReplicatedBase::~NumaReplicatedBase() {
    if (context != nullptr)
        context->detach(this);
}

inline const NumaConfig& NumaReplicatedBase::get_numa_config() const {
    return context->get_numa_config();
}

}  // namespace Stockfish


#endif  // #ifndef NUMA_H_INCLUDED
