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

#include <stdint.h>

#ifdef __linux__
    #include <sys/auxv.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#else
    #error This file is only supported on Linux
#endif

#define DEFINE_BUILD(x) \
    namespace Stockfish_##x { \
        extern int main(int argc, char* argv[]); \
    } \
    extern "C" void (*__start_##x##_init[])(void); \
    extern "C" void (*__stop_##x##_init[])(void); \
    int entry_##x(int argc, char* argv[]) { \
        unsigned count = __stop_##x##_init - __start_##x##_init; \
        for (unsigned i = 0; i < count; i++) \
            __start_##x##_init[i](); \
        return Stockfish_##x::main(argc, argv); \
    }

DEFINE_BUILD(riscv64)
DEFINE_BUILD(riscv64_rva23)

struct CpuFeatures {
    bool gcv;        // GCV
    bool zbitmanip;  // Zba + Zbb + Zbs
    bool zicond;     // Zicond
    int  vlen;       // vector length in bits (valid only if gcv is true), always a power of two
};

static CpuFeatures query_cpu_features() {
    unsigned long  hwcap    = getauxval(AT_HWCAP);
    constexpr long MASK_GCV = 0x20112d;

    bool gcv  = (hwcap & MASK_GCV) == MASK_GCV;
    int  vlen = 0;
    if (gcv)
    {
        unsigned long vlenb;
        asm volatile("csrr %0, 0xc22" : "=r"(vlenb));  // 0xc22 = vlenb
        vlen = int(vlenb * 8);
    }

    constexpr long     NR_riscv_hwprobe = 258;
    constexpr int64_t  KEY_IMA_EXT_0    = 4;
    constexpr uint64_t EXT_ZB           = 1 << 3 | 1 << 4 | 1 << 5;
    constexpr uint64_t EXT_ZICOND       = uint64_t(1) << 35;

    struct {
        int64_t  key;
        uint64_t value;
    } pair = {KEY_IMA_EXT_0, 0};

    long rc        = syscall(NR_riscv_hwprobe, &pair, 1UL, 0UL, nullptr, 0U);
    bool zbitmanip = false, zicond = false;

    if (rc == 0 && pair.key == KEY_IMA_EXT_0)
    {
        zbitmanip = (pair.value & EXT_ZB) == EXT_ZB;
        zicond    = (pair.value & EXT_ZICOND) == EXT_ZICOND;
    }

    return CpuFeatures{.gcv = gcv, .zbitmanip = zbitmanip, .zicond = zicond, .vlen = vlen};
}


// Selects the most capable ISA variant supported by this CPU
static int dispatch(const CpuFeatures& f, int argc, char* argv[]) {
    if (!f.gcv || !f.zbitmanip || !f.zicond || f.vlen < 128)
        return entry_riscv64(argc, argv);

    return entry_riscv64_rva23(argc, argv);
}

int main(int argc, char* argv[]) {
    CpuFeatures f = query_cpu_features();
    return dispatch(f, argc, argv);
}
