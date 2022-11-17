# Stockfish, a UCI chess playing engine derived from Glaurung 2.1
# Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)
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

### Establish the operating system name
KERNEL = $(shell uname -s)
ifeq ($(KERNEL),Linux)
	OS = $(shell uname -o)
endif

### Target Windows OS
ifeq ($(OS),Windows_NT)
	ifneq ($(COMP),ndk)
		target_windows = yes
	endif
else ifeq ($(COMP),mingw)
	target_windows = yes
	ifeq ($(WINE_PATH),)
		WINE_PATH = $(shell which wine)
	endif
endif

### Executable name
ifeq ($(target_windows),yes)
	EXE = stockfish.exe
else
	EXE = stockfish
endif

### Installation dir definitions
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

### Built-in benchmark for pgo-builds
ifeq ($(SDE_PATH),)
	PGOBENCH = $(WINE_PATH) ./$(EXE) bench
else
	PGOBENCH = $(SDE_PATH) -- $(WINE_PATH) ./$(EXE) bench
endif

### Source and object files
SRCS = benchmark.cpp bitbase.cpp bitboard.cpp endgame.cpp evaluate.cpp main.cpp \
	material.cpp misc.cpp movegen.cpp movepick.cpp pawns.cpp position.cpp psqt.cpp \
	search.cpp thread.cpp timeman.cpp tt.cpp uci.cpp ucioption.cpp tune.cpp syzygy/tbprobe.cpp \
	nnue/evaluate_nnue.cpp nnue/features/half_ka_v2_hm.cpp

OBJS = $(notdir $(SRCS:.cpp=.o))

VPATH = syzygy:nnue:nnue/features

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
# prefetch = yes/no   --- -DUSE_PREFETCH   --- Use prefetch asm-instruction
# popcnt = yes/no     --- -DUSE_POPCNT     --- Use popcnt asm-instruction
# pext = yes/no       --- -DUSE_PEXT       --- Use pext x86_64 asm-instruction
# sse = yes/no        --- -msse            --- Use Intel Streaming SIMD Extensions
# mmx = yes/no        --- -mmmx            --- Use Intel MMX instructions
# sse2 = yes/no       --- -msse2           --- Use Intel Streaming SIMD Extensions 2
# ssse3 = yes/no      --- -mssse3          --- Use Intel Supplemental Streaming SIMD Extensions 3
# sse41 = yes/no      --- -msse4.1         --- Use Intel Streaming SIMD Extensions 4.1
# avx2 = yes/no       --- -mavx2           --- Use Intel Advanced Vector Extensions 2
# avxvnni = yes/no    --- -mavxvnni        --- Use Intel Vector Neural Network Instructions AVX
# avx512 = yes/no     --- -mavx512bw       --- Use Intel Advanced Vector Extensions 512
# vnni256 = yes/no    --- -mavx512vnni     --- Use Intel Vector Neural Network Instructions 256
# vnni512 = yes/no    --- -mavx512vnni     --- Use Intel Vector Neural Network Instructions 512
# neon = yes/no       --- -DUSE_NEON       --- Use ARM SIMD architecture
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.
#
# Example of use for these flags:
# make build ARCH=x86-64-avx512 debug=yes sanitize="address undefined"


### 2.1. General and architecture defaults

ifeq ($(ARCH),)
   ARCH = x86-64-modern
   help_skip_sanity = yes
endif
# explicitly check for the list of supported architectures (as listed with make help),
# the user can override with `make ARCH=x86-32-vnni256 SUPPORTED_ARCH=true`
ifeq ($(ARCH), $(filter $(ARCH), \
                 x86-64-vnni512 x86-64-vnni256 x86-64-avx512 x86-64-avxvnni x86-64-bmi2 \
                 x86-64-avx2 x86-64-sse41-popcnt x86-64-modern x86-64-ssse3 x86-64-sse3-popcnt \
                 x86-64 x86-32-sse41-popcnt x86-32-sse2 x86-32 ppc-64 ppc-32 e2k \
                 armv7 armv7-neon armv8 apple-silicon general-64 general-32 riscv64))
   SUPPORTED_ARCH=true
else
   SUPPORTED_ARCH=false
endif

optimize = yes
debug = no
sanitize = none
bits = 64
prefetch = no
popcnt = no
pext = no
sse = no
mmx = no
sse2 = no
ssse3 = no
sse41 = no
avx2 = no
avxvnni = no
avx512 = no
vnni256 = no
vnni512 = no
neon = no
arm_version = 0
STRIP = strip

### 2.2 Architecture specific

ifeq ($(findstring x86,$(ARCH)),x86)

# x86-32/64

ifeq ($(findstring x86-32,$(ARCH)),x86-32)
	arch = i386
	bits = 32
	sse = no
	mmx = yes
else
	arch = x86_64
	sse = yes
	sse2 = yes
endif

ifeq ($(findstring -sse,$(ARCH)),-sse)
	sse = yes
endif

ifeq ($(findstring -popcnt,$(ARCH)),-popcnt)
	popcnt = yes
endif

ifeq ($(findstring -mmx,$(ARCH)),-mmx)
	mmx = yes
endif

ifeq ($(findstring -sse2,$(ARCH)),-sse2)
	sse = yes
	sse2 = yes
endif

ifeq ($(findstring -ssse3,$(ARCH)),-ssse3)
	sse = yes
	sse2 = yes
	ssse3 = yes
endif

ifeq ($(findstring -sse41,$(ARCH)),-sse41)
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
endif

ifeq ($(findstring -modern,$(ARCH)),-modern)
	popcnt = yes
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
endif

ifeq ($(findstring -avx2,$(ARCH)),-avx2)
	popcnt = yes
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
	avx2 = yes
endif

ifeq ($(findstring -avxvnni,$(ARCH)),-avxvnni)
	popcnt = yes
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
	avx2 = yes
	avxvnni = yes
	pext = yes
endif

ifeq ($(findstring -bmi2,$(ARCH)),-bmi2)
	popcnt = yes
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
	avx2 = yes
	pext = yes
endif

ifeq ($(findstring -avx512,$(ARCH)),-avx512)
	popcnt = yes
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
	avx2 = yes
	pext = yes
	avx512 = yes
endif

ifeq ($(findstring -vnni256,$(ARCH)),-vnni256)
	popcnt = yes
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
	avx2 = yes
	pext = yes
	vnni256 = yes
endif

ifeq ($(findstring -vnni512,$(ARCH)),-vnni512)
	popcnt = yes
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
	avx2 = yes
	pext = yes
	avx512 = yes
	vnni512 = yes
endif

ifeq ($(sse),yes)
	prefetch = yes
endif

# 64-bit pext is not available on x86-32
ifeq ($(bits),32)
	pext = no
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
	prefetch = yes
	bits = 32
	arm_version = 7
endif

ifeq ($(ARCH),armv7-neon)
	arch = armv7
	prefetch = yes
	popcnt = yes
	neon = yes
	bits = 32
	arm_version = 7
endif

ifeq ($(ARCH),armv8)
	arch = armv8
	prefetch = yes
	popcnt = yes
	neon = yes
	arm_version = 8
endif

ifeq ($(ARCH),apple-silicon)
	arch = arm64
	prefetch = yes
	popcnt = yes
	neon = yes
	arm_version = 8
endif

ifeq ($(ARCH),ppc-32)
	arch = ppc
	bits = 32
endif

ifeq ($(ARCH),ppc-64)
	arch = ppc64
	popcnt = yes
	prefetch = yes
endif

ifeq ($(findstring e2k,$(ARCH)),e2k)
	arch = e2k
	mmx = yes
	bits = 64
	sse = yes
	sse2 = yes
	ssse3 = yes
	sse41 = yes
	popcnt = yes
endif

ifeq ($(ARCH),riscv64)
	arch = riscv64
endif
endif


### ==========================================================================
### Section 3. Low-level Configuration
### ==========================================================================

### 3.1 Selecting compiler (default = gcc)
ifeq ($(MAKELEVEL),0)
       export ENV_CXXFLAGS := $(CXXFLAGS)
       export ENV_DEPENDFLAGS := $(DEPENDFLAGS)
       export ENV_LDFLAGS := $(LDFLAGS)
endif

CXXFLAGS = $(ENV_CXXFLAGS) -Wall -Wcast-qual -fno-exceptions -std=c++17 $(EXTRACXXFLAGS)
DEPENDFLAGS = $(ENV_DEPENDFLAGS) -std=c++17
LDFLAGS = $(ENV_LDFLAGS) $(EXTRALDFLAGS)

ifeq ($(COMP),)
	COMP=gcc
endif

ifeq ($(COMP),gcc)
	comp=gcc
	CXX=g++
	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifeq ($(arch),$(filter $(arch),armv7 armv8 riscv64))
		ifeq ($(OS),Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
		ifeq ($(ARCH),riscv64)
			CXXFLAGS += -latomic
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

ifeq ($(target_windows),yes)
	LDFLAGS += -static
endif

ifeq ($(COMP),mingw)
	comp=mingw

	ifeq ($(bits),64)
		ifeq ($(shell which x86_64-w64-mingw32-c++-posix 2> /dev/null),)
			CXX=x86_64-w64-mingw32-c++
		else
			CXX=x86_64-w64-mingw32-c++-posix
		endif
	else
		ifeq ($(shell which i686-w64-mingw32-c++-posix 2> /dev/null),)
			CXX=i686-w64-mingw32-c++
		else
			CXX=i686-w64-mingw32-c++-posix
		endif
	endif
	CXXFLAGS += -pedantic -Wextra -Wshadow
endif

ifeq ($(COMP),icc)
	comp=icc
	CXX=icpc
	CXXFLAGS += -diag-disable 1476,10120 -Wcheck -Wabi -Wdeprecated -strict-ansi
endif

ifeq ($(COMP),clang)
	comp=clang
	CXX=clang++
	ifeq ($(target_windows),yes)
		CXX=x86_64-w64-mingw32-clang++
	endif

	CXXFLAGS += -pedantic -Wextra -Wshadow

	ifeq ($(filter $(KERNEL),Darwin OpenBSD FreeBSD),)
	ifeq ($(target_windows),)
	ifneq ($(RTLIB),compiler-rt)
		LDFLAGS += -latomic
	endif
	endif
	endif

	ifeq ($(arch),$(filter $(arch),armv7 armv8 riscv64))
		ifeq ($(OS),Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
		ifeq ($(ARCH),riscv64)
			CXXFLAGS += -latomic
		endif
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif
endif

ifeq ($(KERNEL),Darwin)
	CXXFLAGS += -mmacosx-version-min=10.14
	LDFLAGS += -mmacosx-version-min=10.14
	ifneq ($(arch),any)
		CXXFLAGS += -arch $(arch)
		LDFLAGS += -arch $(arch)
	endif
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
		ifneq ($(shell which arm-linux-androideabi-strip 2>/dev/null),)
			STRIP=arm-linux-androideabi-strip
		else
			STRIP=llvm-strip
		endif
	endif
	ifeq ($(arch),armv8)
		CXX=aarch64-linux-android21-clang++
		ifneq ($(shell which aarch64-linux-android-strip 2>/dev/null),)
			STRIP=aarch64-linux-android-strip
		else
			STRIP=llvm-strip
		endif
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
	ifeq ($(KERNEL),Darwin)
		EXTRAPROFILEFLAGS = -fvisibility=hidden
	endif
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

	ifeq ($(KERNEL),Darwin)
		ifeq ($(comp),$(filter $(comp),clang icc))
			CXXFLAGS += -mdynamic-no-pic
		endif

		ifeq ($(comp),gcc)
			ifneq ($(arch),arm64)
				CXXFLAGS += -mdynamic-no-pic
			endif
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

### 3.5 prefetch and popcount
ifeq ($(prefetch),yes)
	ifeq ($(sse),yes)
		CXXFLAGS += -msse
	endif
else
	CXXFLAGS += -DNO_PREFETCH
endif

ifeq ($(popcnt),yes)
	ifeq ($(arch),$(filter $(arch),ppc64 armv7 armv8 arm64))
		CXXFLAGS += -DUSE_POPCNT
	else ifeq ($(comp),icc)
		CXXFLAGS += -msse3 -DUSE_POPCNT
	else
		CXXFLAGS += -msse3 -mpopcnt -DUSE_POPCNT
	endif
endif

### 3.6 SIMD architectures
ifeq ($(avx2),yes)
	CXXFLAGS += -DUSE_AVX2
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mavx2 -mbmi
	endif
endif

ifeq ($(avxvnni),yes)
	CXXFLAGS += -DUSE_VNNI -DUSE_AVXVNNI
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mavxvnni
	endif
endif

ifeq ($(avx512),yes)
	CXXFLAGS += -DUSE_AVX512
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mavx512f -mavx512bw
	endif
endif

ifeq ($(vnni256),yes)
	CXXFLAGS += -DUSE_VNNI
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mavx512f -mavx512bw -mavx512vnni -mavx512dq -mavx512vl -mprefer-vector-width=256
	endif
endif

ifeq ($(vnni512),yes)
	CXXFLAGS += -DUSE_VNNI
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mavx512vnni -mavx512dq -mavx512vl
	endif
endif

ifeq ($(sse41),yes)
	CXXFLAGS += -DUSE_SSE41
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -msse4.1
	endif
endif

ifeq ($(ssse3),yes)
	CXXFLAGS += -DUSE_SSSE3
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mssse3
	endif
endif

ifeq ($(sse2),yes)
	CXXFLAGS += -DUSE_SSE2
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -msse2
	endif
endif

ifeq ($(mmx),yes)
	CXXFLAGS += -DUSE_MMX
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mmmx
	endif
endif

ifeq ($(neon),yes)
	CXXFLAGS += -DUSE_NEON=$(arm_version)
	ifeq ($(KERNEL),Linux)
	ifneq ($(COMP),ndk)
	ifneq ($(arch),armv8)
		CXXFLAGS += -mfpu=neon
	endif
	endif
	endif
endif

### 3.7 pext
ifeq ($(pext),yes)
	CXXFLAGS += -DUSE_PEXT
	ifeq ($(comp),$(filter $(comp),gcc clang mingw))
		CXXFLAGS += -mbmi2
	endif
endif

### 3.7.1 Try to include git commit sha for versioning
GIT_SHA = $(shell git rev-parse --short HEAD 2>/dev/null)
ifneq ($(GIT_SHA), )
	CXXFLAGS += -DGIT_SHA=\"$(GIT_SHA)\"
endif

### 3.7.2 Try to include git commit date for versioning
GIT_DATE = $(shell git show -s --date=format:'%Y%m%d' --format=%cd HEAD 2>/dev/null)
ifneq ($(GIT_DATE), )
	CXXFLAGS += -DGIT_DATE=\"$(GIT_DATE)\"
endif

### 3.8 Link Time Optimization
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(optimize),yes)
ifeq ($(debug), no)
	ifeq ($(comp),clang)
		CXXFLAGS += -flto=full
		ifeq ($(target_windows),yes)
			CXXFLAGS += -fuse-ld=lld
		endif
		LDFLAGS += $(CXXFLAGS)

# GCC and CLANG use different methods for parallelizing LTO and CLANG pretends to be
# GCC on some systems.
	else ifeq ($(comp),gcc)
	ifeq ($(gccisclang),)
		CXXFLAGS += -flto -flto-partition=one
		LDFLAGS += $(CXXFLAGS) -flto=jobserver
	else
		CXXFLAGS += -flto=full
		LDFLAGS += $(CXXFLAGS)
	endif

# To use LTO and static linking on Windows,
# the tool chain requires gcc version 10.1 or later.
	else ifeq ($(comp),mingw)
		CXXFLAGS += -flto -flto-partition=one
		LDFLAGS += $(CXXFLAGS) -save-temps
	endif
endif
endif

### 3.9 Android 5 can only run position independent executables. Note that this
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
	@echo "x86-64-vnni512          > x86 64-bit with vnni support 512bit wide"
	@echo "x86-64-vnni256          > x86 64-bit with vnni support 256bit wide"
	@echo "x86-64-avx512           > x86 64-bit with avx512 support"
	@echo "x86-64-avxvnni          > x86 64-bit with avxvnni support"
	@echo "x86-64-bmi2             > x86 64-bit with bmi2 support"
	@echo "x86-64-avx2             > x86 64-bit with avx2 support"
	@echo "x86-64-sse41-popcnt     > x86 64-bit with sse41 and popcnt support"
	@echo "x86-64-modern           > common modern CPU, currently x86-64-sse41-popcnt"
	@echo "x86-64-ssse3            > x86 64-bit with ssse3 support"
	@echo "x86-64-sse3-popcnt      > x86 64-bit with sse3 and popcnt support"
	@echo "x86-64                  > x86 64-bit generic (with sse2 support)"
	@echo "x86-32-sse41-popcnt     > x86 32-bit with sse41 and popcnt support"
	@echo "x86-32-sse2             > x86 32-bit with sse2 support"
	@echo "x86-32                  > x86 32-bit generic (with mmx and sse support)"
	@echo "ppc-64                  > PPC 64-bit"
	@echo "ppc-32                  > PPC 32-bit"
	@echo "armv7                   > ARMv7 32-bit"
	@echo "armv7-neon              > ARMv7 32-bit with popcnt and neon"
	@echo "armv8                   > ARMv8 64-bit with popcnt and neon"
	@echo "e2k                     > Elbrus 2000"
	@echo "apple-silicon           > Apple silicon ARM64"
	@echo "general-64              > unspecified 64-bit"
	@echo "general-32              > unspecified 32-bit"
	@echo "riscv64                 > RISC-V 64-bit"
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
	@echo "make -j build ARCH=x86-64  (A portable, slow compile for 64-bit systems)"
	@echo "make -j build ARCH=x86-32  (A portable, slow compile for 32-bit systems)"
	@echo ""
	@echo "Advanced examples, for experienced users looking for performance: "
	@echo ""
	@echo "make    help  ARCH=x86-64-bmi2"
	@echo "make -j profile-build ARCH=x86-64-bmi2 COMP=gcc COMPCXX=g++-9.0"
	@echo "make -j build ARCH=x86-64-ssse3 COMP=clang"
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
        clang-profile-use clang-profile-make FORCE

build: net config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

profile-build: net config-sanity objclean profileclean
	@echo ""
	@echo "Step 1/4. Building instrumented executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_make)
	@echo ""
	@echo "Step 2/4. Running benchmark for pgo-build ..."
	$(PGOBENCH) 2>&1 | tail -n 4
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
	$(STRIP) $(BINDIR)/$(EXE)

# clean all
clean: objclean profileclean
	@rm -f .depend *~ core

# evaluation network (nnue)
net:
	$(eval nnuenet := $(shell grep EvalFileDefaultName evaluate.h | grep define | sed 's/.*\(nn-[a-z0-9]\{12\}.nnue\).*/\1/'))
	@echo "Default net: $(nnuenet)"
	$(eval nnuedownloadurl1 := https://tests.stockfishchess.org/api/nn/$(nnuenet))
	$(eval nnuedownloadurl2 := https://github.com/official-stockfish/networks/raw/master/$(nnuenet))
	$(eval curl_or_wget := $(shell if hash curl 2>/dev/null; then echo "curl -skL"; elif hash wget 2>/dev/null; then echo "wget -qO-"; fi))
	@if [ "x$(curl_or_wget)" = "x" ]; then \
	    echo "Automatic download failed: neither curl nor wget is installed. Install one of these tools or download the net manually"; exit 1; \
        fi
	$(eval shasum_command := $(shell if hash shasum 2>/dev/null; then echo "shasum -a 256 "; elif hash sha256sum 2>/dev/null; then echo "sha256sum "; fi))
	@if [ "x$(shasum_command)" = "x" ]; then \
            echo "shasum / sha256sum not found, skipping net validation"; \
        fi
	@for nnuedownloadurl in "$(nnuedownloadurl1)" "$(nnuedownloadurl2)"; do \
	   if test -f "$(nnuenet)"; then \
	      echo "$(nnuenet) available."; \
	   else \
	      if [ "x$(curl_or_wget)" != "x" ]; then \
	         echo "Downloading $${nnuedownloadurl}"; $(curl_or_wget) $${nnuedownloadurl} > $(nnuenet);\
	      fi; \
	   fi; \
	   if [ "x$(shasum_command)" != "x" ]; then \
	      if [ "$(nnuenet)" != "nn-"`$(shasum_command) $(nnuenet) | cut -c1-12`".nnue" ]; then \
	         echo "Removing failed download"; rm -f $(nnuenet); \
	      else \
	         echo "Network validated"; break; \
	      fi; \
	   fi; \
	done
	@if ! test -f "$(nnuenet)"; then \
	    echo "Failed to download $(nnuenet)."; \
	fi

# clean binaries and objects
objclean:
	@rm -f stockfish stockfish.exe *.o ./syzygy/*.o ./nnue/*.o ./nnue/features/*.o

# clean auxiliary profiling files
profileclean:
	@rm -rf profdir
	@rm -f bench.txt *.gcda *.gcno ./syzygy/*.gcda ./nnue/*.gcda ./nnue/features/*.gcda *.s
	@rm -f stockfish.profdata *.profraw
	@rm -f stockfish.*args*
	@rm -f stockfish.*lt*
	@rm -f stockfish.res
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
	@echo "prefetch: '$(prefetch)'"
	@echo "popcnt: '$(popcnt)'"
	@echo "pext: '$(pext)'"
	@echo "sse: '$(sse)'"
	@echo "mmx: '$(mmx)'"
	@echo "sse2: '$(sse2)'"
	@echo "ssse3: '$(ssse3)'"
	@echo "sse41: '$(sse41)'"
	@echo "avx2: '$(avx2)'"
	@echo "avxvnni: '$(avxvnni)'"
	@echo "avx512: '$(avx512)'"
	@echo "vnni256: '$(vnni256)'"
	@echo "vnni512: '$(vnni512)'"
	@echo "neon: '$(neon)'"
	@echo "arm_version: '$(arm_version)'"
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
	 test "$(arch)" = "armv7" || test "$(arch)" = "armv8" || test "$(arch)" = "arm64" || test "$(arch)" = "riscv64"
	@test "$(bits)" = "32" || test "$(bits)" = "64"
	@test "$(prefetch)" = "yes" || test "$(prefetch)" = "no"
	@test "$(popcnt)" = "yes" || test "$(popcnt)" = "no"
	@test "$(pext)" = "yes" || test "$(pext)" = "no"
	@test "$(sse)" = "yes" || test "$(sse)" = "no"
	@test "$(mmx)" = "yes" || test "$(mmx)" = "no"
	@test "$(sse2)" = "yes" || test "$(sse2)" = "no"
	@test "$(ssse3)" = "yes" || test "$(ssse3)" = "no"
	@test "$(sse41)" = "yes" || test "$(sse41)" = "no"
	@test "$(avx2)" = "yes" || test "$(avx2)" = "no"
	@test "$(avx512)" = "yes" || test "$(avx512)" = "no"
	@test "$(vnni256)" = "yes" || test "$(vnni256)" = "no"
	@test "$(vnni512)" = "yes" || test "$(vnni512)" = "no"
	@test "$(neon)" = "yes" || test "$(neon)" = "no"
	@test "$(comp)" = "gcc" || test "$(comp)" = "icc" || test "$(comp)" = "mingw" || test "$(comp)" = "clang" \
	|| test "$(comp)" = "armv7a-linux-androideabi16-clang"  || test "$(comp)" = "aarch64-linux-android21-clang"

$(EXE): $(OBJS)
	+$(CXX) -o $@ $(OBJS) $(LDFLAGS)

# Force recompilation to ensure version info is up-to-date
misc.o: FORCE
FORCE:

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
	EXTRACXXFLAGS+=$(EXTRAPROFILEFLAGS) \
	EXTRALDFLAGS='-lgcov' \
	all

gcc-profile-use:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-use=profdir -fno-peel-loops -fno-tracer' \
	EXTRACXXFLAGS+=$(EXTRAPROFILEFLAGS) \
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

.depend: $(SRCS)
	-@$(CXX) $(DEPENDFLAGS) -MM $(SRCS) > $@ 2> /dev/null

-include .depend
