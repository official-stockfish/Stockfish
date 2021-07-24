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

#include <cstring>
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
    
    #define _XCR_XFEATURE_ENABLED_MASK  0

#else
#   message "No CPU-ID intrinsic defined for compiler."
#endif
#else
#   message "No CPU-ID intrinsic defined for processor architecture (currently only x86-32/64 is supported)."
#endif

bool CpuInfo::detect_OS_AVX() {

    bool avxSupported = false;

    if (OSXSAVE() && AVX())
    {
        uint64_t xcrFeatureMask = xgetbv(_XCR_XFEATURE_ENABLED_MASK);
        avxSupported = (xcrFeatureMask & 0x06) == 0x06;
    }

    return avxSupported;
}

bool CpuInfo::detect_OS_AVX512() {
    if (!detect_OS_AVX())
        return false;

    uint64_t xcrFeatureMask = xgetbv(_XCR_XFEATURE_ENABLED_MASK);
    return (xcrFeatureMask & 0xE6) == 0xE6;
}

std::string CpuInfo::get_info_string() {
    std::string s;

    s += "\nCPU Vendor: ";
    s += Vendor();
    s += "\n";

    s += "CPU Brand: ";
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
    s += "\n";

    s += "SIMD 128-bit: ";
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

    s += "SIMD 256-bit: ";
    if (AVX())         s += "AVX ";
    if (XOP())         s += "XOP ";
    if (FMA3())        s += "FMA3 ";
    if (FMA4())        s += "FMA4 ";
    if (AVX2())        s += "AVX2 ";
    s += "\n";

    s += "SIMD 512-bit: ";
    if (AVX512F())         s += "AVX512-F ";
    if (AVX512CD())        s += "AVX512-CD ";
    if (AVX512PF())        s += "AVX512-PF ";
    if (AVX512ER())        s += "AVX512-ER ";
    if (AVX512VL())        s += "AVX512-VL ";
    if (AVX512BW())        s += "AVX512-BW ";
    if (AVX512DQ())        s += "AVX512-DQ ";
    if (AVX512IFMA())      s += "AVX512-IFMA ";
    if (AVX512VBMI())      s += "AVX512-VBMI ";
    if (AVX512VPOPCNTDQ()) s += "AVX512-VPOPCNTDQ ";
    if (AVX5124FMAPS())    s += "AVX512-4FMAPS ";
    if (AVX5124VNNIW())    s += "AVX512-4VNNIW ";
    if (AVX512VBMI2())     s += "AVX512-VBMI2 ";
    if (AVX512VPCLMUL())   s += "AVX512-VPCLMUL ";
    if (AVX512VNNI())      s += "AVX512-VNNI ";
    if (AVX512BITALG())    s += "AVX512-BITALG ";
    s += "\n";

    return s;
}

} // namespace Stockfish
