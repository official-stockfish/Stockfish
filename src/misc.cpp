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

#include "misc.h"

#ifdef _WIN32
    #if _WIN32_WINNT < 0x0601
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601  // Force to include needed API prototypes
    #endif

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <windows.h>
// The needed Windows API for processor groups could be missed from old Windows
// versions, so instead of calling them directly (forcing the linker to resolve
// the calls at compile time), try to load them at runtime. To do this we need
// first to define the corresponding function pointers.
extern "C" {
using fun1_t = bool (*)(LOGICAL_PROCESSOR_RELATIONSHIP,
                        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,
                        PDWORD);
using fun2_t = bool (*)(USHORT, PGROUP_AFFINITY);
using fun3_t = bool (*)(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY);
using fun4_t = bool (*)(USHORT, PGROUP_AFFINITY, USHORT, PUSHORT);
using fun5_t = WORD (*)();
using fun6_t = bool (*)(HANDLE, DWORD, PHANDLE);
using fun7_t = bool (*)(LPCSTR, LPCSTR, PLUID);
using fun8_t = bool (*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
}
#endif

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string_view>

#include "types.h"

#if defined(__linux__) && !defined(__ANDROID__)
    #include <sys/mman.h>
#endif

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__OpenBSD__) \
  || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32)) \
  || defined(__e2k__)
    #define POSIXALIGNEDALLOC
    #include <stdlib.h>
#endif

namespace Stockfish {

namespace {

// Version number or dev.
constexpr std::string_view version = "16.1";

// Our fancy logging facility. The trick here is to replace cin.rdbuf() and
// cout.rdbuf() with two Tie objects that tie cin and cout to a file stream. We
// can toggle the logging of std::cout and std:cin at runtime whilst preserving
// usual I/O functionality, all without changing a single line of code!
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81

struct Tie: public std::streambuf {  // MSVC requires split streambuf for cin and cout

    Tie(std::streambuf* b, std::streambuf* l) :
        buf(b),
        logBuf(l) {}

    int sync() override { return logBuf->pubsync(), buf->pubsync(); }
    int overflow(int c) override { return log(buf->sputc(char(c)), "<< "); }
    int underflow() override { return buf->sgetc(); }
    int uflow() override { return log(buf->sbumpc(), ">> "); }

    std::streambuf *buf, *logBuf;

    int log(int c, const char* prefix) {

        static int last = '\n';  // Single log file

        if (last == '\n')
            logBuf->sputn(prefix, 3);

        return last = logBuf->sputc(char(c));
    }
};

class Logger {

    Logger() :
        in(std::cin.rdbuf(), file.rdbuf()),
        out(std::cout.rdbuf(), file.rdbuf()) {}
    ~Logger() { start(""); }

    std::ofstream file;
    Tie           in, out;

   public:
    static void start(const std::string& fname) {

        static Logger l;

        if (l.file.is_open())
        {
            std::cout.rdbuf(l.out.buf);
            std::cin.rdbuf(l.in.buf);
            l.file.close();
        }

        if (!fname.empty())
        {
            l.file.open(fname, std::ifstream::out);

            if (!l.file.is_open())
            {
                std::cerr << "Unable to open debug log file " << fname << std::endl;
                exit(EXIT_FAILURE);
            }

            std::cin.rdbuf(&l.in);
            std::cout.rdbuf(&l.out);
        }
    }
};

}  // namespace


// Returns the full name of the current Stockfish version.
// For local dev compiles we try to append the commit sha and commit date
// from git if that fails only the local compilation date is set and "nogit" is specified:
// Stockfish dev-YYYYMMDD-SHA
// or
// Stockfish dev-YYYYMMDD-nogit
//
// For releases (non-dev builds) we only include the version number:
// Stockfish version
std::string engine_info(bool to_uci) {
    std::stringstream ss;
    ss << "Stockfish " << version << std::setfill('0');

    if constexpr (version == "dev")
    {
        ss << "-";
#ifdef GIT_DATE
        ss << stringify(GIT_DATE);
#else
        constexpr std::string_view months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");
        std::string                month, day, year;
        std::stringstream          date(__DATE__);  // From compiler, format is "Sep 21 2008"

        date >> month >> day >> year;
        ss << year << std::setw(2) << std::setfill('0') << (1 + months.find(month) / 4)
           << std::setw(2) << std::setfill('0') << day;
#endif

        ss << "-";

#ifdef GIT_SHA
        ss << stringify(GIT_SHA);
#else
        ss << "nogit";
#endif
    }

    ss << (to_uci ? "\nid author " : " by ") << "the Stockfish developers (see AUTHORS file)";

    return ss.str();
}


// Returns a string trying to describe the compiler we use
std::string compiler_info() {

#define make_version_string(major, minor, patch) \
    stringify(major) "." stringify(minor) "." stringify(patch)

    // Predefined macros hell:
    //
    // __GNUC__                Compiler is GCC, Clang or ICX
    // __clang__               Compiler is Clang or ICX
    // __INTEL_LLVM_COMPILER   Compiler is ICX
    // _MSC_VER                Compiler is MSVC
    // _WIN32                  Building on Windows (any)
    // _WIN64                  Building on Windows 64 bit

    std::string compiler = "\nCompiled by                : ";

#if defined(__INTEL_LLVM_COMPILER)
    compiler += "ICX ";
    compiler += stringify(__INTEL_LLVM_COMPILER);
#elif defined(__clang__)
    compiler += "clang++ ";
    compiler += make_version_string(__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif _MSC_VER
    compiler += "MSVC ";
    compiler += "(version ";
    compiler += stringify(_MSC_FULL_VER) "." stringify(_MSC_BUILD);
    compiler += ")";
#elif defined(__e2k__) && defined(__LCC__)
    #define dot_ver2(n) \
        compiler += char('.'); \
        compiler += char('0' + (n) / 10); \
        compiler += char('0' + (n) % 10);

    compiler += "MCST LCC ";
    compiler += "(version ";
    compiler += std::to_string(__LCC__ / 100);
    dot_ver2(__LCC__ % 100) dot_ver2(__LCC_MINOR__) compiler += ")";
#elif __GNUC__
    compiler += "g++ (GNUC) ";
    compiler += make_version_string(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    compiler += "Unknown compiler ";
    compiler += "(unknown version)";
#endif

#if defined(__APPLE__)
    compiler += " on Apple";
#elif defined(__CYGWIN__)
    compiler += " on Cygwin";
#elif defined(__MINGW64__)
    compiler += " on MinGW64";
#elif defined(__MINGW32__)
    compiler += " on MinGW32";
#elif defined(__ANDROID__)
    compiler += " on Android";
#elif defined(__linux__)
    compiler += " on Linux";
#elif defined(_WIN64)
    compiler += " on Microsoft Windows 64-bit";
#elif defined(_WIN32)
    compiler += " on Microsoft Windows 32-bit";
#else
    compiler += " on unknown system";
#endif

    compiler += "\nCompilation architecture   : ";
#if defined(ARCH)
    compiler += stringify(ARCH);
#else
    compiler += "(undefined architecture)";
#endif

    compiler += "\nCompilation settings       : ";
    compiler += (Is64Bit ? "64bit" : "32bit");
#if defined(USE_VNNI)
    compiler += " VNNI";
#endif
#if defined(USE_AVX512)
    compiler += " AVX512";
#endif
    compiler += (HasPext ? " BMI2" : "");
#if defined(USE_AVX2)
    compiler += " AVX2";
#endif
#if defined(USE_SSE41)
    compiler += " SSE41";
#endif
#if defined(USE_SSSE3)
    compiler += " SSSE3";
#endif
#if defined(USE_SSE2)
    compiler += " SSE2";
#endif
    compiler += (HasPopCnt ? " POPCNT" : "");
#if defined(USE_NEON_DOTPROD)
    compiler += " NEON_DOTPROD";
#elif defined(USE_NEON)
    compiler += " NEON";
#endif

#if !defined(NDEBUG)
    compiler += " DEBUG";
#endif

    compiler += "\nCompiler __VERSION__ macro : ";
#ifdef __VERSION__
    compiler += __VERSION__;
#else
    compiler += "(undefined macro)";
#endif

    compiler += "\n";

    return compiler;
}


// Debug functions used mainly to collect run-time statistics
constexpr int MaxDebugSlots = 32;

namespace {

template<size_t N>
struct DebugInfo {
    std::atomic<int64_t> data[N] = {0};

    constexpr inline std::atomic<int64_t>& operator[](int index) { return data[index]; }
};

DebugInfo<2> hit[MaxDebugSlots];
DebugInfo<2> mean[MaxDebugSlots];
DebugInfo<3> stdev[MaxDebugSlots];
DebugInfo<6> correl[MaxDebugSlots];

}  // namespace

void dbg_hit_on(bool cond, int slot) {

    ++hit[slot][0];
    if (cond)
        ++hit[slot][1];
}

void dbg_mean_of(int64_t value, int slot) {

    ++mean[slot][0];
    mean[slot][1] += value;
}

void dbg_stdev_of(int64_t value, int slot) {

    ++stdev[slot][0];
    stdev[slot][1] += value;
    stdev[slot][2] += value * value;
}

void dbg_correl_of(int64_t value1, int64_t value2, int slot) {

    ++correl[slot][0];
    correl[slot][1] += value1;
    correl[slot][2] += value1 * value1;
    correl[slot][3] += value2;
    correl[slot][4] += value2 * value2;
    correl[slot][5] += value1 * value2;
}

void dbg_print() {

    int64_t n;
    auto    E   = [&n](int64_t x) { return double(x) / n; };
    auto    sqr = [](double x) { return x * x; };

    for (int i = 0; i < MaxDebugSlots; ++i)
        if ((n = hit[i][0]))
            std::cerr << "Hit #" << i << ": Total " << n << " Hits " << hit[i][1]
                      << " Hit Rate (%) " << 100.0 * E(hit[i][1]) << std::endl;

    for (int i = 0; i < MaxDebugSlots; ++i)
        if ((n = mean[i][0]))
        {
            std::cerr << "Mean #" << i << ": Total " << n << " Mean " << E(mean[i][1]) << std::endl;
        }

    for (int i = 0; i < MaxDebugSlots; ++i)
        if ((n = stdev[i][0]))
        {
            double r = sqrt(E(stdev[i][2]) - sqr(E(stdev[i][1])));
            std::cerr << "Stdev #" << i << ": Total " << n << " Stdev " << r << std::endl;
        }

    for (int i = 0; i < MaxDebugSlots; ++i)
        if ((n = correl[i][0]))
        {
            double r = (E(correl[i][5]) - E(correl[i][1]) * E(correl[i][3]))
                     / (sqrt(E(correl[i][2]) - sqr(E(correl[i][1])))
                        * sqrt(E(correl[i][4]) - sqr(E(correl[i][3]))));
            std::cerr << "Correl. #" << i << ": Total " << n << " Coefficient " << r << std::endl;
        }
}


// Used to serialize access to std::cout
// to avoid multiple threads writing at the same time.
std::ostream& operator<<(std::ostream& os, SyncCout sc) {

    static std::mutex m;

    if (sc == IO_LOCK)
        m.lock();

    if (sc == IO_UNLOCK)
        m.unlock();

    return os;
}


// Trampoline helper to avoid moving Logger to misc.h
void start_logger(const std::string& fname) { Logger::start(fname); }


#ifdef NO_PREFETCH

void prefetch(void*) {}

#else

void prefetch(void* addr) {

    #if defined(_MSC_VER)
    _mm_prefetch((char*) addr, _MM_HINT_T0);
    #else
    __builtin_prefetch(addr);
    #endif
}

#endif


// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc(). Memory allocated with
// std_aligned_alloc() must be freed with std_aligned_free().
void* std_aligned_alloc(size_t alignment, size_t size) {

#if defined(POSIXALIGNEDALLOC)
    void* mem;
    return posix_memalign(&mem, alignment, size) ? nullptr : mem;
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
    return _mm_malloc(size, alignment);
#elif defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif
}

void std_aligned_free(void* ptr) {

#if defined(POSIXALIGNEDALLOC)
    free(ptr);
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
    _mm_free(ptr);
#elif defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// aligned_large_pages_alloc() will return suitably aligned memory, if possible using large pages.

#if defined(_WIN32)

static void* aligned_large_pages_alloc_windows([[maybe_unused]] size_t allocSize) {

    #if !defined(_WIN64)
    return nullptr;
    #else

    HANDLE hProcessToken{};
    LUID   luid{};
    void*  mem = nullptr;

    const size_t largePageSize = GetLargePageMinimum();
    if (!largePageSize)
        return nullptr;

    // Dynamically link OpenProcessToken, LookupPrivilegeValue and AdjustTokenPrivileges

    HMODULE hAdvapi32 = GetModuleHandle(TEXT("advapi32.dll"));

    if (!hAdvapi32)
        hAdvapi32 = LoadLibrary(TEXT("advapi32.dll"));

    auto fun6 = fun6_t((void (*)()) GetProcAddress(hAdvapi32, "OpenProcessToken"));
    if (!fun6)
        return nullptr;
    auto fun7 = fun7_t((void (*)()) GetProcAddress(hAdvapi32, "LookupPrivilegeValueA"));
    if (!fun7)
        return nullptr;
    auto fun8 = fun8_t((void (*)()) GetProcAddress(hAdvapi32, "AdjustTokenPrivileges"));
    if (!fun8)
        return nullptr;

    // We need SeLockMemoryPrivilege, so try to enable it for the process
    if (!fun6(  // OpenProcessToken()
          GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hProcessToken))
        return nullptr;

    if (fun7(  // LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &luid)
          nullptr, "SeLockMemoryPrivilege", &luid))
    {
        TOKEN_PRIVILEGES tp{};
        TOKEN_PRIVILEGES prevTp{};
        DWORD            prevTpLen = 0;

        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
        // we still need to query GetLastError() to ensure that the privileges were actually obtained.
        if (fun8(  // AdjustTokenPrivileges()
              hProcessToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &prevTp, &prevTpLen)
            && GetLastError() == ERROR_SUCCESS)
        {
            // Round up size to full pages and allocate
            allocSize = (allocSize + largePageSize - 1) & ~size_t(largePageSize - 1);
            mem       = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                                     PAGE_READWRITE);

            // Privilege no longer needed, restore previous state
            fun8(  // AdjustTokenPrivileges ()
              hProcessToken, FALSE, &prevTp, 0, nullptr, nullptr);
        }
    }

    CloseHandle(hProcessToken);

    return mem;

    #endif
}

void* aligned_large_pages_alloc(size_t allocSize) {

    // Try to allocate large pages
    void* mem = aligned_large_pages_alloc_windows(allocSize);

    // Fall back to regular, page-aligned, allocation if necessary
    if (!mem)
        mem = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    return mem;
}

#else

void* aligned_large_pages_alloc(size_t allocSize) {

    #if defined(__linux__)
    constexpr size_t alignment = 2 * 1024 * 1024;  // assumed 2MB page size
    #else
    constexpr size_t alignment = 4096;  // assumed small page size
    #endif

    // Round up to multiples of alignment
    size_t size = ((allocSize + alignment - 1) / alignment) * alignment;
    void*  mem  = std_aligned_alloc(alignment, size);
    #if defined(MADV_HUGEPAGE)
    madvise(mem, size, MADV_HUGEPAGE);
    #endif
    return mem;
}

#endif


// aligned_large_pages_free() will free the previously allocated ttmem

#if defined(_WIN32)

void aligned_large_pages_free(void* mem) {

    if (mem && !VirtualFree(mem, 0, MEM_RELEASE))
    {
        DWORD err = GetLastError();
        std::cerr << "Failed to free large page memory. Error code: 0x" << std::hex << err
                  << std::dec << std::endl;
        exit(EXIT_FAILURE);
    }
}

#else

void aligned_large_pages_free(void* mem) { std_aligned_free(mem); }

#endif


namespace WinProcGroup {

#ifndef _WIN32

void bindThisThread(size_t) {}

#else

// Retrieves logical processor information using Windows-specific
// API and returns the best node id for the thread with index idx. Original
// code from Texel by Peter Österlund.
static int best_node(size_t idx) {

    int   threads      = 0;
    int   nodes        = 0;
    int   cores        = 0;
    DWORD returnLength = 0;
    DWORD byteOffset   = 0;

    // Early exit if the needed API is not available at runtime
    HMODULE k32  = GetModuleHandle(TEXT("Kernel32.dll"));
    auto    fun1 = (fun1_t) (void (*)()) GetProcAddress(k32, "GetLogicalProcessorInformationEx");
    if (!fun1)
        return -1;

    // First call to GetLogicalProcessorInformationEx() to get returnLength.
    // We expect the call to fail due to null buffer.
    if (fun1(RelationAll, nullptr, &returnLength))
        return -1;

    // Once we know returnLength, allocate the buffer
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *buffer, *ptr;
    ptr = buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) malloc(returnLength);

    // Second call to GetLogicalProcessorInformationEx(), now we expect to succeed
    if (!fun1(RelationAll, buffer, &returnLength))
    {
        free(buffer);
        return -1;
    }

    while (byteOffset < returnLength)
    {
        if (ptr->Relationship == RelationNumaNode)
            nodes++;

        else if (ptr->Relationship == RelationProcessorCore)
        {
            cores++;
            threads += (ptr->Processor.Flags == LTP_PC_SMT) ? 2 : 1;
        }

        assert(ptr->Size);
        byteOffset += ptr->Size;
        ptr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) (((char*) ptr) + ptr->Size);
    }

    free(buffer);

    std::vector<int> groups;

    // Run as many threads as possible on the same node until the core limit is
    // reached, then move on to filling the next node.
    for (int n = 0; n < nodes; n++)
        for (int i = 0; i < cores / nodes; i++)
            groups.push_back(n);

    // In case a core has more than one logical processor (we assume 2) and we
    // still have threads to allocate, spread them evenly across available nodes.
    for (int t = 0; t < threads - cores; t++)
        groups.push_back(t % nodes);

    // If we still have more threads than the total number of logical processors
    // then return -1 and let the OS to decide what to do.
    return idx < groups.size() ? groups[idx] : -1;
}


// Sets the group affinity of the current thread
void bindThisThread(size_t idx) {

    // Use only local variables to be thread-safe
    int node = best_node(idx);

    if (node == -1)
        return;

    // Early exit if the needed API are not available at runtime
    HMODULE k32  = GetModuleHandle(TEXT("Kernel32.dll"));
    auto    fun2 = fun2_t((void (*)()) GetProcAddress(k32, "GetNumaNodeProcessorMaskEx"));
    auto    fun3 = fun3_t((void (*)()) GetProcAddress(k32, "SetThreadGroupAffinity"));
    auto    fun4 = fun4_t((void (*)()) GetProcAddress(k32, "GetNumaNodeProcessorMask2"));
    auto    fun5 = fun5_t((void (*)()) GetProcAddress(k32, "GetMaximumProcessorGroupCount"));

    if (!fun2 || !fun3)
        return;

    if (!fun4 || !fun5)
    {
        GROUP_AFFINITY affinity;
        if (fun2(node, &affinity))                         // GetNumaNodeProcessorMaskEx
            fun3(GetCurrentThread(), &affinity, nullptr);  // SetThreadGroupAffinity
    }
    else
    {
        // If a numa node has more than one processor group, we assume they are
        // sized equal and we spread threads evenly across the groups.
        USHORT elements, returnedElements;
        elements                 = fun5();  // GetMaximumProcessorGroupCount
        GROUP_AFFINITY* affinity = (GROUP_AFFINITY*) malloc(elements * sizeof(GROUP_AFFINITY));
        if (fun4(node, affinity, elements, &returnedElements))  // GetNumaNodeProcessorMask2
            fun3(GetCurrentThread(), &affinity[idx % returnedElements],
                 nullptr);  // SetThreadGroupAffinity
        free(affinity);
    }
}

#endif

}  // namespace WinProcGroup

#ifdef _WIN32
    #include <direct.h>
    #define GETCWD _getcwd
#else
    #include <unistd.h>
    #define GETCWD getcwd
#endif

CommandLine::CommandLine(int _argc, char** _argv) :
    argc(_argc),
    argv(_argv) {
    std::string pathSeparator;

    // Extract the path+name of the executable binary
    std::string argv0 = argv[0];

#ifdef _WIN32
    pathSeparator = "\\";
    #ifdef _MSC_VER
    // Under windows argv[0] may not have the extension. Also _get_pgmptr() had
    // issues in some Windows 10 versions, so check returned values carefully.
    char* pgmptr = nullptr;
    if (!_get_pgmptr(&pgmptr) && pgmptr != nullptr && *pgmptr)
        argv0 = pgmptr;
    #endif
#else
    pathSeparator = "/";
#endif

    // Extract the working directory
    workingDirectory = "";
    char  buff[40000];
    char* cwd = GETCWD(buff, 40000);
    if (cwd)
        workingDirectory = cwd;

    // Extract the binary directory path from argv0
    binaryDirectory = argv0;
    size_t pos      = binaryDirectory.find_last_of("\\/");
    if (pos == std::string::npos)
        binaryDirectory = "." + pathSeparator;
    else
        binaryDirectory.resize(pos + 1);

    // Pattern replacement: "./" at the start of path is replaced by the working directory
    if (binaryDirectory.find("." + pathSeparator) == 0)
        binaryDirectory.replace(0, 1, workingDirectory);
}

}  // namespace Stockfish
