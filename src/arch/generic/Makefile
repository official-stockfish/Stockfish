define GENERIC_HELP_STRING
To build Stockfish, run the following command:

make <build-target> [ARCH=<preset>] [COMP=<compiler>]

Build presets for other architectures:

general-64              > 64-bit generic
general-32              > 32-bit generic

To see architecture-specific build options, run 'make help ARCH=<arch>'.
Currently supported values: x86, arm, generic

endef
export GENERIC_HELP_STRING

ifeq ($(COMP),$(filter $(COMP),gcc clang icx))

ifeq ($(ARCH),general-64)
    CXXFLAGS += -m64
else ifeq ($(ARCH),general-32)
    CXXFLAGS += -m32
endif

endif

help-arch:
	@echo "$${GENERIC_HELP_STRING}"

config-sanity-arch:
	@test "$(ARCH)" = "general-64" || \
	 test "$(ARCH)" = "general-32"
