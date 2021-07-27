/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

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

#include "cpuinfo.h"

namespace Stockfish {

// query CPU at runtime and initialize static member data
const CpuInfo::CpuId Stockfish::CpuInfo::CPUID;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#if _WIN32

#include <windows.h>
#include <intrin.h>

    void CpuInfo::cpuid(int32_t out[4], int32_t eax, int32_t ecx) {
        __cpuidex(out, eax, ecx);
    }

    uint64_t CpuInfo::xgetbv(unsigned int x) {
        return _xgetbv(x);
    }

# elif defined(__GNUC__) || defined(__clang__)

#include <cpuid.h>

    void CpuInfo::cpuid(int32_t out[4], int32_t eax, int32_t ecx) {
        __cpuid_count(eax, ecx, out[0], out[1], out[2], out[3]);
    }

    uint64_t CpuInfo::xgetbv(unsigned int index) {
        uint32_t eax, edx;
        __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
        return ((uint64_t)edx << 32) | eax;
    }

#else
#   message "No CPU-ID intrinsic defined for compiler."
#endif
#else
#   message "No CPU-ID intrinsic defined for processor architecture (currently only x86-32/64 is supported)."
#endif

#ifndef _XCR_XFEATURE_ENABLED_MASK
    #define _XCR_XFEATURE_ENABLED_MASK 0
#endif

bool CpuInfo::OS_AVX() {
    if (OSXSAVE() && AVX())
    {
        const uint64_t xcrFeatureMask = xgetbv(_XCR_XFEATURE_ENABLED_MASK);
        // check for OS-support of YMM state. Necessary for AVX and AVX2.
        return (xcrFeatureMask & 0x06) == 0x06;
    }
    return false;
}

bool CpuInfo::OS_AVX2() {
    if (OS_AVX())
    {
        return AVX2();
    }
    return false;
}

bool CpuInfo::OS_AVX512() {
    if (OS_AVX() && AVX512F() && AVX512DQ() && AVX512CD() && AVX512BW() && AVX512VL())
    {
        const uint64_t xcrFeatureMask = xgetbv(_XCR_XFEATURE_ENABLED_MASK);
        // Check for OS-support of ZMM and YMM state. Necessary for AVX-512.
        return (xcrFeatureMask & 0xE6) == 0xE6;
    }
    return false;
}

std::string CpuInfo::get_info_string() {
    std::string s;

    s += "\nVendor: ";
    s += Vendor();
    s += ", Family: ";
    s += std::to_string(CPUID._family);
    s += ", Model: ";
    s += std::to_string(CPUID._model);
    s += "\n";

    s += "Brand: ";
    s += Brand();
    s += "\n";

    s += "Hardware Features: ";
    if (X64())         s += "64bit ";
    if (MMX())         s += "MMX ";
    if (ABM())         s += "ABM ";
    if (RDRAND())      s += "RDRAND ";
    if (RDSEED())      s += "RDSEED ";
    if (BMI1())        s += "BMI1 ";
    if (BMI2())        s += "BMI2 ";
    if (ADX())         s += "ADX ";
    if (MPX())         s += "MPX ";
    if (PREFETCHWT1()) s += "PREFETCHWT1 ";
    if (RDPID())       s += "RDPID ";
    if (GFNI())        s += "GFNI ";
    if (VAES())        s += "VAES ";
    if (SSE())         s += "SSE ";
    if (SSE2())        s += "SSE2 ";
    if (SSE3())        s += "SSE3 ";
    if (SSSE3())       s += "SSSE3 ";
    if (SSE4a())       s += "SSE4a ";
    if (SSE41())       s += "SSE4.1 ";
    if (SSE42())       s += "SSE4.2 ";
    if (AES())         s += "AES-NI ";
    if (SHA())         s += "SHA ";
    s += "\n";

    s += "OS + Hardware Features: ";
    (OS_AVX())    ? s += "AVX = yes, "   : s += "AVX = no, ";
    (OS_AVX2())   ? s += "AVX2 = yes, "  : s += "AVX2 = no, ";
    (OS_AVX512()) ? s += "AVX-512 = yes" : s += "AVX-512 = no";
    s += "\n";

    return s;
}

} // namespace Stockfish
