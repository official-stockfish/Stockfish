# Stockfish, a UCI chess playing engine derived from Glaurung 2.1
# Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)
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
KERNEL := $(shell uname -s)
ifeq ($(KERNEL),Linux)
	OS := $(shell uname -o)
endif

### Target Windows OS
ifeq ($(OS),Windows_NT)
	ifneq ($(COMP),ndk)
		target_windows = yes
	endif
else ifeq ($(COMP),mingw)
	target_windows = yes
	ifeq ($(WINE_PATH),)
		WINE_PATH := $(shell which wine)
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
PGOBENCH = $(WINE_PATH) ./$(EXE) bench

### Source and object files
SRCS = benchmark.cpp bitboard.cpp evaluate.cpp main.cpp \
	misc.cpp movegen.cpp movepick.cpp position.cpp \
	search.cpp thread.cpp timeman.cpp tt.cpp uci.cpp ucioption.cpp tune.cpp syzygy/tbprobe.cpp \
	nnue/nnue_accumulator.cpp nnue/nnue_misc.cpp nnue/features/half_ka_v2_hm.cpp nnue/network.cpp \
	engine.cpp score.cpp memory.cpp

HEADERS = benchmark.h bitboard.h evaluate.h misc.h movegen.h movepick.h history.h \
		nnue/nnue_misc.h nnue/features/half_ka_v2_hm.h nnue/layers/affine_transform.h \
		nnue/layers/affine_transform_sparse_input.h nnue/layers/clipped_relu.h nnue/layers/simd.h \
		nnue/layers/sqr_clipped_relu.h nnue/nnue_accumulator.h nnue/nnue_architecture.h \
		nnue/nnue_common.h nnue/nnue_feature_transformer.h position.h \
		search.h syzygy/tbprobe.h thread.h thread_win32_osx.h timeman.h \
		tt.h tune.h types.h uci.h ucioption.h perft.h nnue/network.h engine.h score.h numa.h memory.h

OBJS = $(notdir $(SRCS:.cpp=.o))

VPATH = syzygy:nnue:nnue/features

### ==========================================================================
### Section 2. High-level Configuration
### ==========================================================================
#
# flag                --- Comp switch        --- Description
# ----------------------------------------------------------------------------
#
# debug = yes/no      --- -DNDEBUG           --- Enable/Disable debug mode
# sanitize = none/<sanitizer> ... (-fsanitize )
#                     --- ( undefined )      --- enable undefined behavior checks
#                     --- ( thread    )      --- enable threading error checks
#                     --- ( address   )      --- enable memory access checks
#                     --- ...etc...          --- see compiler documentation for supported sanitizers
# optimize = yes/no   --- (-O3/-fast etc.)   --- Enable/Disable optimizations
# arch = (name)       --- (-arch)            --- Target architecture
# bits = 64/32        --- -DIS_64BIT         --- 64-/32-bit operating system
# prefetch = yes/no   --- -DUSE_PREFETCH     --- Use prefetch asm-instruction
# popcnt = yes/no     --- -DUSE_POPCNT       --- Use popcnt asm-instruction
# pext = yes/no       --- -DUSE_PEXT         --- Use pext x86_64 asm-instruction
# sse = yes/no        --- -msse              --- Use Intel Streaming SIMD Extensions
# mmx = yes/no        --- -mmmx              --- Use Intel MMX instructions
# sse2 = yes/no       --- -msse2             --- Use Intel Streaming SIMD Extensions 2
# ssse3 = yes/no      --- -mssse3            --- Use Intel Supplemental Streaming SIMD Extensions 3
# sse41 = yes/no      --- -msse4.1           --- Use Intel Streaming SIMD Extensions 4.1
# avx2 = yes/no       --- -mavx2             --- Use Intel Advanced Vector Extensions 2
# avxvnni = yes/no    --- -mavxvnni          --- Use Intel Vector Neural Network Instructions AVX
# avx512 = yes/no     --- -mavx512bw         --- Use Intel Advanced Vector Extensions 512
# vnni256 = yes/no    --- -mavx256vnni       --- Use Intel Vector Neural Network Instructions 512 with 256bit operands
# vnni512 = yes/no    --- -mavx512vnni       --- Use Intel Vector Neural Network Instructions 512
# altivec = yes/no    --- -maltivec          --- Use PowerPC Altivec SIMD extension
# vsx = yes/no        --- -mvsx              --- Use POWER VSX SIMD extension
# neon = yes/no       --- -DUSE_NEON         --- Use ARM SIMD architecture
# dotprod = yes/no    --- -DUSE_NEON_DOTPROD --- Use ARM advanced SIMD Int8 dot product instructions
# lsx = yes/no        --- -mlsx              --- Use Loongson SIMD eXtension
# lasx = yes/no       --- -mlasx             --- use Loongson Advanced SIMD eXtension
#
# Note that Makefile is space sensitive, so when adding new architectures
# or modifying existing flags, you have to make sure there are no extra spaces
# at the end of the line for flag values.
#
# Example of use for these flags:
# make build ARCH=x86-64-avx512 debug=yes sanitize="address undefined"


### 2.1. General and architecture defaults

ifeq ($(ARCH),)
   ARCH = native
endif

ifeq ($(ARCH), native)
   override ARCH := $(shell $(SHELL) ../scripts/get_native_properties.sh | cut -d " " -f 1)
endif

# explicitly check for the list of supported architectures (as listed with make help),
# the user can override with `make ARCH=x86-32-vnni256 SUPPORTED_ARCH=true`
ifeq ($(ARCH), $(filter $(ARCH), \
                 x86-64-vnni512 x86-64-vnni256 x86-64-avx512 x86-64-avxvnni x86-64-bmi2 \
                 x86-64-avx2 x86-64-sse41-popcnt x86-64-modern x86-64-ssse3 x86-64-sse3-popcnt \
                 x86-64 x86-32-sse41-popcnt x86-32-sse2 x86-32 ppc-64 ppc-64-altivec ppc-64-vsx ppc-32 e2k \
                 armv7 armv7-neon armv8 armv8-dotprod apple-silicon general-64 general-32 riscv64 \
                 loongarch64 loongarch64-lsx loongarch64-lasx))
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
altivec = no
vsx = no
neon = no
dotprod = no
arm_version = 0
lsx = no
lasx = no
STRIP = strip

ifneq ($(shell which clang-format-18 2> /dev/null),)
	CLANG-FORMAT = clang-format-18
else
	CLANG-FORMAT = clang-format
endif

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
        $(warning *** ARCH=$(ARCH) is deprecated, defaulting to ARCH=x86-64-sse41-popcnt. Execute `make help` for a list of available architectures. ***)
        $(shell sleep 5)
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

ifeq ($(ARCH),armv8-dotprod)
	arch = armv8
	prefetch = yes
	popcnt = yes
	neon = yes
	dotprod = yes
	arm_version = 8
endif

ifeq ($(ARCH),apple-silicon)
	arch = arm64
	prefetch = yes
	popcnt = yes
	neon = yes
	dotprod = yes
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

ifeq ($(ARCH),ppc-64-altivec)
	arch = ppc64
	popcnt = yes
	prefetch = yes
	altivec = yes
endif

ifeq ($(ARCH),ppc-64-vsx)
	arch = ppc64
	popcnt = yes
	prefetch = yes
	vsx = yes
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

ifeq ($(findstring loongarch64,$(ARCH)),loongarch64)
	arch = loongarch64
	prefetch = yes

ifeq ($(findstring -lasx,$(ARCH)),-lasx)
	lsx = yes
	lasx = yes
endif

ifeq ($(findstring -lsx,$(ARCH)),-lsx)
	lsx = yes
endif

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
	CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-declarations

	ifeq ($(arch),$(filter $(arch),armv7 armv8 riscv64))
		ifeq ($(OS),Android)
			CXXFLAGS += -m$(bits)
			LDFLAGS += -m$(bits)
		endif
		ifeq ($(ARCH),riscv64)
			CXXFLAGS += -latomic
		endif
	else ifeq ($(arch),loongarch64)
		CXXFLAGS += -latomic
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
	CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-declarations
endif

ifeq ($(COMP),icx)
	comp=icx
	CXX=icpx
	CXXFLAGS += --intel -pedantic -Wextra -Wshadow -Wmissing-prototypes \
		-Wconditional-uninitialized -Wabi -Wdeprecated
endif

ifeq ($(COMP),clang)
	comp=clang
	CXX=clang++
	ifeq ($(target_windows),yes)
		CXX=x86_64-w64-mingw32-clang++
	endif

	CXXFLAGS += -pedantic -Wextra -Wshadow -Wmissing-prototypes \
	            -Wconditional-uninitialized

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
	else ifeq ($(arch),loongarch64)
		CXXFLAGS += -latomic
	else
		CXXFLAGS += -m$(bits)
		LDFLAGS += -m$(bits)
	endif
endif

ifeq ($(KERNEL),Darwin)
	CXXFLAGS += -mmacosx-version-min=10.15
	LDFLAGS += -mmacosx-version-min=10.15
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
	ifeq ($(arch),x86_64)
		CXX=x86_64-linux-android21-clang++
		ifneq ($(shell which x86_64-linux-android-strip 2>/dev/null),)
			STRIP=x86_64-linux-android-strip
		else
			STRIP=llvm-strip
		endif
	endif
	LDFLAGS += -static-libstdc++ -pie -lm -latomic
endif

ifeq ($(comp),icx)
	profile_make = icx-profile-make
	profile_use = icx-profile-use
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

### Allow overwriting CXX from command line
ifdef COMPCXX
	CXX=$(COMPCXX)
endif

### Sometimes gcc is really clang
ifeq ($(COMP),gcc)
	gccversion := $(shell $(CXX) --version 2>/dev/null)
	gccisclang := $(findstring clang,$(gccversion))
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

	CXXFLAGS += -O3 -funroll-loops

	ifeq ($(comp),gcc)
		ifeq ($(OS), Android)
			CXXFLAGS += -fno-gcse -mthumb -march=armv7-a -mfloat-abi=softfp
		endif
	endif

	ifeq ($(KERNEL),Darwin)
		ifeq ($(comp),$(filter $(comp),clang icx))
			CXXFLAGS += -mdynamic-no-pic
		endif

		ifeq ($(comp),gcc)
			ifneq ($(arch),arm64)
				CXXFLAGS += -mdynamic-no-pic
			endif
		endif
	endif

	ifeq ($(comp),clang)
		clangmajorversion := $(shell $(CXX) -dumpversion 2>/dev/null | cut -f1 -d.)
		ifeq ($(shell expr $(clangmajorversion) \< 16),1)
			CXXFLAGS += -fexperimental-new-pass-manager
		endif
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
	ifeq ($(arch),$(filter $(arch),ppc64 ppc64-altivec ppc64-vsx armv7 armv8 arm64))
		CXXFLAGS += -DUSE_POPCNT
	else
		CXXFLAGS += -msse3 -mpopcnt -DUSE_POPCNT
	endif
endif

### 3.6 SIMD architectures
ifeq ($(avx2),yes)
	CXXFLAGS += -DUSE_AVX2
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mavx2 -mbmi
	endif
endif

ifeq ($(avxvnni),yes)
	CXXFLAGS += -DUSE_VNNI -DUSE_AVXVNNI
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mavxvnni
	endif
endif

ifeq ($(avx512),yes)
	CXXFLAGS += -DUSE_AVX512
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mavx512f -mavx512bw
	endif
endif

ifeq ($(vnni256),yes)
	CXXFLAGS += -DUSE_VNNI
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mavx512f -mavx512bw -mavx512vnni -mavx512dq -mavx512vl -mprefer-vector-width=256
	endif
endif

ifeq ($(vnni512),yes)
	CXXFLAGS += -DUSE_VNNI
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mavx512f -mavx512bw -mavx512vnni -mavx512dq -mavx512vl -mprefer-vector-width=512
	endif
endif

ifeq ($(sse41),yes)
	CXXFLAGS += -DUSE_SSE41
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -msse4.1
	endif
endif

ifeq ($(ssse3),yes)
	CXXFLAGS += -DUSE_SSSE3
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mssse3
	endif
endif

ifeq ($(sse2),yes)
	CXXFLAGS += -DUSE_SSE2
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -msse2
	endif
endif

ifeq ($(mmx),yes)
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mmmx
	endif
endif

ifeq ($(altivec),yes)
	CXXFLAGS += -maltivec
	ifeq ($(COMP),gcc)
		CXXFLAGS += -mabi=altivec
	endif
endif

ifeq ($(vsx),yes)
	CXXFLAGS += -mvsx
	ifeq ($(COMP),gcc)
		CXXFLAGS += -DNO_WARN_X86_INTRINSICS -DUSE_SSE2
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

ifeq ($(dotprod),yes)
	CXXFLAGS += -march=armv8.2-a+dotprod -DUSE_NEON_DOTPROD
endif

ifeq ($(lasx),yes)
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mlasx
	endif
endif

ifeq ($(lsx),yes)
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mlsx
	endif
endif

### 3.7 pext
ifeq ($(pext),yes)
	CXXFLAGS += -DUSE_PEXT
	ifeq ($(comp),$(filter $(comp),gcc clang mingw icx))
		CXXFLAGS += -mbmi2
	endif
endif

### 3.8.1 Try to include git commit sha for versioning
GIT_SHA := $(shell git rev-parse HEAD 2>/dev/null | cut -c 1-8)
ifneq ($(GIT_SHA), )
	CXXFLAGS += -DGIT_SHA=$(GIT_SHA)
endif

### 3.8.2 Try to include git commit date for versioning
GIT_DATE := $(shell git show -s --date=format:'%Y%m%d' --format=%cd HEAD 2>/dev/null)
ifneq ($(GIT_DATE), )
	CXXFLAGS += -DGIT_DATE=$(GIT_DATE)
endif

### 3.8.3 Try to include architecture
ifneq ($(ARCH), )
	CXXFLAGS += -DARCH=$(ARCH)
endif

### 3.9 Link Time Optimization
### This is a mix of compile and link time options because the lto link phase
### needs access to the optimization flags.
ifeq ($(optimize),yes)
ifeq ($(debug), no)
	ifeq ($(comp),$(filter $(comp),clang icx))
		CXXFLAGS += -flto=full
		ifeq ($(comp),icx)
			CXXFLAGS += -fwhole-program-vtables
                endif
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

### 3.10 Android 5 can only run position independent executables. Note that this
### breaks Android 4.0 and earlier.
ifeq ($(OS), Android)
	CXXFLAGS += -fPIE
	LDFLAGS += -fPIE -pie
endif

### ==========================================================================
### Section 4. Public Targets
### ==========================================================================

help:
	@echo "" && \
	echo "To compile stockfish, type: " && \
	echo "" && \
	echo "make -j target [ARCH=arch] [COMP=compiler] [COMPCXX=cxx]" && \
	echo "" && \
	echo "Supported targets:" && \
	echo "" && \
	echo "help                    > Display architecture details" && \
	echo "profile-build           > standard build with profile-guided optimization" && \
	echo "build                   > skip profile-guided optimization" && \
	echo "net                     > Download the default nnue nets" && \
	echo "strip                   > Strip executable" && \
	echo "install                 > Install executable" && \
	echo "clean                   > Clean up" && \
	echo "" && \
	echo "Supported archs:" && \
	echo "" && \
	echo "native                  > select the best architecture for the host processor (default)" && \
	echo "x86-64-vnni512          > x86 64-bit with vnni 512bit support" && \
	echo "x86-64-vnni256          > x86 64-bit with vnni 512bit support, limit operands to 256bit wide" && \
	echo "x86-64-avx512           > x86 64-bit with avx512 support" && \
	echo "x86-64-avxvnni          > x86 64-bit with vnni 256bit support" && \
	echo "x86-64-bmi2             > x86 64-bit with bmi2 support" && \
	echo "x86-64-avx2             > x86 64-bit with avx2 support" && \
	echo "x86-64-sse41-popcnt     > x86 64-bit with sse41 and popcnt support" && \
	echo "x86-64-modern           > deprecated, currently x86-64-sse41-popcnt" && \
	echo "x86-64-ssse3            > x86 64-bit with ssse3 support" && \
	echo "x86-64-sse3-popcnt      > x86 64-bit with sse3 compile and popcnt support" && \
	echo "x86-64                  > x86 64-bit generic (with sse2 support)" && \
	echo "x86-32-sse41-popcnt     > x86 32-bit with sse41 and popcnt support" && \
	echo "x86-32-sse2             > x86 32-bit with sse2 support" && \
	echo "x86-32                  > x86 32-bit generic (with mmx compile support)" && \
	echo "ppc-64                  > PPC 64-bit" && \
	echo "ppc-64-altivec          > PPC 64-bit with altivec support" && \
	echo "ppc-64-vsx              > PPC 64-bit with vsx support" && \
	echo "ppc-32                  > PPC 32-bit" && \
	echo "armv7                   > ARMv7 32-bit" && \
	echo "armv7-neon              > ARMv7 32-bit with popcnt and neon" && \
	echo "armv8                   > ARMv8 64-bit with popcnt and neon" && \
	echo "armv8-dotprod           > ARMv8 64-bit with popcnt, neon and dot product support" && \
	echo "e2k                     > Elbrus 2000" && \
	echo "apple-silicon           > Apple silicon ARM64" && \
	echo "general-64              > unspecified 64-bit" && \
	echo "general-32              > unspecified 32-bit" && \
	echo "riscv64                 > RISC-V 64-bit" && \
	echo "loongarch64             > LoongArch 64-bit" && \
	echo "loongarch64-lsx         > LoongArch 64-bit with SIMD eXtension" && \
	echo "loongarch64-lasx        > LoongArch 64-bit with Advanced SIMD eXtension" && \
	echo "" && \
	echo "Supported compilers:" && \
	echo "" && \
	echo "gcc                     > GNU compiler (default)" && \
	echo "mingw                   > GNU compiler with MinGW under Windows" && \
	echo "clang                   > LLVM Clang compiler" && \
	echo "icx                     > Intel oneAPI DPC++/C++ Compiler" && \
	echo "ndk                     > Google NDK to cross-compile for Android" && \
	echo "" && \
	echo "Simple examples. If you don't know what to do, you likely want to run one of: " && \
	echo "" && \
	echo "make -j profile-build ARCH=x86-64-avx2    # typically a fast compile for common systems " && \
	echo "make -j profile-build ARCH=x86-64-sse41-popcnt  # A more portable compile for 64-bit systems " && \
	echo "make -j profile-build ARCH=x86-64         # A portable compile for 64-bit systems " && \
	echo "" && \
	echo "Advanced examples, for experienced users: " && \
	echo "" && \
	echo "make -j profile-build ARCH=x86-64-avxvnni" && \
	echo "make -j profile-build ARCH=x86-64-avxvnni COMP=gcc COMPCXX=g++-12.0" && \
	echo "make -j build ARCH=x86-64-ssse3 COMP=clang" && \
	echo ""
ifneq ($(SUPPORTED_ARCH), true)
	@echo "Specify a supported architecture with the ARCH option for more details"
	@echo ""
endif


.PHONY: help analyze build profile-build strip install clean net \
	objclean profileclean config-sanity \
	icx-profile-use icx-profile-make \
	gcc-profile-use gcc-profile-make \
	clang-profile-use clang-profile-make FORCE \
	format analyze

analyze: net config-sanity objclean
	$(MAKE) -k ARCH=$(ARCH) COMP=$(COMP) $(OBJS)

build: net config-sanity
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) all

profile-build: net config-sanity objclean profileclean
	@echo ""
	@echo "Step 1/4. Building instrumented executable ..."
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) $(profile_make)
	@echo ""
	@echo "Step 2/4. Running benchmark for pgo-build ..."
	$(PGOBENCH) > PGOBENCH.out 2>&1
	tail -n 4 PGOBENCH.out
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

# clean binaries and objects
objclean:
	@rm -f stockfish stockfish.exe *.o ./syzygy/*.o ./nnue/*.o ./nnue/features/*.o

# clean auxiliary profiling files
profileclean:
	@rm -rf profdir
	@rm -f bench.txt *.gcda *.gcno ./syzygy/*.gcda ./nnue/*.gcda ./nnue/features/*.gcda *.s PGOBENCH.out
	@rm -f stockfish.profdata *.profraw
	@rm -f stockfish.*args*
	@rm -f stockfish.*lt*
	@rm -f stockfish.res
	@rm -f ./-lstdc++.res

# evaluation network (nnue)
net:
	@$(SHELL) ../scripts/net.sh

format:
	$(CLANG-FORMAT) -i $(SRCS) $(HEADERS) -style=file

# default target
default:
	help

### ==========================================================================
### Section 5. Private Targets
### ==========================================================================

all: $(EXE) .depend

config-sanity: net
	@echo ""
	@echo "Config:" && \
	echo "debug: '$(debug)'" && \
	echo "sanitize: '$(sanitize)'" && \
	echo "optimize: '$(optimize)'" && \
	echo "arch: '$(arch)'" && \
	echo "bits: '$(bits)'" && \
	echo "kernel: '$(KERNEL)'" && \
	echo "os: '$(OS)'" && \
	echo "prefetch: '$(prefetch)'" && \
	echo "popcnt: '$(popcnt)'" && \
	echo "pext: '$(pext)'" && \
	echo "sse: '$(sse)'" && \
	echo "mmx: '$(mmx)'" && \
	echo "sse2: '$(sse2)'" && \
	echo "ssse3: '$(ssse3)'" && \
	echo "sse41: '$(sse41)'" && \
	echo "avx2: '$(avx2)'" && \
	echo "avxvnni: '$(avxvnni)'" && \
	echo "avx512: '$(avx512)'" && \
	echo "vnni256: '$(vnni256)'" && \
	echo "vnni512: '$(vnni512)'" && \
	echo "altivec: '$(altivec)'" && \
	echo "vsx: '$(vsx)'" && \
	echo "neon: '$(neon)'" && \
	echo "dotprod: '$(dotprod)'" && \
	echo "arm_version: '$(arm_version)'" && \
	echo "lsx: '$(lsx)'" && \
	echo "lasx: '$(lasx)'" && \
	echo "target_windows: '$(target_windows)'" && \
	echo "" && \
	echo "Flags:" && \
	echo "CXX: $(CXX)" && \
	echo "CXXFLAGS: $(CXXFLAGS)" && \
	echo "LDFLAGS: $(LDFLAGS)" && \
	echo "" && \
	echo "Testing config sanity. If this fails, try 'make help' ..." && \
	echo "" && \
	(test "$(debug)" = "yes" || test "$(debug)" = "no") && \
	(test "$(optimize)" = "yes" || test "$(optimize)" = "no") && \
	(test "$(SUPPORTED_ARCH)" = "true") && \
	(test "$(arch)" = "any" || test "$(arch)" = "x86_64" || test "$(arch)" = "i386" || \
	 test "$(arch)" = "ppc64" || test "$(arch)" = "ppc" || test "$(arch)" = "e2k" || \
	 test "$(arch)" = "armv7" || test "$(arch)" = "armv8" || test "$(arch)" = "arm64" || \
	 test "$(arch)" = "riscv64" || test "$(arch)" = "loongarch64") && \
	(test "$(bits)" = "32" || test "$(bits)" = "64") && \
	(test "$(prefetch)" = "yes" || test "$(prefetch)" = "no") && \
	(test "$(popcnt)" = "yes" || test "$(popcnt)" = "no") && \
	(test "$(pext)" = "yes" || test "$(pext)" = "no") && \
	(test "$(sse)" = "yes" || test "$(sse)" = "no") && \
	(test "$(mmx)" = "yes" || test "$(mmx)" = "no") && \
	(test "$(sse2)" = "yes" || test "$(sse2)" = "no") && \
	(test "$(ssse3)" = "yes" || test "$(ssse3)" = "no") && \
	(test "$(sse41)" = "yes" || test "$(sse41)" = "no") && \
	(test "$(avx2)" = "yes" || test "$(avx2)" = "no") && \
	(test "$(avx512)" = "yes" || test "$(avx512)" = "no") && \
	(test "$(vnni256)" = "yes" || test "$(vnni256)" = "no") && \
	(test "$(vnni512)" = "yes" || test "$(vnni512)" = "no") && \
	(test "$(altivec)" = "yes" || test "$(altivec)" = "no") && \
	(test "$(vsx)" = "yes" || test "$(vsx)" = "no") && \
	(test "$(neon)" = "yes" || test "$(neon)" = "no") && \
	(test "$(lsx)" = "yes" || test "$(lsx)" = "no") && \
	(test "$(lasx)" = "yes" || test "$(lasx)" = "no") && \
	(test "$(comp)" = "gcc" || test "$(comp)" = "icx" || test "$(comp)" = "mingw" || \
	 test "$(comp)" = "clang" || test "$(comp)" = "armv7a-linux-androideabi16-clang" || \
	 test "$(comp)" = "aarch64-linux-android21-clang")

$(EXE): $(OBJS)
	+$(CXX) -o $@ $(OBJS) $(LDFLAGS)

# Force recompilation to ensure version info is up-to-date
misc.o: FORCE
FORCE:

clang-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-generate ' \
	EXTRALDFLAGS=' -fprofile-generate' \
	all

clang-profile-use:
	$(XCRUN) llvm-profdata merge -output=stockfish.profdata *.profraw
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-use=stockfish.profdata' \
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

icx-profile-make:
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-generate ' \
	EXTRALDFLAGS=' -fprofile-instr-generate' \
	all

icx-profile-use:
	$(XCRUN) llvm-profdata merge -output=stockfish.profdata *.profraw
	$(MAKE) ARCH=$(ARCH) COMP=$(COMP) \
	EXTRACXXFLAGS='-fprofile-instr-use=stockfish.profdata' \
	EXTRALDFLAGS='-fprofile-use ' \
	all

.depend: $(SRCS)
	-@$(CXX) $(DEPENDFLAGS) -MM $(SRCS) > $@ 2> /dev/null

ifeq (, $(filter $(MAKECMDGOALS), help strip install clean net objclean profileclean config-sanity))
-include .depend
endif
