#include <cpuid.h>
#include <stdio.h>
#include <stdint.h>

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

DEFINE_BUILD(x86_64)
DEFINE_BUILD(x86_64_sse41_popcnt)
DEFINE_BUILD(x86_64_avx2)
DEFINE_BUILD(x86_64_bmi2)
DEFINE_BUILD(x86_64_avxvnni)
DEFINE_BUILD(x86_64_avx512)
DEFINE_BUILD(x86_64_vnni512)
DEFINE_BUILD(x86_64_avx512icl)

// Zen, Zen+ and Zen 2 (AMD family 17h) have microcoded pdep/pext
static bool has_slow_bmi2() {
    unsigned int eax, ebx, ecx, edx;
    __cpuid(0, eax, ebx, ecx, edx);
    if (ebx != 0x68747541 || edx != 0x69746e65 || ecx != 0x444d4163)  // "AuthenticAMD"
        return false;
    __cpuid(1, eax, ebx, ecx, edx);
    unsigned family = (eax >> 8) & 0xf;
    if (family == 0xf)
        family += (eax >> 20) & 0xff;
    return family == 0x17;
}

constexpr uint64_t XCR0_SSE_AVX_MASK = 0x06;
constexpr uint64_t XCR0_AVX512_MASK  = 0xE6;

int main(int argc, char* argv[]) {
    unsigned _;
    unsigned max_leaf = __get_cpuid_max(0, &_);
    if (max_leaf < 1U)
    {
        return entry_x86_64(argc, argv);
    }
    unsigned int eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    if (!(ecx & (1U << 19)) || !(ecx & (1U << 23)))
    {  // no popcnt or no sse4.1
        return entry_x86_64(argc, argv);
    }

    bool     xgetbv_ok = (ecx & (1U << 27)) != 0;  // OSXSAVE
    uint64_t xcr0      = 0;

    if (xgetbv_ok)
    {
        uint32_t xcr0_eax, xcr0_edx;
        uint32_t xcr0_ecx = 0;
        asm("xgetbv" : "=a"(xcr0_eax), "=d"(xcr0_edx) : "c"(xcr0_ecx));
        xcr0 = (static_cast<uint64_t>(xcr0_edx) << 32) | xcr0_eax;
    }


    bool leaf_supported = __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
    if (!leaf_supported || !(ebx & (1U << 5)) || !xgetbv_ok
        || (xcr0 & XCR0_SSE_AVX_MASK) != XCR0_SSE_AVX_MASK)
    {
        // CPUID query not supported, no avx2, missing xgetbv, or missing OS restore for AVX regs
        return entry_x86_64_sse41_popcnt(argc, argv);
    }

    if (!(ebx & (1U << 8)) || has_slow_bmi2())
    {  // no or slow bmi2
        return entry_x86_64_avx2(argc, argv);
    }

    if (!(ebx & (1U << 16)) || !(ebx & (1U << 31)) || !(ebx & (1U << 30))
        || (xcr0 & XCR0_AVX512_MASK) != XCR0_AVX512_MASK)
    {
        // no avx512f/vl/bw, or OS doesn't restore AVX-512 regs
        bool leaf_supported = __get_cpuid_count(7, 1, &eax, &ebx, &ecx, &edx);
        if (leaf_supported && eax & (1U << 4))
        {  // avxvnni
            return entry_x86_64_avxvnni(argc, argv);
        }
        else
        {
            return entry_x86_64_bmi2(argc, argv);
        }
    }

    if (!(ecx & 1U << 11 /* vnni512 */))
    {
        return entry_x86_64_avx512(argc, argv);
    }

    if (!(ebx & 1U << 21 /* ifma */) || !(ecx & 1U << 1 /* vbmi */) || !(ecx & 1U << 6 /* vbmi2 */)
        || !(ecx & 1U << 14 /* vpopcntdq */) || !(ecx & 1U << 12 /* bitalg */)
        || !(ecx & 1U << 10 /* vpclmulqdq */) || !(ecx & 1U << 8 /* gfni */)
        || !(ecx & 1U << 9 /* vaes */))
    {
        return entry_x86_64_vnni512(argc, argv);
    }

    return entry_x86_64_avx512icl(argc, argv);
}
