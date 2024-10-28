define HELP_STRING_I386
To build Stockfish, run the following command:

make <build-target> [ARCH=<preset>] [NO_AVX512=1]

Build presets for x86 architecture:

x86-64-avx512-vnni      > 64-bit, AVX-512 with BW/VL/VNNI
x86-64-avx512           > 64-bit, AVX-512 with BW/VL
x86-64-avxvnni          > 64-bit, AVX2 with AVX-VNNI
x86-64-bmi2             > 64-bit, AVX2 and BMI2
x86-64-avx2             > 64-bit, AVX2
x86-64-avx              > 64-bit, AVX
x86-64-popcnt           > 64-bit, POPCNT with SSE4.2
x86-64-sse41            > 64-bit, SSE4.1
x86-64-ssse3            > 64-bit, SSSE3
x86-64                  > 64-bit generic (supports up to SSE2)
x86-32                  > 32-bit generic (supports up to SSE2)

Build presets for x86 architecture, AMD processors:

x86-64-bmi              > 64-bit, AVX with BMI
x86-64-abm              > 64-bit, SSE4a with ABM

x86 options:

    NO_AVX512

If NO_AVX512 is set, Stockfish will use 256-bit SIMD instructions instead of
512-bit ones. This option is only available if AVX-512 is enabled.

endef
export HELP_STRING_I386

# Compatibility layer for old Makefile presets
ifeq ($(ARCH),x86-64-vnni512)
    override ARCH := x86-64-avx512-vnni
endif

ifeq ($(ARCH),x86-64-vnni256)
    override ARCH := x86-64-avx512-vnni
    override NO_AVX512 := 1
endif

ifneq ($(filter $(COMP),gcc clang icx),)

ifeq ($(ARCH),x86-64-avx512-vnni)
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 \
                   -mpopcnt -mavx -mbmi -mavx2 -mbmi2 -mavx512f -mavx512bw \
                   -mavx512vl -mavx512vnni
    ifeq ($(NO_AVX512),1)
        SF_CXXFLAGS += -mprefer-vector-width=256 -DNO_AVX512
    endif
else ifeq ($(ARCH),x86-64-avx512)
    # Xeon Phi is no longer supported on future compiler versions.
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 \
                   -mpopcnt -mavx -mbmi -mavx2 -mbmi2 -mavx512f -mavx512bw \
                   -mavx512vl
    ifeq ($(NO_AVX512),1)
        SF_CXXFLAGS += -mprefer-vector-width=256 -DNO_AVX512
    endif
else ifeq ($(ARCH),x86-64-avxvnni)
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 \
                   -mpopcnt -mavx -mbmi -mavx2 -mbmi2 -mavxvnni
else ifeq ($(ARCH),x86-64-bmi2)
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 \
                   -mpopcnt -mavx -mbmi -mavx2 -mbmi2
else ifeq ($(ARCH),x86-64-avx2)
    # VIA Eden X4 does not support BMI/BMI2, need a custom profile if required.
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 \
                   -mpopcnt -mavx -mbmi -mavx2
else ifeq ($(ARCH),x86-64-bmi)
    # Some AMD processors (AMD family 15h) have BMI1 only.
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 \
                   -mpopcnt -mavx -mbmi
else ifeq ($(ARCH),x86-64-avx)
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 \
                   -mpopcnt -mavx
else ifeq ($(ARCH),x86-64-popcnt)
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 \
                   -mpopcnt
else ifeq ($(ARCH),x86-64-sse41)
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3 -msse4.1
else ifeq ($(ARCH),x86-64-ssse3)
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -mssse3
else ifeq ($(ARCH),x86-64-abm)
    SF_CXXFLAGS += -m64 -mmmx -msse -msse2 -msse3 -msse4a -mabm
else ifeq ($(ARCH),x86-64)
    # Modern compilers add SSE2 flag by default.
    SF_CXXFLAGS += -m64
else ifeq ($(ARCH),x86-32)
    SF_CXXFLAGS += -m32
endif

endif   # gcc clang icx

.PHONY: help-arch config-sanity-arch

help-arch:
	@echo "$${HELP_STRING_I386}"

config-sanity-arch:
	@[ "$(ARCH)" = "x86-64-avx512-vnni" -o \
	   "$(ARCH)" = "x86-64-avx512" -o \
	   "$(ARCH)" = "x86-64-avxvnni" -o \
	   "$(ARCH)" = "x86-64-bmi2" -o \
	   "$(ARCH)" = "x86-64-avx2" -o \
	   "$(ARCH)" = "x86-64-bmi" -o \
	   "$(ARCH)" = "x86-64-avx" -o \
	   "$(ARCH)" = "x86-64-popcnt" -o \
	   "$(ARCH)" = "x86-64-sse41" -o \
	   "$(ARCH)" = "x86-64-ssse3" -o \
	   "$(ARCH)" = "x86-64-abm" -o \
	   "$(ARCH)" = "x86-64" -o \
	   "$(ARCH)" = "x86-32" \
	]
