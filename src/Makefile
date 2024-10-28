# Stockfish, a UCI chess playing engine derived from Glaurung 2.1
# Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)
#
# Stockfish is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Stockfish is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

default: help

.PHONY: default help strip install clean objclean profileclean net format \
        all config-sanity analyze build profile-build \
        gcc-profile-make gcc-profile-use \
        clang-profile-make clang-profile-use \
        icx-profile-make icx-profile-use

VPATH = syzygy:nnue:nnue/features

SRCS := \
    benchmark.cpp bitboard.cpp engine.cpp evaluate.cpp main.cpp memory.cpp \
    misc.cpp movegen.cpp movepick.cpp position.cpp score.cpp search.cpp \
    thread.cpp timeman.cpp tt.cpp tune.cpp uci.cpp ucioption.cpp \
    nnue/network.cpp nnue/nnue_misc.cpp nnue/features/half_ka_v2_hm.cpp \
    syzygy/tbprobe.cpp

HEADERS := \
    benchmark.h bitboard.h common.h engine.h evaluate.h memory.h misc.h \
    movegen.h movepick.h numa.h perft.h position.h score.h search.h thread.h \
    thread_win32_osx.h timeman.h tt.h tune.h types.h uci.h ucioption.h \
    nnue/network.h nnue/nnue_accumulator.h nnue/nnue_architecture.h \
    nnue/nnue_common.h nnue/nnue_feature_transformer.h nnue/nnue_misc.h \
    nnue/features/half_ka_v2_hm.h nnue/layers/affine_transform.h \
    nnue/layers/clipped_relu.h nnue/layers/sqr_clipped_relu.h \
    syzygy/tbprobe.h \
    arch/i386/arch.h \
    arch/i386/nnue/layers/affine_transform.h \
    arch/i386/nnue/layers/affine_transform_sparse_input.h \
    arch/i386/nnue/layers/clipped_relu.h \
    arch/i386/nnue/layers/sqr_clipped_relu.h \
    arch/i386/nnue/nnue_feature_transformer.h \
    arch/arm/arch.h \
    arch/arm/nnue/layers/affine_transform.h \
    arch/arm/nnue/layers/affine_transform_sparse_input.h \
    arch/arm/nnue/layers/clipped_relu.h \
    arch/arm/nnue/layers/sqr_clipped_relu.h \
    arch/arm/nnue/nnue_feature_transformer.h \
    arch/generic/arch.h \
    arch/generic/nnue/layers/affine_transform.h \
    arch/generic/nnue/layers/clipped_relu.h \
    arch/generic/nnue/layers/sqr_clipped_relu.h \
    arch/generic/nnue/nnue_feature_transformer.h

OBJS := $(notdir $(SRCS:.cpp=.o))

INSTALL_PREFIX := /usr/local
INSTALL_PATH   := $(INSTALL_PREFIX)/bin

ifeq ($(OS),Windows_NT)
    INSTALL_EXE := stockfish.exe
else
    INSTALL_EXE := stockfish
endif

strip:
	-@test -f stockfish && strip stockfish
	-@test -f stockfish.exe && strip stockfish.exe

install:
	mkdir -p -m 755 $(INSTALL_PATH)
	cp $(INSTALL_EXE) $(INSTALL_PATH)
	strip $(INSTALL_PATH)/$(INSTALL_EXE)

clean: objclean profileclean
	@rm -f .depend

objclean:
	@rm -f stockfish stockfish.exe $(OBJS) *.o.tmp

profileclean:
	@rm -f PGOBENCH.out
	@rm -rf profdir
	@rm -f stockfish.profdata *.profraw

net:
	@$(SHELL) ../scripts/net.sh

format: CLANG_FORMAT := $(shell command -v clang-format-18 2> /dev/null || \
                                command -v clang-format 2> /dev/null)
format:
	@test -n "$(CLANG_FORMAT)" || ( \
	  echo "clang-format not found. Please install clang-format-18."; false \
	)
	@$(CLANG_FORMAT) -i $(SRCS) $(HEADERS) -style=file

### ==========================================================================

CXX_REQUIRED_RULES := analyze config-sanity build profile-build all \
                      gcc-profile-make gcc-profile-use \
                      clang-profile-make clang-profile-use \
                      icx-profile-make icx-profile-use

ifeq ($(MAKELEVEL),0)
ifneq ($(filter $(MAKECMDGOALS),$(CXX_REQUIRED_RULES)),)

optimize = yes
debug = no
sanitize = none

ifeq ($(shell command -v $(CXX) 2> /dev/null),)
    $(error Compiler $(CXX) not found)
endif

define test-compiler-macro
$(shell echo | $(CXX) -dM -x c++ -E - | \
        grep -E "^#define[[:space:]]+$(1)$|([[:space:]]+.*)" > /dev/null 2>&1 && echo 1)
endef

define get-compiler-macro
$(shell echo | $(CXX) -dM -x c++ -E - | \
        grep -E "^#define[[:space:]]+$(1)$|([[:space:]]+.*)" | \
        sed "s/^#define[[:space:]]\{1,\}$(1)[[:space:]]\{1,\}\+//")
endef

### 1. Detect compiler type

ifeq ($(call test-compiler-macro,__GNUC__),1)
    ifeq ($(call test-compiler-macro,__INTEL_LLVM_COMPILER),1)
        $(info Using Intel oneAPI DPC++/C++ Compiler) $(info )
        COMP := icx
        profile_make = icx-profile-make
        profile_use  = icx-profile-use
    else ifeq ($(call test-compiler-macro,__clang__),1)
        $(info Using LLVM C/C++ Compiler (Clang)) $(info )
        COMP := clang
        CLANG_VERSION := $(call get-compiler-macro,__clang_major__)
        ifneq ($(findstring Apple,$(call get-compiler-macro,__VERSION__)),)
            CLANG_APPLE := y
            LLVM_PROFDATA := xcrun llvm-profdata
        else
            LLVM_PROFDATA := $(shell command -v llvm-profdata-$(CLANG_VERSION) 2> /dev/null || \
								     command -v llvm-profdata 2> /dev/null)
        endif
        profile_make = clang-profile-make
        profile_use  = clang-profile-use
        export LLVM_PROFDATA
    else
        $(info Using GNU C/C++ Compiler) $(info )
        COMP := gcc
        GCC_VERSION := $(call get-compiler-macro,__GNUC__)
        profile_make = gcc-profile-make
        profile_use  = gcc-profile-use
    endif
endif

ifneq ($(filter $(COMP),gcc clang),)
    MINGW := $(call test-compiler-macro,__MINGW32__)
endif

ifeq ($(MINGW),1)
    EXE = stockfish.exe
else
    EXE = stockfish
endif

export COMP MINGW EXE

### 2. Set compiler options

# GNU C Compiler
# https://gcc.gnu.org/onlinedocs/gcc/Option-Index.html
#
# Clang Compiler
# https://clang.llvm.org/docs/ClangCommandLineReference.html
# https://clang.llvm.org/docs/DiagnosticsReference.html
#
# Intel oneAPI DPC++/C++ Compiler
# https://www.intel.com/content/www/us/en/docs/dpcpp-cpp-compiler/developer-guide-reference/2024-2/alphabetical-option-list.html

### 2.1. Common options

SF_CXXFLAGS := -std=c++17 -I. -Wall -DUSE_PTHREADS
SF_LDFLAGS  :=

SF_LIBS := pthread

### 2.2. Compiler-specific options

ifeq ($(COMP),gcc)
    SF_CXXFLAGS += -pedantic -Wextra -Wcast-qual -Wmissing-declarations \
                   -Wshadow
else ifeq ($(COMP),clang)
    SF_CXXFLAGS += -pedantic -Wextra -Wcast-qual -Wconditional-uninitialized \
                   -Wmissing-prototypes -Wshadow
else ifeq ($(COMP),icx)
    SF_CXXFLAGS += -Wabi -Wmissing-declarations -Wmissing-prototypes -Wshadow
endif

### 2.3. Optimization options

ifeq ($(optimize),yes)
    SF_CXXFLAGS += -O3

    ifeq ($(COMP),gcc)
        SF_CXXFLAGS += -funroll-loops
        ifeq ($(shell expr $(GCC_VERSION) \< 12),1)
            SF_CXXFLAGS += -flto
            SF_LDFLAGS  += -flto
        else
            SF_CXXFLAGS += -flto=jobserver
            SF_LDFLAGS  += -flto=jobserver
        endif
        SF_CXXFLAGS += -flto-partition=one
        SF_LDFLAGS  += -flto-partition=one
    else ifeq ($(COMP),clang)
        SF_CXXFLAGS += -funroll-loops -flto=full
        SF_LDFLAGS  += -flto=full
        ifeq ($(shell expr $(CLANG_VERSION) \< 16),1)
            SF_CXXFLAGS += -fexperimental-new-pass-manager
        endif
	else ifeq ($(COMP),icx)
        SF_CXXFLAGS += -flto=full
        SF_LDFLAGS  += -flto=full
    endif
endif

### 2.4. Debug options

ifeq ($(debug),no)
    SF_CXXFLAGS += -DNDEBUG
else
    SF_CXXFLAGS += -g
endif

### 2.5. Sanitizer options

ifneq ($(sanitize),none)
    SF_CXXFLAGS += -g3 $(addprefix -fsanitize=,$(sanitize))
endif

### 2.6. Include Git commit hash and date

GIT_SHA := $(shell git rev-parse --short=8 HEAD 2> /dev/null)
ifneq ($(GIT_SHA),)
    SF_CXXFLAGS += -DGIT_SHA=$(GIT_SHA)
endif

GIT_DATE := $(shell git show -s --date=format:'%Y%m%d' --format=%cd HEAD 2> /dev/null)
ifneq ($(GIT_DATE),)
    SF_CXXFLAGS += -DGIT_DATE=$(GIT_DATE)
endif

### 2.7. Add flags based on target OS

ifeq ($(MINGW),1)
    SF_LDFLAGS += -static
endif

endif   # CXX_REQUIRED_RULES

### 3. Add flags from architecture-specific Makefile
###   Note that this section is not enclosed in the CXX_REQUIRED_RULES block;
###   Users shall be able to see the help text even when there is no compiler.

ifeq ($(ARCH),)
    override ARCH := native
endif

ifeq ($(ARCH),native)
    # -march=native is not available on some architectures.
    # Need some automatic detection here.
    ARCH_NATIVE := y
    SF_CXXFLAGS += -march=native -DARCH_NATIVE
else
    ifneq ($(filter x86%,$(ARCH)),)
        ARCH_FAMILY := i386
    else ifneq ($(filter arm%,$(ARCH)),)
        ARCH_FAMILY := arm
    else ifneq ($(filter ppc%,$(ARCH)),)
        ARCH_FAMILY := ppc
    else
        ARCH_FAMILY := generic
    endif

    include ./arch/$(ARCH_FAMILY)/Makefile
endif

export ARCH

SF_CXXFLAGS += -DARCH=$(ARCH)

### 4. Extra flags for cross-compilation
###   Information of target architecture is needed here.

ifneq ($(filter $(MAKECMDGOALS),$(CXX_REQUIRED_RULES)),)

# Android NDK
ifneq ($(filter $(ARCH_FAMILY),i386 arm),)
    ifeq ($(call test-compiler-macro,__ANDROID__),1)
        SF_CXXFLAGS += -stdlib=libc++ -fPIE
        SF_LDFLAGS  += -static-libstdc++ -pie
        SF_LIBS     += m atomic
    endif
endif

# Link atomic library if not i386/arm family
ifneq ($(ARCH_NATIVE),y)
    ifeq ($(filter $(ARCH_FAMILY),i386 arm),)
        SF_LIBS += atomic
    endif
endif

endif   # CXX_REQUIRED_RULES
endif   # MAKELEVEL=0

SF_CXXFLAGS := $(strip $(SF_CXXFLAGS) $(CXXFLAGS))
SF_LDFLAGS  := $(strip $(SF_LDFLAGS) $(LDFLAGS))
SF_LIBS     := $(strip $(SF_LIBS) $(LIBS))

export SF_CXXFLAGS SF_LDFLAGS SF_LIBS

### ==========================================================================

define HELP_STRING
To see architecture-specific build options, run 'make help ARCH=<arch>'.
Currently supported values: x86, arm, generic

How-to-build examples:

    make profile-build

Build Stockfish with profile-guided optimization (PGO) for the current
architecture.

    make build ARCH=x86-64-avx2 CXX=clang++-18

Build Stockfish for the x86-64 architecture with AVX2/BMI2 support using clang++-18.

Check the Stockfish wiki for advanced build configuration.

endef
export HELP_STRING

# Print how-to-build help text if architecture is not set, otherwise
# list all available build presets for the selected architecture.
ifneq ($(ARCH_NATIVE),y)

help: help-arch

config-sanity: config-sanity-arch

else

help:
	@echo "$${HELP_STRING}"

endif

define CONFIG_SANITY_STRING

Build options:
  optimize: $(optimize)
  debug: $(debug)
  sanitize: $(sanitize)

Compiler options:
  CXX: $(CXX)
  CXXFLAGS: $(SF_CXXFLAGS)
  LDFLAGS: $(SF_LDFLAGS) $(SF_LIBS:%=-l%)

endef
export CONFIG_SANITY_STRING

config-sanity: net
	@[ "$(optimize)" = "yes" -o "$(optimize)" = "no" ]
	@[ "$(debug)" = "yes" -o "$(debug)" = "no" ]
	@[ ! -z "$(sanitize)" ]
	@echo "$${CONFIG_SANITY_STRING}"

analyze: config-sanity objclean
	@$(MAKE) -k --no-print-directory CXXFLAGS="" LDFLAGS="" $(OBJS)

build: config-sanity
	@$(MAKE) --no-print-directory CXXFLAGS="" LDFLAGS="" all

profile-build: config-sanity objclean profileclean
	@echo "Step 1/4. Building instrumented executable ..."
	@$(MAKE) --no-print-directory CXXFLAGS="" LDFLAGS="" $(profile_make)
	@printf "\n%s\n" "Step 2/4. Running benchmark for pgo-build ..."
	@$(EMULATE) ./$(EXE) bench > PGOBENCH.out 2>&1
	@tail -n 4 PGOBENCH.out
	@printf "\n%s\n" "Step 3/4. Building optimized executable ..."
	@$(MAKE) --no-print-directory objclean
	@$(MAKE) --no-print-directory CXXFLAGS="" LDFLAGS="" $(profile_use)
	@printf "\n%s\n" "Step 4/4. Deleting profile data ..."
	@$(MAKE) --no-print-directory profileclean

all: $(EXE) .depend

$(EXE): $(OBJS)
	+$(CXX) $(SF_LDFLAGS) -o $@ $(OBJS) $(SF_LIBS:%=-l%)

%.o: %.cpp
	+$(CXX) $(SF_CXXFLAGS) -c -o $@ $<

# Force recompilation to ensure version info is up-to-date
misc.o: FORCE
FORCE:

.depend: $(SRCS)
	-@$(CXX) $(SF_CXXFLAGS) -MM $(SRCS) > $@ && \
	  printf "%s\n\n" "Dependency updated, restarting Make..."

gcc-profile-make:
	@mkdir -p profdir
	@$(MAKE) --no-print-directory \
		CXXFLAGS="-fprofile-generate=profdir" LDFLAGS="" LIBS="gcov" all

gcc-profile-use:
	@$(MAKE) --no-print-directory \
		CXXFLAGS="-fprofile-use=profdir -fno-peel-loops -fno-tracer" LDFLAGS="" LIBS="gcov" all

clang-profile-make:
	@$(MAKE) --no-print-directory \
		CXXFLAGS="-fprofile-generate" LDFLAGS="-fprofile-generate" all

clang-profile-use:
	@$(LLVM_PROFDATA) merge -output=stockfish.profdata *.profraw
	@$(MAKE) --no-print-directory \
		CXXFLAGS="-fprofile-use=stockfish.profdata" \
		LDFLAGS="-fprofile-use=stockfish.profdata" \
		all

icx-profile-make:
	@$(MAKE) --no-print-directory \
		CXXFLAGS="-fprofile-generate" LDFLAGS="-fprofile-generate" all

icx-profile-use:
	@llvm-profdata merge *.profraw --output stockfish.profdata
	@$(MAKE) --no-print-directory \
		CXXFLAGS="-fprofile-use=stockfish.profdata" \
		LDFLAGS="-fprofile-use=stockfish.profdata" \
		all

ifneq ($(filter $(MAKECMDGOALS),$(CXX_REQUIRED_RULES)),)
-include .depend
endif 
