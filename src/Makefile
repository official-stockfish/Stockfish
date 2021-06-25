# Stockfish, a UCI chess playing engine derived from Glaurung 2.1
# Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)
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


### ==========================================================================
### Section 1. General Configuration
### ==========================================================================

### Executable name
ifeq ($(COMP),mingw)
EXE = stockfish.exe
else
EXE = stockfish
endif

### Installation dir definitions
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

### Built-in benchmark for pgo-builds
ifeq ($(SDE_PATH),)
	PGOBENCH = ./$(EXE) bench
else
	PGOBENCH = $(SDE_PATH) -- ./$(EXE) bench
endif

### Source and object files
SRCS = benchmark.cpp bitbase.cpp bitboard.cpp endgame.cpp evaluate.cpp main.cpp \
	material.cpp misc.cpp movegen.cpp movepick.cpp pawns.cpp position.cpp psqt.cpp \
	search.cpp thread.cpp timeman.cpp tt.cpp uci.cpp ucioption.cpp tune.cpp syzygy/tbprobe.cpp \
	nnue/evaluate_nnue.cpp nnue/features/half_ka_v2.cpp

OBJS = $(notdir $(SRCS:.cpp=.o))

VPATH = syzygy:nnue:nnue/features

### Establish the operating system name
KERNEL = $(shell uname -s)
ifeq ($(KERNEL),Linux)
	OS = $(shell uname -o)
endif

### ==========================================================================
### Section 2. High-level Configuration
### ==========================================================================
#
# flag                --- Comp switch      --- Description
# ----------------------------------------------------------------------------
#
# debug = yes/no      --- -DNDEBUG         --- Enable/Disable debug mode
# sanitize = none/<sanitizer> ... (-fsanitize )
#                     --- ( undefined )    --- enable undefined behavior checks
#                     --- ( thread    )    --- enable threading error checks
#                     --- ( address   )    --- enable memory access checks
#                     --- ...etc...        --- see compiler documentation for supported sanitizers
# optimize = yes/no   --- (-O3/-fast etc.) --- Enable/Disable optimizations
# arch = (name)       --- (-arch)          --- Target architecture
# bits = 64/32        --- -DIS_64BIT       --- 64-/32-bit operating system
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.
#
# Example of use for these flags:
# make build ARCH=x86-64 debug=on sanitize="address undefined"


### 2.1. General and architecture defaults

ifeq ($(ARCH),)
   ARCH = x86-64
   help_skip_sanity = yes
endif
# explicitly check for the list of supported architectures (as listed with make help),
# the user can override with `make ARCH=x86-32-vnni256 SUPPORTED_ARCH=true`
ifeq ($(ARCH), $(filter $(ARCH), \
                 x86-64 x86-32 ppc-64 ppc-32 e2k \
                 armv7 armv8 apple-silicon general-64 general-32))
   SUPPORTED_ARCH=true
else
   SUPPORTED_ARCH=false
endif

optimize = yes
debug = no
sanitize = none
bits = 64
STRIP = strip

### 2.2 Architecture specific

ifeq ($(findstring x86,$(ARCH)),x86)

# x86-32/64

ifeq ($(findstring x86-32,$(ARCH)),x86-32)
	arch = i386
	bits = 32
else
	arch = x86_64
endif

else

# all other architectures

ifeq ($(ARCH),general-32)
	arch = any
	bits = 32
endif

ifeq ($(ARCH),general-64)
	arch = any
endif

ifeq ($(ARCH),armv7)
	arch = armv7
	bits = 32
endif

ifeq ($(ARCH),armv8)
	arch = armv8
endif

ifeq ($(ARCH),apple-silicon)
	arch = arm64
endif

ifeq ($(ARCH),ppc-32)
	arch = ppc
	bits = 32
endif

ifeq ($(ARCH),ppc-64)
	arch = ppc64
endif

ifeq ($(findstring e2k,$(ARCH)),e2k)
	arch = e2k
	bits = 64
endif

endif

### ==========================================================================
### Section 3. Low-level Configuration
### ==========================================================================

### 3.1 Selecting compiler (default = gcc)
CXXFLAGS += -Wall -Wcast-qual -fno-exceptions -std=c++17 $(EXTRACXXFLAGS)
DEPENDFLAGS += -std=c++17
LDFLAGS += $(EXTRALDFLAGS)

ifeq ($(COMP),)
	COMP=gcc
endif

ifeq ($(COMP),gcc)
	comp=gcc
	CXX=g++
	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifeq ($(arch),$(filter $(arch),armv7 armv8))
		ifeq ($(OS),Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif

	ifeq ($(arch),$(filter $(arch),armv7))
		LDFLAGS += -latomic
	endif

	ifneq ($(KERNEL),Darwin)
	   LDFLAGS += -Wl,--no-as-needed
	endif
endif

ifeq ($(COMP),mingw)
	comp=mingw

	ifeq ($(KERNEL),Linux)
		ifeq ($(bits),64)
			ifeq ($(shell which x86_64-w64-mingw32-c++-posix),)
				CXX=x86_64-w64-mingw32-c++
			else
				CXX=x86_64-w64-mingw32-c++-posix
			endif
		else
			ifeq ($(shell which i686-w64-mingw32-c++-posix),)
				CXX=i686-w64-mingw32-c++
			else
				CXX=i686-w64-mingw32-c++-posix
			endif
		endif
	else
		CXX=g++
	endif

	CXXFLAGS += -Wextra -Wshadow
	LDFLAGS += -static
endif

ifeq ($(COMP),icc)
	comp=icc
	CXX=icpc
	CXXFLAGS += -diag-disable 1476,10120 -Wcheck -Wabi -Wdeprecated -strict-ansi
endif

ifeq ($(COMP),clang)
	comp=clang
	CXX=clang++
	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifneq ($(KERNEL),Darwin)
	ifneq ($(KERNEL),OpenBSD)
	ifneq ($(KERNEL),FreeBSD)
		LDFLAGS += -latomic
	endif
	endif
	endif

	ifeq ($(arch),$(filter $(arch),armv7 armv8))
		ifeq ($(OS),Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif
endif

ifeq ($(KERNEL),Darwin)
	CXXFLAGS += -arch $(arch) -mmacosx-version-min=10.14
	LDFLAGS += -arch $(arch) -mmacosx-version-min=10.14
	XCRUN = xcrun
endif

# To cross-compile for Android, NDK version r21 or later is recommended.
# In earlier NDK versions, you'll need to pass -fno-addrsig if using GNU binutils.
# Currently we don't know how to make PGO builds with the NDK yet.
ifeq ($(COMP),ndk)
	CXXFLAGS += -stdlib=libc++ -fPIE
	comp=clang
	ifeq ($(arch),armv7)
		CXX=armv7a-linux-androideabi16-clang++
		CXXFLAGS += -mthumb -march=armv7-a -mfloat-abi=softfp -mfpu=neon
		STRIP=arm-linux-androideabi-strip
	endif
	ifeq ($(arch),armv8)
		CXX=aarch64-linux-android21-clang++
		STRIP=aarch64-linux-android-strip
	endif
	LDFLAGS += -static-libstdc++ -pie -lm -latomic
endif

ifeq ($(comp),icc)
	profile_make = icc-profile-make
	profile_use = icc-profile-use
else ifeq ($(comp),clang)
	profile_make = clang-profile-make
	profile_use = clang-profile-use
else
	profile_make = gcc-profile-make
	profile_use = gcc-profile-use
endif

### Travis CI script uses COMPILER to overwrite CXX
ifdef COMPILER
	COMPCXX=$(COMPILER)
endif

### Allow overwriting CXX from command line
ifdef COMPCXX
	CXX=$(COMPCXX)
endif

### Sometimes gcc is really clang
ifeq ($(COMP),gcc)
	gccversion = $(shell $(CXX) --version)
	gccisclang = $(findstring clang,$(gccversion))
	ifneq ($(gccisclang),)
		profile_make = clang-profile-make
		profile_use = clang-profile-use
	endif
endif

### On mingw use Windows threads, otherwise POSIX
ifneq ($(comp),mingw)
	CXXFLAGS += -DUSE_PTHREADS
	# On Android Bionic's C library comes with its own pthread implementation bundled in
	ifneq ($(OS),Android)
		# Haiku has pthreads in its libroot, so only link it in on other platforms
		ifneq ($(KERNEL),Haiku)
			ifneq ($(COMP),ndk)
				LDFLAGS += -lpthread
			endif
		endif
	endif
endif

### 3.2.1 Debugging
ifeq ($(debug),no)
	CXXFLAGS += -DNDEBUG
else
	CXXFLAGS += -g
endif

### 3.2.2 Debugging with undefined behavior sanitizers
ifneq ($(sanitize),none)
        CXXFLAGS += -g3 $(addprefix -fsanitize=,$(sanitize))
        LDFLAGS += $(addprefix -fsanitize=,$(sanitize))
endif

### 3.3 Optimization
ifeq ($(optimize),yes)

	CXXFLAGS += -O3

	ifeq ($(comp),gcc)
		ifeq ($(OS), Android)
			CXXFLAGS += -fno-gcse -mthumb -march=armv7-a -mfloat-abi=softfp
		endif
	endif

	ifeq ($(comp),$(filter $(comp),gcc clang icc))
		ifeq ($(KERNEL),Darwin)
			CXXFLAGS += -mdynamic-no-pic
		endif
	endif

	ifeq ($(comp),clang)
		CXXFLAGS += -fexperimental-new-pass-manager
	endif
endif

### 3.4 Bits
ifeq ($(bits),64)
	CXXFLAGS += -DIS_64BIT
endif

### 3.5 Link Time Optimization
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(optimize),yes)
ifeq ($(debug), no)
	ifeq ($(comp),clang)
		CXXFLAGS += -flto
		ifneq ($(findstring MINGW,$(KERNEL)),)
			CXXFLAGS += -fuse-ld=lld
		else ifneq ($(findstring MSYS,$(KERNEL)),)
			CXXFLAGS += -fuse-ld=lld
		endif
		LDFLAGS += $(CXXFLAGS)

# GCC and CLANG use different methods for parallelizing LTO and CLANG pretends to be
# GCC on some systems.
	else ifeq ($(comp),gcc)
	ifeq ($(gccisclang),)
		CXXFLAGS += -flto
		LDFLAGS += $(CXXFLAGS) -flto=jobserver
		ifneq ($(findstring MINGW,$(KERNEL)),)
			LDFLAGS += -save-temps
		else ifneq ($(findstring MSYS,$(KERNEL)),)
			LDFLAGS += -save-temps
		endif
	else
		CXXFLAGS += -flto
		LDFLAGS += $(CXXFLAGS)
	endif

# To use LTO and static linking on windows, the tool chain requires a recent gcc:
# gcc version 10.1 in msys2 or TDM-GCC version 9.2 are known to work, older might not.
# So, only enable it for a cross from Linux by default.
	else ifeq ($(comp),mingw)
	ifeq ($(KERNEL),Linux)
	ifneq ($(arch),i386)
		CXXFLAGS += -flto
		LDFLAGS += $(CXXFLAGS) -flto=jobserver
	endif
	endif
	endif
endif
endif

### 3.6 Android 5 can only run position independent executables. Note that this
### breaks Android 4.0 and earlier.
ifeq ($(OS), Android)
	CXXFLAGS += -fPIE
	LDFLAGS += -fPIE -pie
endif

### ==========================================================================
### Section 4. Public Targets
### ==========================================================================


help:
	@echo ""
	@echo "To compile stockfish, type: "
	@echo ""
	@echo "make target ARCH=arch [COMP=compiler] [COMPCXX=cxx]"
	@echo ""
	@echo "Supported targets:"
	@echo ""
	@echo "help                    > Display architecture details"
	@echo "build                   > Standard build"
	@echo "net                     > Download the default nnue net"
	@echo "profile-build           > Faster build (with profile-guided optimization)"
	@echo "strip                   > Strip executable"
	@echo "install                 > Install executable"
	@echo "clean                   > Clean up"
	@echo ""
	@echo "Supported archs:"
	@echo ""
	@echo "x86-64                  > x86 64-bit"
	@echo "x86-32                  > x86 32-bit"
	@echo "ppc-64                  > PPC 64-bit"
	@echo "ppc-32                  > PPC 32-bit"
	@echo "armv7                   > ARMv7 32-bit"
	@echo "armv8                   > ARMv8 64-bit"
	@echo "e2k                     > Elbrus 2000"
	@echo "apple-silicon           > Apple silicon ARM64"
	@echo "general-64              > unspecified 64-bit"
	@echo "general-32              > unspecified 32-bit"
	@echo ""
	@echo "Supported compilers:"
	@echo ""
	@echo "gcc                     > Gnu compiler (default)"
	@echo "mingw                   > Gnu compiler with MinGW under Windows"
	@echo "clang                   > LLVM Clang compiler"
	@echo "icc                     > Intel compiler"
	@echo "ndk                     > Google NDK to cross-compile for Android"
	@echo ""
	@echo "Simple examples. If you don't know what to do, you likely want to run: "
	@echo ""
	@echo "make -j build ARCH=general-64  (A portable, slow compile for 64-bit systems)"
	@echo "make -j build ARCH=general-32  (A portable, slow compile for 32-bit systems)"
	@echo ""
	@echo "Advanced examples, for experienced users looking for performance: "
	@echo ""
	@echo "make    help  ARCH=x86-64"
	@echo "make -j profile-build ARCH=x86-64 COMP=gcc COMPCXX=g++-9.0"
	@echo "make -j build ARCH=x86-64 COMP=clang"
	@echo ""
	@echo "-------------------------------"
ifeq ($(SUPPORTED_ARCH)$(help_skip_sanity), true)
	@echo "The selected architecture $(ARCH) will enable the following configuration: "
	@$(MAKE) ARCH=$(ARCH) COMP=$(COMP) config-sanity
else
	@echo "Specify a supported architecture with the ARCH option for more details"
	@echo ""
endif


.PHONY: help build profile-build strip install clean net objclean profileclean \
        config-sanity icc-profile-use icc-profile-make gcc-profile-use gcc-profile-make \
        clang-profile-use clang-profile-make

build: net config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

profile-build: net config-sanity objclean profileclean
	@echo ""
	@echo "Step 1/4. Building instrumented executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_make)
	@echo ""
	@echo "Step 2/4. Running benchmark for pgo-build ..."
	$(PGOBENCH) > /dev/null
	@echo ""
	@echo "Step 3/4. Building optimized executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) objclean
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_use)
	@echo ""
	@echo "Step 4/4. Deleting profile data ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) profileclean

strip:
	$(STRIP) $(EXE)

install:
	-mkdir -p -m 755 $(BINDIR)
	-cp $(EXE) $(BINDIR)
	-strip $(BINDIR)/$(EXE)

# clean all
clean: objclean profileclean
	@rm -f .depend *~ core

# evaluation network (nnue)
net:
	$(eval nnuenet := $(shell grep EvalFileDefaultName evaluate.h | grep define | sed 's/.*\(nn-[a-z0-9]\{12\}.nnue\).*/\1/'))
	@echo "Default net: $(nnuenet)"
	$(eval nnuedownloadurl := https://tests.stockfishchess.org/api/nn/$(nnuenet))
	$(eval curl_or_wget := $(shell if hash curl 2>/dev/null; then echo "curl -skL"; elif hash wget 2>/dev/null; then echo "wget -qO-"; fi))
	@if test -f "$(nnuenet)"; then \
            echo "Already available."; \
         else \
            if [ "x$(curl_or_wget)" = "x" ]; then \
               echo "Automatic download failed: neither curl nor wget is installed. Install one of these tools or download the net manually"; exit 1; \
            else \
               echo "Downloading $(nnuedownloadurl)"; $(curl_or_wget) $(nnuedownloadurl) > $(nnuenet);\
            fi; \
        fi;
	$(eval shasum_command := $(shell if hash shasum 2>/dev/null; then echo "shasum -a 256 "; elif hash sha256sum 2>/dev/null; then echo "sha256sum "; fi))
	@if [ "x$(shasum_command)" != "x" ]; then \
	    if [ "$(nnuenet)" != "nn-"`$(shasum_command) $(nnuenet) | cut -c1-12`".nnue" ]; then \
                echo "Failed download or $(nnuenet) corrupted, please delete!"; exit 1; \
            fi \
         else \
            echo "shasum / sha256sum not found, skipping net validation"; \
        fi

# clean binaries and objects
objclean:
	@rm -f $(EXE) *.o ./syzygy/*.o ./nnue/*.o ./nnue/features/*.o

# clean auxiliary profiling files
profileclean:
	@rm -rf profdir
	@rm -f bench.txt *.gcda *.gcno ./syzygy/*.gcda ./nnue/*.gcda ./nnue/features/*.gcda *.s
	@rm -f stockfish.profdata *.profraw
	@rm -f stockfish.exe.lto_wrapper_args
	@rm -f stockfish.exe.ltrans.out
	@rm -f ./-lstdc++.res

default:
	help

### ==========================================================================
### Section 5. Private Targets
### ==========================================================================

all: $(EXE) .depend

config-sanity: net
	@echo ""
	@echo "Config:"
	@echo "debug: '$(debug)'"
	@echo "sanitize: '$(sanitize)'"
	@echo "optimize: '$(optimize)'"
	@echo "arch: '$(arch)'"
	@echo "bits: '$(bits)'"
	@echo "kernel: '$(KERNEL)'"
	@echo "os: '$(OS)'"
	@echo ""
	@echo "Flags:"
	@echo "CXX: $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo ""
	@echo "Testing config sanity. If this fails, try 'make help' ..."
	@echo ""
	@test "$(debug)" = "yes" || test "$(debug)" = "no"
	@test "$(optimize)" = "yes" || test "$(optimize)" = "no"
	@test "$(SUPPORTED_ARCH)" = "true"
	@test "$(arch)" = "any" || test "$(arch)" = "x86_64" || test "$(arch)" = "i386" || \
	 test "$(arch)" = "ppc64" || test "$(arch)" = "ppc" || test "$(arch)" = "e2k" || \
	 test "$(arch)" = "armv7" || test "$(arch)" = "armv8" || test "$(arch)" = "arm64"
	@test "$(bits)" = "32" || test "$(bits)" = "64"
	@test "$(comp)" = "gcc" || test "$(comp)" = "icc" || test "$(comp)" = "mingw" || test "$(comp)" = "clang" \
	|| test "$(comp)" = "armv7a-linux-androideabi16-clang"  || test "$(comp)" = "aarch64-linux-android21-clang"

$(EXE): $(OBJS)
	+$(CXX) -o $@ $(OBJS) $(LDFLAGS)

clang-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-generate ' \
	EXTRALDFLAGS=' -fprofile-instr-generate' \
	all

clang-profile-use:
	$(XCRUN) llvm-profdata merge -output=stockfish.profdata *.profraw
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-use=stockfish.profdata' \
	EXTRALDFLAGS='-fprofile-use ' \
	all

gcc-profile-make:
	@mkdir -p profdir
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-generate=profdir' \
	EXTRALDFLAGS='-lgcov' \
	all

gcc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-use=profdir -fno-peel-loops -fno-tracer' \
	EXTRALDFLAGS='-lgcov' \
	all

icc-profile-make:
	@mkdir -p profdir
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-prof-gen=srcpos -prof_dir ./profdir' \
	all

icc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-prof_use -prof_dir ./profdir' \
	all

.depend:
	-@$(CXX) $(DEPENDFLAGS) -MM $(SRCS) > $@ 2> /dev/null

-include .depend
