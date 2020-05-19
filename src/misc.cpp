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

#ifdef _WIN32
#if _WIN32_WINNT < 0x0601
#undef  _WIN32_WINNT
#define _WIN32_WINNT 0x0601 // Force to include needed API prototypes
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
typedef bool(*fun1_t)(LOGICAL_PROCESSOR_RELATIONSHIP,
                      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD);
typedef bool(*fun2_t)(USHORT, PGROUP_AFFINITY);
typedef bool(*fun3_t)(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY);
}
#endif

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#include <stdlib.h>
#include <sys/mman.h>
#endif

#include "misc.h"
#include "thread.h"

using namespace std;

namespace {

/// Version number. If Version is left empty, then compile date in the format
/// DD-MM-YY and show in engine_info.
const string Version = "";

/// Our fancy logging facility. The trick here is to replace cin.rdbuf() and
/// cout.rdbuf() with two Tie objects that tie cin and cout to a file stream. We
/// can toggle the logging of std::cout and std:cin at runtime whilst preserving
/// usual I/O functionality, all without changing a single line of code!
/// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81

struct Tie: public streambuf { // MSVC requires split streambuf for cin and cout

  Tie(streambuf* b, streambuf* l) : buf(b), logBuf(l) {}

  int sync() override { return logBuf->pubsync(), buf->pubsync(); }
  int overflow(int c) override { return log(buf->sputc((char)c), "<< "); }
  int underflow() override { return buf->sgetc(); }
  int uflow() override { return log(buf->sbumpc(), ">> "); }

  streambuf *buf, *logBuf;

  int log(int c, const char* prefix) {

    static int last = '\n'; // Single log file

    if (last == '\n')
        logBuf->sputn(prefix, 3);

    return last = logBuf->sputc((char)c);
  }
};

class Logger {

  Logger() : in(cin.rdbuf(), file.rdbuf()), out(cout.rdbuf(), file.rdbuf()) {}
 ~Logger() { start(""); }

  ofstream file;
  Tie in, out;

public:
  static void start(const std::string& fname) {

    static Logger l;

    if (!fname.empty() && !l.file.is_open())
    {
        l.file.open(fname, ifstream::out);

        if (!l.file.is_open())
        {
            cerr << "Unable to open debug log file " << fname << endl;
            exit(EXIT_FAILURE);
        }

        cin.rdbuf(&l.in);
        cout.rdbuf(&l.out);
    }
    else if (fname.empty() && l.file.is_open())
    {
        cout.rdbuf(l.out.buf);
        cin.rdbuf(l.in.buf);
        l.file.close();
    }
  }
};

} // namespace

/// engine_info() returns the full name of the current Stockfish version. This
/// will be either "Stockfish <Tag> DD-MM-YY" (where DD-MM-YY is the date when
/// the program was compiled) or "Stockfish <Version>", depending on whether
/// Version is empty.

const string engine_info(bool to_uci) {

  const string months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");
  string month, day, year;
  stringstream ss, date(__DATE__); // From compiler, format is "Sep 21 2008"

  ss << "Stockfish " << Version << setfill('0');

  if (Version.empty())
  {
      date >> month >> day >> year;
      ss << setw(2) << day << setw(2) << (1 + months.find(month) / 4) << year.substr(2);
  }

  ss << (Is64Bit ? " 64" : "")
     << (HasPext ? " BMI2" : (HasPopCnt ? " POPCNT" : ""))
     << (to_uci  ? "\nid author ": " by ")
     << "T. Romstad, M. Costalba, J. Kiiski, G. Linscott";

  return ss.str();
}


/// compiler_info() returns a string trying to describe the compiler we use

const std::string compiler_info() {

  #define stringify2(x) #x
  #define stringify(x) stringify2(x)
  #define make_version_string(major, minor, patch) stringify(major) "." stringify(minor) "." stringify(patch)

/// Predefined macros hell:
///
/// __GNUC__           Compiler is gcc, Clang or Intel on Linux
/// __INTEL_COMPILER   Compiler is Intel
/// _MSC_VER           Compiler is MSVC or Intel on Windows
/// _WIN32             Building on Windows (any)
/// _WIN64             Building on Windows 64 bit

  std::string compiler = "\nCompiled by ";

  #ifdef __clang__
     compiler += "clang++ ";
     compiler += make_version_string(__clang_major__, __clang_minor__, __clang_patchlevel__);
  #elif __INTEL_COMPILER
     compiler += "Intel compiler ";
     compiler += "(version ";
     compiler += stringify(__INTEL_COMPILER) " update " stringify(__INTEL_COMPILER_UPDATE);
     compiler += ")";
  #elif _MSC_VER
     compiler += "MSVC ";
     compiler += "(version ";
     compiler += stringify(_MSC_FULL_VER) "." stringify(_MSC_BUILD);
     compiler += ")";
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

  compiler += "\n __VERSION__ macro expands to: ";
  #ifdef __VERSION__
     compiler += __VERSION__;
  #else
     compiler += "(undefined macro)";
  #endif
  compiler += "\n";

  return compiler;
}


/// Debug functions used mainly to collect run-time statistics
static std::atomic<int64_t> hits[2], means[2];

void dbg_hit_on(bool b) { ++hits[0]; if (b) ++hits[1]; }
void dbg_hit_on(bool c, bool b) { if (c) dbg_hit_on(b); }
void dbg_mean_of(int v) { ++means[0]; means[1] += v; }

void dbg_print() {

  if (hits[0])
      cerr << "Total " << hits[0] << " Hits " << hits[1]
           << " hit rate (%) " << 100 * hits[1] / hits[0] << endl;

  if (means[0])
      cerr << "Total " << means[0] << " Mean "
           << (double)means[1] / means[0] << endl;
}


/// Used to serialize access to std::cout to avoid multiple threads writing at
/// the same time.

std::ostream& operator<<(std::ostream& os, SyncCout sc) {

  static std::mutex m;

  if (sc == IO_LOCK)
      m.lock();

  if (sc == IO_UNLOCK)
      m.unlock();

  return os;
}


/// Trampoline helper to avoid moving Logger to misc.h
void start_logger(const std::string& fname) { Logger::start(fname); }


/// prefetch() preloads the given address in L1/L2 cache. This is a non-blocking
/// function that doesn't stall the CPU waiting for data to be loaded from memory,
/// which can be quite slow.
#ifdef NO_PREFETCH

void prefetch(void*) {}

#else

void prefetch(void* addr) {

#  if defined(__INTEL_COMPILER)
   // This hack prevents prefetches from being optimized away by
   // Intel compiler. Both MSVC and gcc seem not be affected by this.
   __asm__ ("");
#  endif

#  if defined(__INTEL_COMPILER) || defined(_MSC_VER)
  _mm_prefetch((char*)addr, _MM_HINT_T0);
#  else
  __builtin_prefetch(addr);
#  endif
}

#endif


/// aligned_ttmem_alloc will return suitably aligned memory, and if possible use large pages.
/// The returned pointer is the aligned one, while the mem argument is the one that needs to be passed to free.
/// With c++17 some of this functionality can be simplified.
#if defined(__linux__) && !defined(__ANDROID__)

void* aligned_ttmem_alloc(size_t allocSize, void*& mem) {

  constexpr size_t alignment = 2 * 1024 * 1024; // assumed 2MB page sizes
  size_t size = ((allocSize + alignment - 1) / alignment) * alignment; // multiple of alignment
  if (posix_memalign(&mem, alignment, size))
     mem = nullptr;
  madvise(mem, allocSize, MADV_HUGEPAGE);
  return mem;
}

#elif defined(_WIN64)

static void* aligned_ttmem_alloc_large_pages(size_t allocSize) {

  HANDLE hProcessToken { };
  LUID luid { };
  void* mem = nullptr;

  const size_t largePageSize = GetLargePageMinimum();
  if (!largePageSize)
      return nullptr;

  // We need SeLockMemoryPrivilege, so try to enable it for the process
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hProcessToken))
      return nullptr;

  if (LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &luid))
  {
      TOKEN_PRIVILEGES tp { };
      TOKEN_PRIVILEGES prevTp { };
      DWORD prevTpLen = 0;

      tp.PrivilegeCount = 1;
      tp.Privileges[0].Luid = luid;
      tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

      // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
      // we still need to query GetLastError() to ensure that the privileges were actually obtained...
      if (AdjustTokenPrivileges(
              hProcessToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &prevTp, &prevTpLen) &&
          GetLastError() == ERROR_SUCCESS)
      {
          // round up size to full pages and allocate
          allocSize = (allocSize + largePageSize - 1) & ~size_t(largePageSize - 1);
          mem = VirtualAlloc(
              NULL, allocSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);

          // privilege no longer needed, restore previous state
          AdjustTokenPrivileges(hProcessToken, FALSE, &prevTp, 0, NULL, NULL);
      }
  }

  CloseHandle(hProcessToken);

  return mem;
}

void* aligned_ttmem_alloc(size_t allocSize, void*& mem) {

  static bool firstCall = true;

  // try to allocate large pages
  mem = aligned_ttmem_alloc_large_pages(allocSize);

  // Suppress info strings on the first call. The first call occurs before 'uci'
  // is received and in that case this output confuses some GUIs.
  if (!firstCall)
  {
      if (mem)
          sync_cout << "info string Hash table allocation: Windows large pages used." << sync_endl;
      else
          sync_cout << "info string Hash table allocation: Windows large pages not used." << sync_endl;
  }
  firstCall = false;

  // fall back to regular, page aligned, allocation if necessary
  if (!mem)
      mem = VirtualAlloc(NULL, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  return mem;
}

#else

void* aligned_ttmem_alloc(size_t allocSize, void*& mem) {

  constexpr size_t alignment = 64; // assumed cache line size
  size_t size = allocSize + alignment - 1; // allocate some extra space
  mem = malloc(size);
  void* ret = reinterpret_cast<void*>((uintptr_t(mem) + alignment - 1) & ~uintptr_t(alignment - 1));
  return ret;
}

#endif

/// aligned_ttmem_free will free the previously allocated ttmem
#if defined(_WIN64)

void aligned_ttmem_free(void* mem) {

  if (mem && !VirtualFree(mem, 0, MEM_RELEASE))
  {
      DWORD err = GetLastError();
      std::cerr << "Failed to free transposition table. Error code: 0x" <<
          std::hex << err << std::dec << std::endl;
      exit(EXIT_FAILURE);
  }
}

#else

void aligned_ttmem_free(void *mem) {
  free(mem);
}

#endif


namespace WinProcGroup {

#ifndef _WIN32

void bindThisThread(size_t) {}

#else

/// best_group() retrieves logical processor information using Windows specific
/// API and returns the best group id for the thread with index idx. Original
/// code from Texel by Peter Österlund.

int best_group(size_t idx) {

  int threads = 0;
  int nodes = 0;
  int cores = 0;
  DWORD returnLength = 0;
  DWORD byteOffset = 0;

  // Early exit if the needed API is not available at runtime
  HMODULE k32 = GetModuleHandle("Kernel32.dll");
  auto fun1 = (fun1_t)(void(*)())GetProcAddress(k32, "GetLogicalProcessorInformationEx");
  if (!fun1)
      return -1;

  // First call to get returnLength. We expect it to fail due to null buffer
  if (fun1(RelationAll, nullptr, &returnLength))
      return -1;

  // Once we know returnLength, allocate the buffer
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *buffer, *ptr;
  ptr = buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)malloc(returnLength);

  // Second call, now we expect to succeed
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
      ptr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(((char*)ptr) + ptr->Size);
  }

  free(buffer);

  std::vector<int> groups;

  // Run as many threads as possible on the same node until core limit is
  // reached, then move on filling the next node.
  for (int n = 0; n < nodes; n++)
      for (int i = 0; i < cores / nodes; i++)
          groups.push_back(n);

  // In case a core has more than one logical processor (we assume 2) and we
  // have still threads to allocate, then spread them evenly across available
  // nodes.
  for (int t = 0; t < threads - cores; t++)
      groups.push_back(t % nodes);

  // If we still have more threads than the total number of logical processors
  // then return -1 and let the OS to decide what to do.
  return idx < groups.size() ? groups[idx] : -1;
}


/// bindThisThread() set the group affinity of the current thread

void bindThisThread(size_t idx) {

  // Use only local variables to be thread-safe
  int group = best_group(idx);

  if (group == -1)
      return;

  // Early exit if the needed API are not available at runtime
  HMODULE k32 = GetModuleHandle("Kernel32.dll");
  auto fun2 = (fun2_t)(void(*)())GetProcAddress(k32, "GetNumaNodeProcessorMaskEx");
  auto fun3 = (fun3_t)(void(*)())GetProcAddress(k32, "SetThreadGroupAffinity");

  if (!fun2 || !fun3)
      return;

  GROUP_AFFINITY affinity;
  if (fun2(group, &affinity))
      fun3(GetCurrentThread(), &affinity, nullptr);
}

#endif

} // namespace WinProcGroup
