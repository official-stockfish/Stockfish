define HELP_STRING_ARM
To build Stockfish, run the following command:

make <build-target> [ARCH=<preset>] [CPU=<cpu>]

Build presets for ARM/AArch64 architecture:

armv8-dotprod           > ARMv8.2-A 64-bit, Neon with DotProd feature
armv8                   > ARMv8-A 64-bit, Neon (Advanced SIMD)
armv7-neon              > ARMv7-A 32-bit, Neon (Advanced SIMD)
armv7                   > ARMv7-A 32-bit, no SIMD support

Stockfish does not support AArch32 targets.
Stockfish does not support non-A profile architectures.

endef
export HELP_STRING_ARM

ifneq ($(filter $(COMP),gcc clang icx),)

ifeq ($(ARCH),armv8-dotprod)
    SF_CXXFLAGS += -march=armv8.2-a+dotprod
else ifeq ($(ARCH),armv8)
    SF_CXXFLAGS += -march=armv8-a
else ifeq ($(ARCH),armv7-neon)
    SF_CXXFLAGS += -march=armv7-a -mfpu=neon -mfloat-abi=softfp
else ifeq ($(ARCH),armv7)
    SF_CXXFLAGS += -march=armv7-a
endif

endif   # gcc clang icx

.PHONY: help-arch config-sanity-arch

help-arch:
	@echo "$${HELP_STRING_ARM}"

config-sanity-arch:
	@[ "$(ARCH)" = "armv8-dotprod" -o \
	   "$(ARCH)" = "armv8" -o \
	   "$(ARCH)" = "armv7-neon" -o \
	   "$(ARCH)" = "armv7" \
	]
