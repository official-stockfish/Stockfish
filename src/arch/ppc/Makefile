define HELP_STRING_PPC
To build Stockfish, run the following command:

make <build-target> [ARCH=<preset>]

Build presets for PowerPC architecture:

ppc-power10             > Power10
ppc-power9              > Power9
ppc-power8              > Power8
ppc-power7              > Power7
ppc-64                  > PowerPC 64-bit
ppc-32                  > PowerPC 32-bit

endef
export HELP_STRING_PPC

ifneq ($(filter $(COMP),gcc clang icx),)

ifeq ($(ARCH),ppc-power10)
    SF_CXXFLAGS += -mcpu=power10
else ifeq ($(ARCH),ppc-power9)
    SF_CXXFLAGS += -mcpu=power9
else ifeq ($(ARCH),ppc-power8)
    SF_CXXFLAGS += -mcpu=power8
else ifeq ($(ARCH),ppc-power7)
    SF_CXXFLAGS += -mcpu=power7
else ifeq ($(ARCH),ppc-64)
    SF_CXXFLAGS += -mcpu=powerpc64
else ifeq ($(ARCH),ppc-32)
    SF_CXXFLAGS += -mcpu=powerpc
endif

endif   # gcc clang icx

.PHONY: help-arch config-sanity-arch

help-arch:
	@echo "$${HELP_STRING_PPC}"

config-sanity-arch:
	@[ "$(ARCH)" = "ppc-power10" -o \
	   "$(ARCH)" = "ppc-power9" -o \
	   "$(ARCH)" = "ppc-power8" -o \
	   "$(ARCH)" = "ppc-power7" -o \
	   "$(ARCH)" = "ppc-64" -o \
	   "$(ARCH)" = "ppc-32" \
	]
