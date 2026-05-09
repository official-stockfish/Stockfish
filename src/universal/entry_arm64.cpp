#include <stdint.h>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/auxv.h>
    #ifndef HWCAP_ASIMDDP
        #define HWCAP_ASIMDDP (1 << 20)
    #endif
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

DEFINE_BUILD(armv8)
DEFINE_BUILD(armv8_dotprod)

struct CpuFeatures {
    bool dotprod;  // ASIMD dot product (ARMv8.2-A FEAT_DotProd)
};


static CpuFeatures query_cpu_features() {
#if defined(_WIN32)
    return {
      .dotprod = (bool) IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE),
    };
#else
    unsigned long hwcap = getauxval(AT_HWCAP);
    return {
      .dotprod = (bool) (hwcap & HWCAP_ASIMDDP),
    };
#endif
}


// Selects the most capable ISA variant supported by this CPU and OS
static int dispatch(const CpuFeatures& f, int argc, char* argv[]) {
    if (!f.dotprod)
        return entry_armv8(argc, argv);

    return entry_armv8_dotprod(argc, argv);
}

int main(int argc, char* argv[]) {
    CpuFeatures features = query_cpu_features();
    return dispatch(features, argc, argv);
}
