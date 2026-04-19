#include <cpuid.h>
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

// AMD Zen/Zen+/Zen2 (family 17h) implement pdep/pext via microcode.
static bool has_slow_bmi2() {
    return __builtin_cpu_is("amd") && (__builtin_cpu_is("znver1") || __builtin_cpu_is("znver2"));
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
};


static CpuFeatures query_cpu_features() {
    return {
      .sse41           = (bool) __builtin_cpu_supports("sse4.1"),
      .popcnt          = (bool) __builtin_cpu_supports("popcnt"),
      .avx2            = (bool) __builtin_cpu_supports("avx2"),
      .bmi2            = (bool) __builtin_cpu_supports("bmi2"),
      .avx512f         = (bool) __builtin_cpu_supports("avx512f"),
      .avx512vl        = (bool) __builtin_cpu_supports("avx512vl"),
      .avx512bw        = (bool) __builtin_cpu_supports("avx512bw"),
      .avx512vnni      = (bool) __builtin_cpu_supports("avx512vnni"),
      .avx512ifma      = (bool) __builtin_cpu_supports("avx512ifma"),
      .avx512vbmi      = (bool) __builtin_cpu_supports("avx512vbmi"),
      .avx512vbmi2     = (bool) __builtin_cpu_supports("avx512vbmi2"),
      .avx512vpopcntdq = (bool) __builtin_cpu_supports("avx512vpopcntdq"),
      .avx512bitalg    = (bool) __builtin_cpu_supports("avx512bitalg"),
      .vpclmulqdq      = (bool) __builtin_cpu_supports("vpclmulqdq"),
      .gfni            = (bool) __builtin_cpu_supports("gfni"),
      .vaes            = (bool) __builtin_cpu_supports("vaes"),
      .avxvnni         = (bool) __builtin_cpu_supports("avxvnni"),
    };
}


// Selects the most capable ISA variant supported by this CPU and OS
static int dispatch(const CpuFeatures& f, int argc, char* argv[]) {
    if (!f.sse41 || !f.popcnt)
        return entry_x86_64(argc, argv);

    if (!f.avx2)
        return entry_x86_64_sse41_popcnt(argc, argv);

    if (!f.bmi2 || has_slow_bmi2())
        return entry_x86_64_avx2(argc, argv);

    if (!f.avx512f || !f.avx512vl || !f.avx512bw)
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
    __builtin_cpu_init();
    CpuFeatures features = query_cpu_features();
    return dispatch(features, argc, argv);
}