/*
 McBrain, a UCI chess playing engine derived from Stockfish and Glaurung 2.1
 Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
 Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish Authors)
 Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (Stockfish Authors)
 Copyright (C) 2017-2018 Michael Byrne, Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (McBrain Authors)
 
 McBrain is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 McBrain is distributed in the hope that it will be useful,
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
typedef bool(WINAPI *fun1_t)(LOGICAL_PROCESSOR_RELATIONSHIP,
                      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD);
typedef bool(WINAPI *fun2_t)(USHORT, PGROUP_AFFINITY);
typedef bool(WINAPI *fun3_t)(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY);

#include "VersionHelpers.h"
}
#endif

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <thread>
#include "misc.h"
#include "thread.h"

using namespace std;

namespace {

/// Version number. If Version is left empty, then compile date in the format
/// DD-MM-YY and show in engine_info.
#ifdef Maverick
const string Version = "the Maverick";
#else
const string Version = "9.8";
#endif

	
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

/// engine_info() returns the full name of the current SugaR version. This
/// will be either "SugaR <Tag> DD-MM-YY" (where DD-MM-YY is the date when
/// the program was compiled) or "SugaR <Version>", depending on whether
/// Version is empty.

const string engine_info(bool to_uci) {

  const string months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");
  string month, day, year;
  stringstream ss, date(__DATE__); // From compiler, format is "Sep 21 2008"
#ifdef Maverick
  ss << "McCain -> " << Version << setfill('0');
#else
  ss << "McBrain " << Version << setfill('0');
#endif
	
  if (Version.empty())
  {
      date >> month >> day >> year;
      ss << setw(2) << day << setw(2) << (1 + months.find(month) / 4) << year.substr(2);
  }
#ifdef Maverick
  ss //<< (Is64Bit ? " 64" : " 32")
     //<< (HasPext ? " BMI2" : (HasPopCnt ? " POPCNT" : ""))
	<< (to_uci  ? "\nid author ": "->")
	<< "dedicated to John S McCain, an American Hero.";
#else
	ss << (Is64Bit ? " 64" : " 32")
	<< (HasPext ? " BMI2" : (HasPopCnt ? " POPCNT" : ""))
	<< (to_uci  ? "\nid author ": " by ")
	<< "M. Byrne and scores of others...";
#endif

	 return ss.str();
}

const std::string system_info()
{
	std::stringstream result;

#ifdef _WIN32
	{
		InitVersion();

		if (IsWindowsXPOrGreater())
		{
			if (IsWindowsXPSP1OrGreater())
			{
				if (IsWindowsXPSP2OrGreater())
				{
					if (IsWindowsXPSP3OrGreater())
					{
						if (IsWindowsVistaOrGreater())
						{
							if (IsWindowsVistaSP1OrGreater())
							{
								if (IsWindowsVistaSP2OrGreater())
								{
									if (IsWindows7OrGreater())
									{
										if (IsWindows7SP1OrGreater())
										{
											if (IsWindows8OrGreater())
											{
												if (IsWindows8Point1OrGreater())
												{
													if (IsWindows10OrGreater())
													{
														result << std::string("Windows 10");
													}
													else
													{
														result << std::string("Windows 8.1");
													}
												}
												else
												{
													result << std::string("Windows 8");
												}
											}
											else
											{
												result << std::string("Windows 7 SP1");
											}
										}
										else
										{
											result << std::string("Windows 7");
										}
									}
									else
									{
										result << std::string("Vista SP2");
									}
								}
								else
								{
									result << std::string("Vista SP1");
								}
							}
							else
							{
								result << std::string("Vista");
							}
						}
						else
						{
							result << std::string("XP SP3");
						}
					}
					else
					{
						result << std::string("XP SP2");
					}
				}
				else
				{
					result << std::string("XP SP1");
				}
			}
			else
			{
				result << std::string("XP");
			}
		}

		if (IsWindowsServer())
		{
			result << std::string(" Server ");
		}
		else
		{
			result << std::string(" Client ");
		}

		result << std::string("Or Greater") << std::endl;

		result << std::endl;
	}
#endif

	return result.str();
}

const std::string hardware_info()
{
	std::stringstream result;

#ifdef _WIN32
	{
		SYSTEM_INFO siSysInfo;

		// Copy the hardware information to the SYSTEM_INFO structure. 

		GetSystemInfo(&siSysInfo);

		HKEY hKey = HKEY_LOCAL_MACHINE;
		const DWORD Const_Data_Size = 10000;
		TCHAR Data[Const_Data_Size];

		ZeroMemory(Data, Const_Data_Size * sizeof(TCHAR));

		DWORD buffersize = Const_Data_Size;

		LONG result_registry_functions = ERROR_SUCCESS;

		result_registry_functions = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("Hardware\\Description\\System\\CentralProcessor\\0\\"), 0, KEY_READ, &hKey);

		if (result_registry_functions == ERROR_SUCCESS)
		{
			// Query the registry value
			result_registry_functions = RegQueryValueEx(hKey, TEXT("ProcessorNameString"), NULL, NULL, (LPBYTE)&Data, &buffersize);

			if (result_registry_functions == ERROR_SUCCESS)
			{
				// Close the Registry Key
				result_registry_functions = RegCloseKey(hKey);

				assert(result_registry_functions == ERROR_SUCCESS);
			}
			else
			{
				assert(result_registry_functions == ERROR_SUCCESS);
			}
		}
		else
		{
			assert(result_registry_functions == ERROR_SUCCESS);
		}

		std::string ProcessorName(Data);

		// Display the contents of the SYSTEM_INFO structure. 

		result << std::endl;

		result << "Hardware information : " << std::endl;
		result << "  CPU Brand          : " << ProcessorName << std::endl;
		//result << "  CPU Architecture   : " << siSysInfo.wProcessorArchitecture << std::endl;
		result << "  CPU Core           : " << siSysInfo.dwNumberOfProcessors << std::endl;
		//result << "  Processor type     : " << siSysInfo.dwProcessorType << std::endl;

		// Used to convert bytes to MB
		const size_t local_1000_000 = 1000 * 1000;

		MEMORYSTATUSEX statex;

		statex.dwLength = sizeof(statex);

		GlobalMemoryStatusEx(&statex);

		result << "  Total RAM          : " << statex.ullTotalPhys / local_1000_000 << "MB" << std::endl;

		result << std::endl;
	}
#endif 

	return result.str();
}

const std::string cores_info()
{
	std::stringstream result;

#ifdef _WIN32
	{
		SYSTEM_INFO siSysInfo;

		// Copy the hardware information to the SYSTEM_INFO structure. 

		GetSystemInfo(&siSysInfo);

		result << std::endl;

		DWORD n = DWORD(std::thread::hardware_concurrency());
		result << "Test running " << n << " Cores\n";

		DWORD local_mask = siSysInfo.dwActiveProcessorMask;

		for (DWORD core_counter = 0; core_counter<n; core_counter++)
		{
			result << "Core " << core_counter << (((core_counter + 1) & local_mask) ? " ready\n" : " not ready\n");
		}

		result << std::endl;
	}
#endif 

	return result.str();
}


/// Debug functions used mainly to collect run-time statistics
static int64_t hits[2], means[2];

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

  static Mutex m;

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

void prefetch2(void* addr) {

  prefetch(addr);
  prefetch((uint8_t*)addr + 64);
}

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

  while (ptr->Size > 0 && byteOffset + ptr->Size <= returnLength)
  {
      if (ptr->Relationship == RelationNumaNode)
          nodes++;

      else if (ptr->Relationship == RelationProcessorCore)
      {
          cores++;
          threads += (ptr->Processor.Flags == LTP_PC_SMT) ? 2 : 1;
      }

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
