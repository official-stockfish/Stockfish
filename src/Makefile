# Stockfish, a UCI chess playing engine derived from Glaurung 2.1
# Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
# Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
# Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad
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
MSYS := $(shell echo $$MSYSTEM)
ifneq (,$(filter $(MSYS),MINGW64 MINGW32))
COMP := mingw
EXE := stockfish.exe
else
EXE := stockfish
endif

### Installation dir definitions
PREFIX := /usr/local
BINDIR := $(PREFIX)/bin

### Built-in benchmark for pgo-builds
PGOBENCH := ./$(EXE) bench

### Source and object files
SRCS := benchmark.cpp bitbase.cpp bitboard.cpp endgame.cpp evaluate.cpp main.cpp \
	material.cpp misc.cpp movegen.cpp movepick.cpp pawns.cpp position.cpp psqt.cpp \
	search.cpp thread.cpp timeman.cpp tt.cpp uci.cpp ucioption.cpp tune.cpp syzygy/tbprobe.cpp

OBJS := $(notdir $(SRCS:.cpp=.o))

VPATH := syzygy

### Establish the operating system name
KERNEL := $(shell uname -s)
ifeq ($(KERNEL),Linux)
	OS := $(shell uname -o)
endif

### ==========================================================================
### Section 2. High-level Configuration
### ==========================================================================
#
# flag                --- Comp switch --- Description
# ----------------------------------------------------------------------------
#
# debug = yes/no      --- -DNDEBUG         --- Enable/Disable debug mode
# sanitize = undefined/thread/no (-fsanitize )
#                     --- ( undefined )    --- enable undefined behavior checks
#                     --- ( thread    )    --- enable threading error  checks
# optimize = yes/no   --- (-O3/-fast etc.) --- Enable/Disable optimizations
# arch = (name)       --- (-arch)          --- Target architecture
# bits = 64/32        --- -DIS_64BIT       --- 64-/32-bit operating system
# prefetch = yes/no   --- -DUSE_PREFETCH   --- Use prefetch asm-instruction
# popcnt = yes/no     --- -DUSE_POPCNT     --- Use popcnt asm-instruction
# sse = yes/no        --- -msse            --- Use Intel Streaming SIMD Extensions
# pext = yes/no       --- -DUSE_PEXT       --- Use pext x86_64 asm-instruction
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.

### 2.1. General and architecture defaults
optimize := yes
debug := no
sanitize := no
bits := 32
prefetch := no
popcnt := no
sse := no
pext := no

### 2.2 Architecture specific

ifeq ($(ARCH),general-32)
	arch := any
endif

ifeq ($(ARCH),x86-32-old)
	arch := i386
endif

ifeq ($(ARCH),x86-32)
	arch := i386
	prefetch := yes
	sse := yes
endif

ifeq ($(ARCH),general-64)
	arch := any
	bits := 64
endif

ifeq ($(ARCH),x86-64)
	arch := x86_64
	bits := 64
	prefetch := yes
	sse := yes
endif

ifeq ($(ARCH),x86-64-modern)
	arch := x86_64
	bits := 64
	prefetch := yes
	popcnt := yes
	sse := yes
endif

ifeq ($(ARCH),x86-64-bmi2)
	arch := x86_64
	bits := 64
	prefetch := yes
	popcnt := yes
	sse := yes
	pext := yes
endif

ifeq ($(ARCH),armv7)
	arch := armv7
	prefetch := yes
endif

ifeq ($(ARCH),ppc-32)
	arch := ppc
endif

ifeq ($(ARCH),ppc-64)
	arch := ppc64
	bits := 64
	popcnt := yes
	prefetch := yes
endif


### ==========================================================================
### Section 3. Low-level configuration
### ==========================================================================

### 3.1 Selecting compiler (default = gcc)

CXXFLAGS += -Wall -Wcast-qual -fno-exceptions -std=c++11 $(EXTRACXXFLAGS)
DEPENDFLAGS += -std=c++11
LDFLAGS += $(EXTRALDFLAGS)

ifeq ($(COMP),)
	COMP:=gcc
endif

ifeq ($(COMP),gcc)
	comp:=gcc
	CXX:=g++
	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifeq ($(ARCH),armv7)
		ifeq ($(OS),Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif

	ifneq ($(KERNEL),Darwin)
	   LDFLAGS += -Wl,--no-as-needed
	endif
endif

ifeq ($(COMP),mingw)
	comp:=mingw

	ifeq ($(KERNEL),Linux)
		ifeq ($(bits),64)
			CXX:=x86_64-w64-mingw32-c++
			ifneq ($(shell which x86_64-w64-mingw32-c++-posix),)
				CXX +=-posix
			endif
		else
			CXX:=i686-w64-mingw32-c++
			ifneq ($(shell which i686-w64-mingw32-c++-posix),)
				CXX +=-posix
			endif
		endif
	else
		CXX:=g++
	endif

	CXXFLAGS += -Wextra -Wshadow
	LDFLAGS += -static
endif

ifeq ($(COMP),icc)
	comp:=icc
	CXX:=icpc
	CXXFLAGS += -diag-disable 1476,10120 -Wcheck -Wabi -Wdeprecated -strict-ansi
endif

ifeq ($(COMP),clang)
	comp:=clang
	CXX:=clang++
	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifneq ($(KERNEL),Darwin)
	ifneq ($(KERNEL),OpenBSD)
		LDFLAGS += -latomic
	endif
	endif

	ifeq ($(ARCH),armv7)
		ifeq ($(OS),Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif
endif

ifeq ($(comp),icc)
	profile_make := icc-profile-make
	profile_use := icc-profile-use
else
ifeq ($(comp),clang)
	profile_make := clang-profile-make
	profile_use := clang-profile-use
else
	profile_make := gcc-profile-make
	profile_use := gcc-profile-use
endif
endif

ifeq ($(KERNEL),Darwin)
	CXXFLAGS += -arch $(arch) -mmacosx-version-min=10.9
	LDFLAGS += -arch $(arch) -mmacosx-version-min=10.9
endif

### Travis CI script uses COMPILER to overwrite CXX
ifdef COMPILER
	COMPCXX:=$(COMPILER)
endif

### Allow overwriting CXX from command line
ifdef COMPCXX
	CXX:=$(COMPCXX)
endif

### On mingw use Windows threads, otherwise POSIX
ifneq ($(comp),mingw)
	# On Android Bionic's C library comes with its own pthread implementation bundled in
	ifneq ($(OS),Android)
		# Haiku has pthreads in its libroot, so only link it in on other platforms
		ifneq ($(KERNEL),Haiku)
			LDFLAGS += -lpthread
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
ifneq ($(sanitize),no)
        CXXFLAGS += -g3 -fsanitize=$(sanitize) -fuse-ld=gold
        LDFLAGS += -fsanitize=$(sanitize) -fuse-ld=gold
endif

### 3.3 Optimization
ifeq ($(optimize),yes)

	CXXFLAGS += -O3

	ifeq ($(comp),gcc)
		ifeq ($(OS), Android)
			CXXFLAGS += -fno-gcse -mthumb -march=armv7-a -mfloat-abi=softfp
		endif
	endif

	ifneq (,$(filter $(comp),gcc clang icc))
		ifeq ($(KERNEL),Darwin)
			CXXFLAGS += -mdynamic-no-pic
		endif
	endif
endif

### 3.4 Bits
ifeq ($(bits),64)
	CXXFLAGS += -DIS_64BIT
endif

### 3.5 prefetch
ifeq ($(prefetch),yes)
	ifeq ($(sse),yes)
		CXXFLAGS += -msse
		DEPENDFLAGS += -msse
	endif
else
	CXXFLAGS += -DNO_PREFETCH
endif

### 3.6 popcnt
ifeq ($(popcnt),yes)
	ifeq ($(arch),ppc64)
		CXXFLAGS += -DUSE_POPCNT
	else ifeq ($(comp),icc)
		CXXFLAGS += -msse3 -DUSE_POPCNT
	else
		CXXFLAGS += -msse3 -mpopcnt -DUSE_POPCNT
	endif
endif

### 3.7 pext
ifeq ($(pext),yes)
	CXXFLAGS += -DUSE_PEXT
	ifneq (,$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -msse4 -mbmi2
	endif
endif

### 3.8 Link Time Optimization, it works since gcc 4.5 but not on mingw under Windows.
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(optimize),yes)
ifeq ($(debug), no)
	ifneq (,$(filter $(comp),gcc clang))
		CXXFLAGS += -flto
		LDFLAGS += $(CXXFLAGS)

	else ifeq ($(comp),mingw)
		ifeq ($(KERNEL),Linux)
			CXXFLAGS += -flto
			LDFLAGS += $(CXXFLAGS)
		endif
	endif
endif
endif

### 3.9 Android 5 can only run position independent executables. Note that this
### breaks Android 4.0 and earlier.
ifeq ($(OS), Android)
	CXXFLAGS += -fPIE
	LDFLAGS += -fPIE -pie
endif

DEP := .depend

### ==========================================================================
### Section 4. Public targets
### ==========================================================================

help:
	@echo ""
	@echo "To compile stockfish, type: "
	@echo ""
	@echo "make target ARCH=arch [COMP=compiler] [COMPCXX=cxx]"
	@echo ""
	@echo "Supported targets:"
	@echo ""
	@echo "build                   > Standard build"
	@echo "profile-build           > PGO build"
	@echo "strip                   > Strip executable"
	@echo "install                 > Install executable"
	@echo "clean                   > Clean up"
	@echo ""
	@echo "Supported archs:"
	@echo ""
	@echo "x86-64-bmi2             > x86 64-bit with pext support (also enables SSE4)"
	@echo "x86-64-modern           > x86 64-bit with popcnt support (also enables SSE3)"
	@echo "x86-64                  > x86 64-bit generic"
	@echo "x86-32                  > x86 32-bit (also enables SSE)"
	@echo "x86-32-old              > x86 32-bit fall back for old hardware"
	@echo "ppc-64                  > PPC 64-bit"
	@echo "ppc-32                  > PPC 32-bit"
	@echo "armv7                   > ARMv7 32-bit"
	@echo "general-64              > unspecified 64-bit"
	@echo "general-32              > unspecified 32-bit"
	@echo ""
	@echo "Supported compilers:"
	@echo ""
	@echo "gcc                     > Gnu compiler (default)"
	@echo "mingw                   > Gnu compiler with MinGW under Windows"
	@echo "clang                   > LLVM Clang compiler"
	@echo "icc                     > Intel compiler"
	@echo ""
	@echo "Simple examples. If you don't know what to do, you likely want to run: "
	@echo ""
	@echo "make build ARCH=x86-64    (This is for 64-bit systems)"
	@echo "make build ARCH=x86-32    (This is for 32-bit systems)"
	@echo ""
	@echo "Advanced examples, for experienced users: "
	@echo ""
	@echo "make build ARCH=x86-64 COMP=clang"
	@echo "make profile-build ARCH=x86-64-bmi2 COMP=gcc COMPCXX=g++-4.8"
	@echo ""


.PHONY: help build profile-build strip install clean objprofileclean \
			config-sanity icc-profile-make icc-profile-use gcc-profile-make gcc-profile-use \
			clang-profile-make clang-profile-use profile-make-hd clang-profile-d icc-profile-mkdir

build: config-sanity all

profile-build: config-sanity objprofileclean \
							profile-make-hd $(profile_make)
	@echo ; echo "Step 2/4. Running benchmark for pgo-build ..."
	$(PGOBENCH) > /dev/null
	@echo ; echo "Step 3/4. Building optimized executable ..."
# inline objclean
	@rm -f $(EXE) *.o ./syzygy/*.o
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_use)
	@echo ; echo "Step 4/4. Deleting profile data ..."
# inline profileclean
	@rm -rf profdir
	@rm -f bench.txt *.gcda *.gcno
	@rm -f stockfish.profdata *.profraw

strip:
	strip $(EXE)

install:
	-mkdir -p -m 755 $(BINDIR)
	-cp $(EXE) $(BINDIR)
	-strip $(BINDIR)/$(EXE)

#clean all
clean: objprofileclean
	@rm -f $(DEP) *~ core

# clean binaries and objects + clean auxiliary profiling files
objprofileclean:
	@rm -f $(EXE) *.o ./syzygy/*.o
	@rm -rf profdir
	@rm -f bench.txt *.gcda *.gcno
	@rm -f stockfish.profdata *.profraw

### ==========================================================================
### Section 5. Private targets
### ==========================================================================

all: $(EXE)

config-sanity:
	@echo ""
	@echo "Config:"
	@echo "debug: '$(debug)'"
	@echo "sanitize: '$(sanitize)'"
	@echo "optimize: '$(optimize)'"
	@echo "arch: '$(arch)'"
	@echo "bits: '$(bits)'"
	@echo "kernel: '$(KERNEL)'"
	@echo "os: '$(OS)'"
	@echo "prefetch: '$(prefetch)'"
	@echo "popcnt: '$(popcnt)'"
	@echo "sse: '$(sse)'"
	@echo "pext: '$(pext)'"
	@echo "will generate binary: '$(EXE)'"
	@echo ""
	@echo "Flags:"
	@echo "CXX: $(CXX)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo ""
	@echo "Testing config sanity. If this fails, try 'make help' ..."
	@echo ""
	@test "$(debug)" = "yes" || test "$(debug)" = "no"
	@test "$(sanitize)" = "undefined" || test "$(sanitize)" = "thread" || test "$(sanitize)" = "address" || test "$(sanitize)" = "no"
	@test "$(optimize)" = "yes" || test "$(optimize)" = "no"
	@test "$(arch)" = "any" || test "$(arch)" = "x86_64" || test "$(arch)" = "i386" || \
	 test "$(arch)" = "ppc64" || test "$(arch)" = "ppc" || test "$(arch)" = "armv7"
	@test "$(bits)" = "32" || test "$(bits)" = "64"
	@test "$(prefetch)" = "yes" || test "$(prefetch)" = "no"
	@test "$(popcnt)" = "yes" || test "$(popcnt)" = "no"
	@test "$(sse)" = "yes" || test "$(sse)" = "no"
	@test "$(pext)" = "yes" || test "$(pext)" = "no"
	@test "$(comp)" = "gcc" || test "$(comp)" = "icc" || test "$(comp)" = "mingw" || test "$(comp)" = "clang"

$(EXE): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

profile-make-hd: ;@echo; echo "Step 1/4. Building instrumented executable ..."

clang-profile-make: EXTRALDFLAGS := -fprofile-instr-generate
clang-profile-make: EXTRACXXFLAGS := -fprofile-instr-generate
clang-profile-make: all

clang-profile-use: EXTRALDFLAGS := -fprofile-use
clang-profile-use: EXTRACXXFLAGS := -fprofile-instr-use=stockfish.profdata
clang-profile-use: clang-profile-d all
clang-profile-d:
	llvm-profdata merge -output=stockfish.profdata *.profraw

gcc-profile-make: EXTRALDFLAGS := -lgcov
gcc-profile-make: EXTRACXXFLAGS := -fprofile-generate
gcc-profile-make: all

gcc-profile-use: EXTRALDFLAGS := -lgcov
gcc-profile-use: EXTRACXXFLAGS := -fprofile-use -fno-peel-loops -fno-tracer
gcc-profile-use: all

icc-profile-make: EXTRACXXFLAGS := -prof-gen=srcpos -prof_dir ./profdir
icc-profile-make: icc-profile-mkdir all
icc-profile-mkdir: ; @mkdir -p profdir

icc-profile-use: EXTRACXXFLAGS := -prof_use -prof_dir ./profdir
icc-profile-use: all

# If fails, > instead of -MF
$(DEP):
	@$(CXX) $(DEPENDFLAGS) $(SRCS) -MM -MF $@

ifneq (,$(filter $(MAKECMDGOALS),build profile-build))
include $(DEP)
endif
