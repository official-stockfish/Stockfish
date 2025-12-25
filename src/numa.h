/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <cstring>

#include "memory.h"

// We support linux very well, but we explicitly do NOT support Android,
// because there is no affected systems, not worth maintaining.
#if defined(__linux__) && !defined(__ANDROID__)
    #if !defined(_GNU_SOURCE)
        #define _GNU_SOURCE
    #endif
    #include <sched.h>
#elif defined(_WIN64)

    #if _WIN32_WINNT < 0x0601
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601  // Force to include needed API prototypes
    #endif

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

// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadselectedcpusetmasks
using GetThreadSelectedCpuSetMasks_t = BOOL (*)(HANDLE, PGROUP_AFFINITY, USHORT, PUSHORT);

#endif

#include "misc.h"

namespace Stockfish {

using CpuIndex  = size_t;
using NumaIndex = size_t;

inline CpuIndex get_hardware_concurrency() {
    CpuIndex concurrency = std::thread::hardware_concurrency();

    // Get all processors across all processor groups on windows, since
    // hardware_concurrency() only returns the number of processors in
    // the first group, because only these are available to std::thread.
#ifdef _WIN64
    concurrency = std::max<CpuIndex>(concurrency, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
#endif

    return concurrency;
}

inline const CpuIndex SYSTEM_THREADS_NB = std::max<CpuIndex>(1, get_hardware_concurrency());

#if defined(_WIN64)

struct WindowsAffinity {
    std::optional<std::set<CpuIndex>> oldApi;
    std::optional<std::set<CpuIndex>> newApi;

    // We also provide diagnostic for when the affinity is set to nullopt
    // whether it was due to being indeterminate. If affinity is indeterminate
    // it is best to assume it is not set at all, so consistent with the meaning
    // of the nullopt affinity.
    bool isNewDeterminate = true;
    bool isOldDeterminate = true;

    std::optional<std::set<CpuIndex>> get_combined() const {
        if (!oldApi.has_value())
            return newApi;
        if (!newApi.has_value())
            return oldApi;

        std::set<CpuIndex> intersect;
        std::set_intersection(oldApi->begin(), oldApi->end(), newApi->begin(), newApi->end(),
                              std::inserter(intersect, intersect.begin()));
        return intersect;
    }

    // Since Windows 11 and Windows Server 2022 thread affinities can span
    // processor groups and can be set as such by a new WinAPI function. However,
    // we may need to force using the old API if we detect that the process has
    // affinity set by the old API already and we want to override that. Due to the
    // limitations of the old API we cannot detect its use reliably. There will be
    // cases where we detect not use but it has actually been used and vice versa.

    bool likely_used_old_api() const { return oldApi.has_value() || !isOldDeterminate; }
};

inline std::pair<BOOL, std::vector<USHORT>> get_process_group_affinity() {

    // GetProcessGroupAffinity requires the GroupArray argument to be
    // aligned to 4 bytes instead of just 2.
    static constexpr size_t GroupArrayMinimumAlignment = 4;
    static_assert(GroupArrayMinimumAlignment >= alignof(USHORT));

    // The function should succeed the second time, but it may fail if the group
    // affinity has changed between GetProcessGroupAffinity calls. In such case
    // we consider this a hard error, as we Cannot work with unstable affinities
    // anyway.
    static constexpr int MAX_TRIES  = 2;
    USHORT               GroupCount = 1;
    for (int i = 0; i < MAX_TRIES; ++i)
    {
        auto GroupArray = std::make_unique<USHORT[]>(
          GroupCount + (GroupArrayMinimumAlignment / alignof(USHORT) - 1));

        USHORT* GroupArrayAligned = align_ptr_up<GroupArrayMinimumAlignment>(GroupArray.get());

        const BOOL status =
          GetProcessGroupAffinity(GetCurrentProcess(), &GroupCount, GroupArrayAligned);

        if (status == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            break;
        }

        if (status != 0)
        {
            return std::make_pair(status,
                                  std::vector(GroupArrayAligned, GroupArrayAligned + GroupCount));
        }
    }

    return std::make_pair(0, std::vector<USHORT>());
}

// On Windows there are two ways to set affinity, and therefore 2 ways to get it.
// These are not consistent, so we have to check both. In some cases it is actually
// not possible to determine affinity. For example when two different threads have
// affinity on different processor groups, set using SetThreadAffinityMask, we cannot
// retrieve the actual affinities.
// From documentation on GetProcessAffinityMask:
//     > If the calling process contains threads in multiple groups,
//     > the function returns zero for both affinity masks.
// In such cases we just give up and assume we have affinity for all processors.
// nullopt means no affinity is set, that is, all processors are allowed
inline WindowsAffinity get_process_affinity() {
    HMODULE k32                            = GetModuleHandle(TEXT("Kernel32.dll"));
    auto    GetThreadSelectedCpuSetMasks_f = GetThreadSelectedCpuSetMasks_t(
      (void (*)()) GetProcAddress(k32, "GetThreadSelectedCpuSetMasks"));

    BOOL status = 0;

    WindowsAffinity affinity;

    if (GetThreadSelectedCpuSetMasks_f != nullptr)
    {
        USHORT RequiredMaskCount;
        status = GetThreadSelectedCpuSetMasks_f(GetCurrentThread(), nullptr, 0, &RequiredMaskCount);

        // We expect ERROR_INSUFFICIENT_BUFFER from GetThreadSelectedCpuSetMasks,
        // but other failure is an actual error.
        if (status == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            affinity.isNewDeterminate = false;
        }
        else if (RequiredMaskCount > 0)
        {
            // If RequiredMaskCount then these affinities were never set, but it's
            // not consistent so GetProcessAffinityMask may still return some affinity.
            auto groupAffinities = std::make_unique<GROUP_AFFINITY[]>(RequiredMaskCount);

            status = GetThreadSelectedCpuSetMasks_f(GetCurrentThread(), groupAffinities.get(),
                                                    RequiredMaskCount, &RequiredMaskCount);

            if (status == 0)
            {
                affinity.isNewDeterminate = false;
            }
            else
            {
                std::set<CpuIndex> cpus;

                for (USHORT i = 0; i < RequiredMaskCount; ++i)
                {
                    const size_t procGroupIndex = groupAffinities[i].Group;

                    for (size_t j = 0; j < WIN_PROCESSOR_GROUP_SIZE; ++j)
                    {
                        if (groupAffinities[i].Mask & (KAFFINITY(1) << j))
                            cpus.insert(procGroupIndex * WIN_PROCESSOR_GROUP_SIZE + j);
                    }
                }

                affinity.newApi = std::move(cpus);
            }
        }
    }

    // NOTE: There is no way to determine full affinity using the old API if
    //       individual threads set affinity on different processor groups.

    DWORD_PTR proc, sys;
    status = GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys);

    // If proc == 0 then we cannot determine affinity because it spans processor groups.
    // On Windows 11 and Server 2022 it will instead
    //     > If, however, hHandle specifies a handle to the current process, the function
    //     > always uses the calling thread's primary group (which by default is the same
    //     > as the process' primary group) in order to set the
    //     > lpProcessAffinityMask and lpSystemAffinityMask.
    // So it will never be indeterminate here. We can only make assumptions later.
    if (status == 0 || proc == 0)
    {
        affinity.isOldDeterminate = false;
        return affinity;
    }

    // If SetProcessAffinityMask was never called the affinity must span
    // all processor groups, but if it was called it must only span one.

    std::vector<USHORT> groupAffinity;  // We need to capture this later and capturing
                                        // from structured bindings requires c++20.

    std::tie(status, groupAffinity) = get_process_group_affinity();
    if (status == 0)
    {
        affinity.isOldDeterminate = false;
        return affinity;
    }

    if (groupAffinity.size() == 1)
    {
        // We detect the case when affinity is set to all processors and correctly
        // leave affinity.oldApi as nullopt.
        if (GetActiveProcessorGroupCount() != 1 || proc != sys)
        {
            std::set<CpuIndex> cpus;

            const size_t procGroupIndex = groupAffinity[0];

            const uint64_t mask = static_cast<uint64_t>(proc);
            for (size_t j = 0; j < WIN_PROCESSOR_GROUP_SIZE; ++j)
            {
                if (mask & (KAFFINITY(1) << j))
                    cpus.insert(procGroupIndex * WIN_PROCESSOR_GROUP_SIZE + j);
            }

            affinity.oldApi = std::move(cpus);
        }
    }
    else
    {
        // If we got here it means that either SetProcessAffinityMask was never set
        // or we're on Windows 11/Server 2022.

        // Since Windows 11 and Windows Server 2022 the behaviour of
        // GetProcessAffinityMask changed:
        //     > If, however, hHandle specifies a handle to the current process,
        //     > the function always uses the calling thread's primary group
        //     > (which by default is the same as the process' primary group)
        //     > in order to set the lpProcessAffinityMask and lpSystemAffinityMask.
        // In which case we can actually retrieve the full affinity.

        if (GetThreadSelectedCpuSetMasks_f != nullptr)
        {
            std::thread th([&]() {
                std::set<CpuIndex> cpus;
                bool               isAffinityFull = true;

                for (auto procGroupIndex : groupAffinity)
                {
                    const int numActiveProcessors =
                      GetActiveProcessorCount(static_cast<WORD>(procGroupIndex));

                    // We have to schedule to two different processors
                    // and & the affinities we get. Otherwise our processor
                    // choice could influence the resulting affinity.
                    // We assume the processor IDs within the group are
                    // filled sequentially from 0.
                    uint64_t procCombined = std::numeric_limits<uint64_t>::max();
                    uint64_t sysCombined  = std::numeric_limits<uint64_t>::max();

                    for (int i = 0; i < std::min(numActiveProcessors, 2); ++i)
                    {
                        GROUP_AFFINITY GroupAffinity;
                        std::memset(&GroupAffinity, 0, sizeof(GROUP_AFFINITY));
                        GroupAffinity.Group = static_cast<WORD>(procGroupIndex);

                        GroupAffinity.Mask = static_cast<KAFFINITY>(1) << i;

                        status =
                          SetThreadGroupAffinity(GetCurrentThread(), &GroupAffinity, nullptr);
                        if (status == 0)
                        {
                            affinity.isOldDeterminate = false;
                            return;
                        }

                        SwitchToThread();

                        DWORD_PTR proc2, sys2;
                        status = GetProcessAffinityMask(GetCurrentProcess(), &proc2, &sys2);
                        if (status == 0)
                        {
                            affinity.isOldDeterminate = false;
                            return;
                        }

                        procCombined &= static_cast<uint64_t>(proc2);
                        sysCombined &= static_cast<uint64_t>(sys2);
                    }

                    if (procCombined != sysCombined)
                        isAffinityFull = false;

                    for (size_t j = 0; j < WIN_PROCESSOR_GROUP_SIZE; ++j)
                    {
                        if (procCombined & (KAFFINITY(1) << j))
                            cpus.insert(procGroupIndex * WIN_PROCESSOR_GROUP_SIZE + j);
                    }
                }

                // We have to detect the case where the affinity was not set,
                // or is set to all processors so that we correctly produce as
                // std::nullopt result.
                if (!isAffinityFull)
                {
                    affinity.oldApi = std::move(cpus);
                }
            });

            th.join();
        }
    }

    return affinity;
}

#endif

#if defined(__linux__) && !defined(__ANDROID__)

inline std::set<CpuIndex> get_process_affinity() {

    std::set<CpuIndex> cpus;

    // For unsupported systems, or in case of a soft error, we may assume
    // all processors are available for use.
    [[maybe_unused]] auto set_to_all_cpus = [&]() {
        for (CpuIndex c = 0; c < SYSTEM_THREADS_NB; ++c)
            cpus.insert(c);
    };

    // cpu_set_t by default holds 1024 entries. This may not be enough soon,
    // but there is no easy way to determine how many threads there actually
    // is. In this case we just choose a reasonable upper bound.
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

    return cpus;
}

#endif

#if defined(__linux__) && !defined(__ANDROID__)

inline static const auto STARTUP_PROCESSOR_AFFINITY = get_process_affinity();

#elif defined(_WIN64)

inline static const auto STARTUP_PROCESSOR_AFFINITY = get_process_affinity();
inline static const auto STARTUP_USE_OLD_AFFINITY_API =
  STARTUP_PROCESSOR_AFFINITY.likely_used_old_api();

#endif

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

// Designed as immutable, because there is no good reason to alter an already
// existing config in a way that doesn't require recreating it completely, and
// it would be complex and expensive to maintain class invariants.
// The CPU (processor) numbers always correspond to the actual numbering used
// by the system. The NUMA node numbers MAY NOT correspond to the system's
// numbering of the NUMA nodes. In particular, empty nodes may be removed, or
// the user may create custom nodes. It is guaranteed that NUMA nodes are NOT
// empty: every node exposed by NumaConfig has at least one processor assigned.
//
// We use startup affinities so as not to modify its own behaviour in time.
//
// Since Stockfish doesn't support exceptions all places where an exception
// should be thrown are replaced by std::exit.
class NumaConfig {
   public:
    NumaConfig() :
        highestCpuIndex(0),
        customAffinity(false) {
        const auto numCpus = SYSTEM_THREADS_NB;
        add_cpu_range_to_node(NumaIndex{0}, CpuIndex{0}, numCpus - 1);
    }

    // This function queries the system for the mapping of processors to NUMA nodes.
    // On Linux we read from standardized kernel sysfs, with a fallback to single NUMA
    // node. On Windows we utilize GetNumaProcessorNodeEx, which has its quirks, see
    // comment for Windows implementation of get_process_affinity.
    static NumaConfig from_system([[maybe_unused]] bool respectProcessAffinity = true) {
        NumaConfig cfg = empty();

#if defined(__linux__) && !defined(__ANDROID__)

        std::set<CpuIndex> allowedCpus;

        if (respectProcessAffinity)
            allowedCpus = STARTUP_PROCESSOR_AFFINITY;

        auto is_cpu_allowed = [respectProcessAffinity, &allowedCpus](CpuIndex c) {
            return !respectProcessAffinity || allowedCpus.count(c) == 1;
        };

        // On Linux things are straightforward, since there's no processor groups and
        // any thread can be scheduled on all processors.
        // We try to gather this information from the sysfs first
        // https://www.kernel.org/doc/Documentation/ABI/stable/sysfs-devices-node

        bool useFallback = false;
        auto fallback    = [&]() {
            useFallback = true;
            cfg         = empty();
        };

        // /sys/devices/system/node/online contains information about active NUMA nodes
        auto nodeIdsStr = read_file_to_string("/sys/devices/system/node/online");
        if (!nodeIdsStr.has_value() || nodeIdsStr->empty())
        {
            fallback();
        }
        else
        {
            remove_whitespace(*nodeIdsStr);
            for (size_t n : indices_from_shortened_string(*nodeIdsStr))
            {
                // /sys/devices/system/node/node.../cpulist
                std::string path =
                  std::string("/sys/devices/system/node/node") + std::to_string(n) + "/cpulist";
                auto cpuIdsStr = read_file_to_string(path);
                // Now, we only bail if the file does not exist. Some nodes may be
                // empty, that's fine. An empty node still has a file that appears
                // to have some whitespace, so we need to handle that.
                if (!cpuIdsStr.has_value())
                {
                    fallback();
                    break;
                }
                else
                {
                    remove_whitespace(*cpuIdsStr);
                    for (size_t c : indices_from_shortened_string(*cpuIdsStr))
                    {
                        if (is_cpu_allowed(c))
                            cfg.add_cpu_to_node(n, c);
                    }
                }
            }
        }

        if (useFallback)
        {
            for (CpuIndex c = 0; c < SYSTEM_THREADS_NB; ++c)
                if (is_cpu_allowed(c))
                    cfg.add_cpu_to_node(NumaIndex{0}, c);
        }

#elif defined(_WIN64)

        std::optional<std::set<CpuIndex>> allowedCpus;

        if (respectProcessAffinity)
            allowedCpus = STARTUP_PROCESSOR_AFFINITY.get_combined();

        // The affinity cannot be determined in all cases on Windows,
        // but we at least guarantee that the number of allowed processors
        // is >= number of processors in the affinity mask. In case the user
        // is not satisfied they must set the processor numbers explicitly.
        auto is_cpu_allowed = [&allowedCpus](CpuIndex c) {
            return !allowedCpus.has_value() || allowedCpus->count(c) == 1;
        };

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
        // We also do this is if need to force old API for some reason.
        //
        // 2024-08-26: It appears that we need to actually always force this behaviour.
        // While Windows allows this to work now, such assignments have bad interaction
        // with the scheduler - in particular it still prefers scheduling on the thread's
        // "primary" node, even if it means scheduling SMT processors first.
        // See https://github.com/official-stockfish/Stockfish/issues/5551
        // See https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups
        //
        //     Each process is assigned a primary group at creation, and by default all
        //     of its threads' primary group is the same. Each thread's ideal processor
        //     is in the thread's primary group, so threads will preferentially be
        //     scheduled to processors on their primary group, but they are able to
        //     be scheduled to processors on any other group.
        //
        // used to be guarded by if (STARTUP_USE_OLD_AFFINITY_API)
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
            cfg.add_cpu_to_node(NumaIndex{0}, c);

#endif

        // We have to ensure no empty NUMA nodes persist.
        cfg.remove_empty_numa_nodes();

        // If the user explicitly opts out from respecting the current process affinity
        // then it may be inconsistent with the current affinity (obviously), so we
        // consider it custom.
        if (!respectProcessAffinity)
            cfg.customAffinity = true;

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
            auto indices = indices_from_shortened_string(std::string(nodeStr));
            if (!indices.empty())
            {
                for (auto idx : indices)
                {
                    if (!cfg.add_cpu_to_node(n, CpuIndex(idx)))
                        std::exit(EXIT_FAILURE);
                }

                n += 1;
            }
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
        // If we can reasonably determine that the threads cannot be contained
        // by the OS within the first NUMA node then we advise distributing
        // and binding threads. When the threads are not bound we can only use
        // NUMA memory replicated objects from the first node, so when the OS
        // has to schedule on other nodes we lose performance. We also suggest
        // binding if there's enough threads to distribute among nodes with minimal
        // disparity. We try to ignore small nodes, in particular the empty ones.

        // If the affinity set by the user does not match the affinity given by
        // the OS then binding is necessary to ensure the threads are running on
        // correct processors.
        if (customAffinity)
            return true;

        // We obviously cannot distribute a single thread, so a single thread
        // should never be bound.
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
            // Special case for when there's no NUMA nodes. This doesn't buy us
            // much, but let's keep the default path simple.
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
                    // NOTE: Do we want to perhaps fill the first available node
                    //       up to 50% first before considering other nodes?
                    //       Probably not, because it would interfere with running
                    //       multiple instances. We basically shouldn't favor any
                    //       particular node.
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

#elif defined(_WIN64)

        // Requires Windows 11. No good way to set thread affinity spanning
        // processor groups before that.
        HMODULE k32                            = GetModuleHandle(TEXT("Kernel32.dll"));
        auto    SetThreadSelectedCpuSetMasks_f = SetThreadSelectedCpuSetMasks_t(
          (void (*)()) GetProcAddress(k32, "SetThreadSelectedCpuSetMasks"));

        // We ALWAYS set affinity with the new API if available, because
        // there's no downsides, and we forcibly keep it consistent with
        // the old API should we need to use it. I.e. we always keep this
        // as a superset of what we set with SetThreadGroupAffinity.
        if (SetThreadSelectedCpuSetMasks_f != nullptr)
        {
            // Only available on Windows 11 and Windows Server 2022 onwards
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

        // Sometimes we need to force the old API, but do not use it unless necessary.
        if (SetThreadSelectedCpuSetMasks_f == nullptr || STARTUP_USE_OLD_AFFINITY_API)
        {
            // On earlier windows version (since windows 7) we cannot run a single thread
            // on multiple processor groups, so we need to restrict the group.
            // We assume the group of the first processor listed for this node.
            // Processors from outside this group will not be assigned for this thread.
            // Normally this won't be an issue because windows used to assign NUMA nodes
            // such that they cannot span processor groups. However, since Windows 10
            // Build 20348 the behaviour changed, so there's a small window of versions
            // between this and Windows 11 that might exhibit problems with not all
            // processors being utilized.
            //
            // We handle this in NumaConfig::from_system by manually splitting the
            // nodes when we detect that there is no function to set affinity spanning
            // processor nodes. This is required because otherwise our thread distribution
            // code may produce suboptimal results.
            //
            // See https://learn.microsoft.com/en-us/windows/win32/procthread/numa-support
            GROUP_AFFINITY affinity;
            std::memset(&affinity, 0, sizeof(GROUP_AFFINITY));
            // We use an ordered set to be sure to get the smallest cpu number here.
            const size_t forcedProcGroupIndex = *(nodes[n].begin()) / WIN_PROCESSOR_GROUP_SIZE;
            affinity.Group                    = static_cast<WORD>(forcedProcGroupIndex);
            for (CpuIndex c : nodes[n])
            {
                const size_t procGroupIndex     = c / WIN_PROCESSOR_GROUP_SIZE;
                const size_t idxWithinProcGroup = c % WIN_PROCESSOR_GROUP_SIZE;
                // We skip processors that are not in the same processor group.
                // If everything was set up correctly this will never be an issue,
                // but we have to account for bad NUMA node specification.
                if (procGroupIndex != forcedProcGroupIndex)
                    continue;

                affinity.Mask |= KAFFINITY(1) << idxWithinProcGroup;
            }

            HANDLE hThread = GetCurrentThread();

            const BOOL status = SetThreadGroupAffinity(hThread, &affinity, nullptr);
            if (status == 0)
                std::exit(EXIT_FAILURE);

            // We yield this thread just to be sure it gets rescheduled. This is
            // defensive, allowed because this code is not performance critical.
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

    static std::vector<size_t> indices_from_shortened_string(const std::string& s) {
        std::vector<size_t> indices;

        if (s.empty())
            return indices;

        for (const auto& ss : split(s, ","))
        {
            if (ss.empty())
                continue;

            auto parts = split(ss, "-");
            if (parts.size() == 1)
            {
                const CpuIndex c = CpuIndex{str_to_size_t(std::string(parts[0]))};
                indices.emplace_back(c);
            }
            else if (parts.size() == 2)
            {
                const CpuIndex cfirst = CpuIndex{str_to_size_t(std::string(parts[0]))};
                const CpuIndex clast  = CpuIndex{str_to_size_t(std::string(parts[1]))};
                for (size_t c = cfirst; c <= clast; ++c)
                {
                    indices.emplace_back(c);
                }
            }
        }

        return indices;
    }
};

class NumaReplicationContext;

// Instances of this class are tracked by the NumaReplicationContext instance.
// NumaReplicationContext informs all tracked instances when NUMA configuration changes.
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

// We force boxing with a unique_ptr. If this becomes an issue due to added
// indirection we may need to add an option for a custom boxing type. When the
// NUMA config changes the value stored at the index 0 is replicated to other nodes.
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
        // Use the first one as the source. It doesn't matter which one we use,
        // because they all must be identical, but the first one is guaranteed to exist.
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

// We force boxing with a unique_ptr. If this becomes an issue due to added
// indirection we may need to add an option for a custom boxing type.
template<typename T>
class LazyNumaReplicated: public NumaReplicatedBase {
   public:
    using ReplicatorFuncType = std::function<T(const T&)>;

    LazyNumaReplicated(NumaReplicationContext& ctx) :
        NumaReplicatedBase(ctx) {
        prepare_replicate_from(T{});
    }

    LazyNumaReplicated(NumaReplicationContext& ctx, T&& source) :
        NumaReplicatedBase(ctx) {
        prepare_replicate_from(std::move(source));
    }

    LazyNumaReplicated(const LazyNumaReplicated&) = delete;
    LazyNumaReplicated(LazyNumaReplicated&& other) noexcept :
        NumaReplicatedBase(std::move(other)),
        instances(std::exchange(other.instances, {})) {}

    LazyNumaReplicated& operator=(const LazyNumaReplicated&) = delete;
    LazyNumaReplicated& operator=(LazyNumaReplicated&& other) noexcept {
        NumaReplicatedBase::operator=(*this, std::move(other));
        instances = std::exchange(other.instances, {});

        return *this;
    }

    LazyNumaReplicated& operator=(T&& source) {
        prepare_replicate_from(std::move(source));

        return *this;
    }

    ~LazyNumaReplicated() override = default;

    const T& operator[](NumaReplicatedAccessToken token) const {
        assert(token.get_numa_index() < instances.size());
        ensure_present(token.get_numa_index());
        return *(instances[token.get_numa_index()]);
    }

    const T& operator*() const { return *(instances[0]); }

    const T* operator->() const { return instances[0].get(); }

    template<typename FuncT>
    void modify_and_replicate(FuncT&& f) {
        auto source = std::move(instances[0]);
        std::forward<FuncT>(f)(*source);
        prepare_replicate_from(std::move(*source));
    }

    void on_numa_config_changed() override {
        // Use the first one as the source. It doesn't matter which one we use,
        // because they all must be identical, but the first one is guaranteed to exist.
        auto source = std::move(instances[0]);
        prepare_replicate_from(std::move(*source));
    }

   private:
    mutable std::vector<std::unique_ptr<T>> instances;
    mutable std::mutex                      mutex;

    void ensure_present(NumaIndex idx) const {
        assert(idx < instances.size());

        if (instances[idx] != nullptr)
            return;

        assert(idx != 0);

        std::unique_lock<std::mutex> lock(mutex);
        // Check again for races.
        if (instances[idx] != nullptr)
            return;

        const NumaConfig& cfg = get_numa_config();
        cfg.execute_on_numa_node(
          idx, [this, idx]() { instances[idx] = std::make_unique<T>(*instances[0]); });
    }

    void prepare_replicate_from(T&& source) {
        instances.clear();

        const NumaConfig& cfg = get_numa_config();
        if (cfg.requires_memory_replication())
        {
            assert(cfg.num_numa_nodes() > 0);

            // We just need to make sure the first instance is there.
            // Note that we cannot move here as we need to reallocate the data
            // on the correct NUMA node.
            cfg.execute_on_numa_node(
              0, [this, &source]() { instances.emplace_back(std::make_unique<T>(source)); });

            // Prepare others for lazy init.
            instances.resize(cfg.num_numa_nodes());
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

    // std::set uses std::less by default, which is required for pointer comparison
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
