# Glaurung, a UCI chess playing engine.
# Copyright (C) 2004-2007 Tord Romstad

# This file is part of Glaurung.
#
# Glaurung is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Glaurung is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


###
### Files
###

EXE = glaurung

OBJS = bitboard.o color.o pawns.o material.o endgame.o evaluate.o main.o \
	misc.o move.o movegen.o history.o movepick.o search.o piece.o \
	position.o square.o direction.o tt.o value.o uci.o ucioption.o \
	mersenne.o book.o bitbase.o san.o benchmark.o


###
### Rules
###

all: $(EXE) .depend

clean: 
	$(RM) *.o .depend glaurung


###
### Compiler:
###

CXX = g++
# CXX = g++-4.2
# CXX = icpc


###
### Dependencies
###

$(EXE): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS)

.depend:
	$(CXX) -MM $(OBJS:.o=.cpp) > $@

include .depend


###
### Compiler and linker switches
###

# Enable/disable debugging:

CXXFLAGS += -DNDEBUG


# Compile with full warnings, and symbol names

CXXFLAGS += -Wall -g


# General optimization flags.  Note that -O2 might be faster than -O3 on some
# systems; this requires testing.

CXXFLAGS += -O3 -fno-exceptions -fomit-frame-pointer -fno-rtti -fstrict-aliasing


# Compiler optimization flags for the Intel C++ compiler in Mac OS X:

# CXXFLAGS += -mdynamic-no-pic -no-prec-div -ipo -static -xP


# Profiler guided optimization with the Intel C++ compiler.  To use it, first
# create the directory ./profdata if it does not already exist, and delete its
# contents if it does exist.  Then compile with -prof_gen, and run the 
# resulting binary for a while (for instance, do ./glaurung bench 128 1, and
# wait 15 minutes for the benchmark to complete).  Then do a 'make clean', and 
# recompile with -prof_use.

# CXXFLAGS += -prof_gen -prof_dir ./profdata
# CXXFLAGS += -prof_use -prof_dir ./profdata


# Profiler guided optimization with GCC.  I've never been able to make this 
# work.

# CXXFLAGS += -fprofile-generate
# LDFLAGS += -fprofile-generate
# CXXFLAGS += -fprofile-use
# CXXFLAGS += -fprofile-use


# General linker flags

LDFLAGS += -lm -lpthread


# Compiler switches for generating binaries for various CPUs in Mac OS X.
# Note that 'arch ppc' and 'arch ppc64' only works with g++, and not with
# the intel compiler.

# CXXFLAGS += -arch ppc
# CXXFLAGS += -arch ppc64
# CXXFLAGS += -arch i386
# CXXFLAGS += -arch x86_64
# LDFLAGS += -arch ppc
# LDFLAGS += -arch ppc64
# LDFLAGS += -arch i386
# LDFLAGS += -arch x86_64


# Backwards compatibility with Mac OS X 10.4 when compiling under 10.5 with 
# GCC 4.0.  I haven't found a way to make it work with GCC 4.2.

# CXXFLAGS += -isysroot /Developer/SDKs/MacOSX10.4u.sdk
# CXXFLAGS += -mmacosx-version-min=10.4
# LDFLAGS += -isysroot /Developer/SDKs/MacOSX10.4u.sdk
# LDFLAGS += -Wl,-syslibroot /Developer/SDKs/MacOSX10.4u.sdk
# LDFLAGS += -mmacosx-version-min=10.4


# Backwards compatibility with Mac OS X 10.4 when compiling with ICC.  Doesn't
# work yet. :-(

# CXXFLAGS += -DMAC_OS_X_VERSION_MIN_REQUIRED=1040
# CXXFLAGS += -DMAC_OS_X_VERSION_MAX_ALLOWED=1040
# CXXFLAGS += -D__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__=1040
# CXXFLAGS += -F/Developer/SDKs/MacOSX10.4u.sdk/
# LDFLAGS += -Wl,-syslibroot -Wl,/Developer/SDKs/MacOSX10.4u.sdk
