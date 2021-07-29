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

# elif defined(__GNUC__) || defined(__clang__)

#include <cpuid.h>

    void CpuInfo::cpuid(int32_t out[4], int32_t eax, int32_t ecx) {
        __cpuid_count(eax, ecx, out[0], out[1], out[2], out[3]);
    }

#else
#   message "No CPU-ID intrinsic defined for compiler."
#endif
#else
#   message "No CPU-ID intrinsic defined for processor architecture (currently only x86-32/64 is supported)."
#endif

bool CpuInfo::osAVX() {
    if (OSXSAVE() && AVX())
    {
        // Check OS has enabled both XMM and YMM state support. Necessary for AVX and AVX2.
        return (xcrFeatureMask() & 0x06) == 0x06;
    }
    return false;
}

bool CpuInfo::osAVX2() {
    if (osAVX())
    {
        return AVX2();
    }
    return false;
}

bool CpuInfo::osAVX512() {
    if (osAVX() && AVX512F() && AVX512BW())
    {
        // Check for OS-support of ZMM and YMM state. Necessary for AVX-512.
        return (xcrFeatureMask() & 0xE6) == 0xE6;
    }
    return false;
}

std::string CpuInfo::infoString() {
    std::string s;

    s += "\nVendor: ";
    s += vendor();
    s += ", Family: ";
    s += std::to_string(CPUID._family);
    s += ", Model: ";
    s += std::to_string(CPUID._model);
    s += ", Stepping: ";
    s += std::to_string(CPUID._stepping);
    s += "\n";
    s += "Brand: ";
    s += brand();
    s += "\n";

    s += "Hardware Features: ";
    if (X64())         s += "64bit ";
    if (MMX())         s += "MMX ";
    if (SSE())         s += "SSE ";
    if (SSE2())        s += "SSE2 ";
    if (SSE3())        s += "SSE3 ";
    if (SSSE3())       s += "SSSE3 ";
    if (SSE41())       s += "SSE4.1 ";
    if (POPCNT())      s += "POPCNT ";
    if (AVX())         s += "AVX ";
    if (AVX2())        s += "AVX2 ";
    if (BMI2())        s += "BMI2 ";

    s += "\n";

    s += "OS Supported Features: ";
    (osAVX())    ? s += "AVX = yes, "   : s += "AVX = no, ";
    (osAVX2())   ? s += "AVX2 = yes, "  : s += "AVX2 = no, ";
    (osAVX512()) ? s += "AVX-512 = yes" : s += "AVX-512 = no";
    s += "\n";

    return s;
}

} // namespace Stockfish
