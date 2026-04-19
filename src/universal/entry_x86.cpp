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

// Returns true if this CPU is an AMD vendor
static bool is_amd() {
    unsigned int eax, ebx, ecx, edx;
    __cpuid(0, eax, ebx, ecx, edx);
    return ebx == 0x68747541 && edx == 0x69746e65 && ecx == 0x444d4163;  // "AuthenticAMD"
}

// Returns the CPU's extended family number from CPUID leaf 1
static unsigned get_cpu_family() {
    unsigned int eax, ebx, ecx, edx;
    __cpuid(1, eax, ebx, ecx, edx);
    // Base family is bits[11:8]; if 0xF, add extended family bits[27:20]
    unsigned family = (eax >> 8) & 0xf;
    if (family == 0xf)
        family += (eax >> 20) & 0xff;
    return family;
}

// AMD Zen/Zen+/Zen2 (family 17h) implement pdep/pext via microcode.
static bool has_slow_bmi2() { return is_amd() && get_cpu_family() == 0x17; }

// Reads XCR0 via XGETBV to check which extended register states the OS saves.
// CPUID feature bits alone don't guarantee the OS saves/restores them on context switches.
static uint64_t read_xcr0() {
    uint32_t xcr0_eax, xcr0_edx;
    asm("xgetbv" : "=a"(xcr0_eax), "=d"(xcr0_edx) : "c"(0U));
    return (static_cast<uint64_t>(xcr0_edx) << 32) | xcr0_eax;
}

struct CpuFeatures {
    bool sse41;            // SSE4.1
    bool popcnt;           // POPCNT
    bool avx2;             // AVX2
    bool bmi2;             // BMI2 (may be slow on AMD Zen/Zen+/Zen2)
    bool avx512f;          // AVX-512 Foundation
    bool avx512vl;         // AVX-512 Vector Length extensions
    bool avx512bw;         // AVX-512 Byte and Word instructions
    bool avx512vnni;       // AVX-512 Vector Neural Network Instructions
    bool avx512ifma;       // AVX-512 Integer Fused Multiply-Add
    bool avx512vbmi;       // AVX-512 Vector Bit Manipulation Instructions
    bool avx512vbmi2;      // AVX-512 VBMI2
    bool avx512vpopcntdq;  // AVX-512 VPOPCNTDQ
    bool avx512bitalg;     // AVX-512 BITALG
    bool vpclmulqdq;       // Carry-less multiplication (AVX512 variant)
    bool gfni;             // Galois Field instructions
    bool vaes;             // AES instructions (AVX512 variant)
    bool avxvnni;          // AVX-VNNI (non-512 dot product instructions)
    bool os_avx;           // OS saves/restores YMM state (XCR0 bits 1+2)
    bool os_avx512;        // OS saves/restores ZMM state (XCR0 bits 1,2,5,6,7)
};

// Queries all relevant CPUID leaves and XCR0 to populate a CpuFeatures struct
static CpuFeatures query_cpu_features() {
    CpuFeatures  f = {};
    unsigned int eax, ebx, ecx, edx;

    if (__get_cpuid_max(0, &eax) < 1)
        return f;

    // Leaf 1: basic feature flags
    __cpuid(1, eax, ebx, ecx, edx);
    f.sse41  = (ecx & (1U << 19)) != 0;
    f.popcnt = (ecx & (1U << 23)) != 0;

    // OSXSAVE (bit 27) means we can call XGETBV to check OS register support
    if (ecx & (1U << 27))
    {
        static constexpr uint64_t XCR0_SSE_AVX_MASK = 0x06;
        static constexpr uint64_t XCR0_AVX512_MASK  = 0xE6;

        uint64_t xcr0 = read_xcr0();
        f.os_avx      = (xcr0 & XCR0_SSE_AVX_MASK) == XCR0_SSE_AVX_MASK;
        f.os_avx512   = (xcr0 & XCR0_AVX512_MASK) == XCR0_AVX512_MASK;
    }

    // Leaf 7.0: structured extended feature flags
    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
        return f;

    f.avx2            = (ebx & (1U << 5)) != 0;
    f.bmi2            = (ebx & (1U << 8)) != 0;
    f.avx512f         = (ebx & (1U << 16)) != 0;
    f.avx512ifma      = (ebx & (1U << 21)) != 0;
    f.avx512vl        = (ebx & (1U << 31)) != 0;
    f.avx512bw        = (ebx & (1U << 30)) != 0;
    f.avx512vbmi      = (ecx & (1U << 1)) != 0;
    f.avx512vbmi2     = (ecx & (1U << 6)) != 0;
    f.gfni            = (ecx & (1U << 8)) != 0;
    f.vaes            = (ecx & (1U << 9)) != 0;
    f.vpclmulqdq      = (ecx & (1U << 10)) != 0;
    f.avx512vnni      = (ecx & (1U << 11)) != 0;
    f.avx512bitalg    = (ecx & (1U << 12)) != 0;
    f.avx512vpopcntdq = (ecx & (1U << 14)) != 0;

    // Leaf 7.1: additional extended feature flags
    if (__get_cpuid_count(7, 1, &eax, &ebx, &ecx, &edx))
        f.avxvnni = (eax & (1U << 4)) != 0;

    return f;
}

// Selects the most capable ISA variant supported by this CPU and OS
static int dispatch(const CpuFeatures& f, int argc, char* argv[]) {
    if (!f.sse41 || !f.popcnt)
        return entry_x86_64(argc, argv);

    if (!f.avx2 || !f.os_avx)
        return entry_x86_64_sse41_popcnt(argc, argv);

    if (!f.bmi2 || has_slow_bmi2())
        return entry_x86_64_avx2(argc, argv);

    if (!f.avx512f || !f.avx512vl || !f.avx512bw || !f.os_avx512)
    {
        if (f.avxvnni)
            return entry_x86_64_avxvnni(argc, argv);
        return entry_x86_64_bmi2(argc, argv);
    }

    if (!f.avx512vnni)
        return entry_x86_64_avx512(argc, argv);

    // AVX512ICL requires the full Icelake-client feature suite
    if (!f.avx512ifma          //
        || !f.avx512vbmi       //
        || !f.avx512vbmi2      //
        || !f.avx512vpopcntdq  //
        || !f.avx512bitalg     //
        || !f.vpclmulqdq       //
        || !f.gfni             //
        || !f.vaes             //
    )
        return entry_x86_64_vnni512(argc, argv);

    return entry_x86_64_avx512icl(argc, argv);
}

int main(int argc, char* argv[]) {
    CpuFeatures features = query_cpu_features();
    return dispatch(features, argc, argv);
}