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
    if (OSXSAVE() && AVX()) {
        // check OS has enabled XMM and YMM state support (necessary for AVX and AVX2)
        return (xcrFeatureMask() & 0x06) == 0x06;
    }
    return false;
}

bool CpuInfo::osAVX2() {
    if (osAVX()) {
        return AVX2();
    }
    return false;
}

bool CpuInfo::osAVX512() {
    if (osAVX() && AVX512F() && AVX512BW()) {
        // check OS has enabled XMM, YMM and ZMM state support (necessary for AVX-512)
        return (xcrFeatureMask() & 0xE6) == 0xE6;
    }
    return false;
}

std::string CpuInfo::infoString() {
    std::string s;
    bool fs = true; // full set of featues supported?

    s += "\nVendor : ";
    s += vendor();
    s += ", Family: ";
    s += std::to_string(CPUID._family);
    s += ", Model: ";
    s += std::to_string(CPUID._model);
    s += ", Stepping: ";
    s += std::to_string(CPUID._stepping);
    s += "\n";
    s += "Brand  : ";
    s += brand();
    s += "\nCPU    : ";
    if (X64())    { s += "64bit "; }  else { s += "[64bit] "; fs = false; }
    if (MMX())    { s += "MMX "; }    else { s += "[MMX] "; fs = false; }
    if (SSE())    { s += "SSE "; }    else { s += "[SSE] "; fs = false; }
    if (SSE2())   { s += "SSE2 "; }   else { s += "[SSE2] "; fs = false; }
    if (SSE3())   { s += "SSE3 "; }   else { s += "[SSE3] "; fs = false; }
    if (SSSE3())  { s += "SSSE3 "; }  else { s += "[SSSE3] "; fs = false; }
    if (SSE41())  { s += "SSE4.1 "; } else { s += "[SSE4.1] "; fs = false; }
    if (POPCNT()) { s += "POPCNT "; } else { s += "[POPCNT] "; fs = false; }
    if (AVX())    { s += "AVX "; }    else { s += "[AVX] "; fs = false; }
    if (AVX2())   { s += "AVX2 "; }   else { s += "[AVX2] "; fs = false; }
    if (BMI2())   { isAMDBeforeZen3() ? s += "BMI2(slow PEXT)" : s += "BMI2"; } else { s += "[BMI2]"; fs = false; }
    s += "\n         ";
    if (AVX512F())    { s += "AVX-512F "; }   else { s += "[AVX-512F] "; fs = false; }
    if (AVX512DQ())   { s += "AVX-512DQ "; }  else { s += "[AVX-512DQ] "; fs = false; }
    if (AVX512BW())   { s += "AVX-512BW "; }  else { s += "[AVX-512BW] "; fs = false; }
    if (AVX512VL())   { s += "AVX-512VL "; }  else { s += "[AVX-512VL] "; fs = false; }
    if (AVX512VNNI()) { s += "AVX-512VNNI"; } else { s += "[AVX-512VNNI]"; fs = false; }
    s += "\nOS     : ";
    if (osAVX())    { s += "AVX "; }    else { s += "[AVX] "; fs = false; }
    if (osAVX2())   { s += "AVX2 "; }   else { s += "[AVX2] "; fs = false; }
    if (osAVX512()) { s += "AVX-512"; } else { s += "[AVX-512]"; fs = false; }
    fs ? s += "\nAll features are supported by your CPU and OS.\n" :
         s += "\nValues in brackets mean that this feature is not supported by your CPU or OS.\n";

    return s;
}

} // namespace Stockfish
