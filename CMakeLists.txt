# Stockfish, a UCI chess playing engine derived from Glaurung 2.1
#
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

cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

# Use debug builds by default.
# See README.md for instructions on how to build Release builds.
set(CMAKE_BUILD_TYPE_INIT "Debug")

project(Stockfish)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

include(EnableFlags)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build flags
option(ENABLE_LTO "Enable link-time optimization" OFF)
option(ENABLE_PROFILE "Enable profiling (generate|use)" OFF)
option(ENABLE_SANITIZE "Enable sanitizer (address|thread|undefined)" OFF)

# Optional processor flags
option(USE_AVX2 "Enable avx2 instructions" OFF)
option(USE_AVX512 "Enable avx512 instructions" OFF)
option(USE_PEXT "Enable bmi2 instructions" OFF)
option(USE_POPCNT "Enable popcnt instructions" ON)
option(USE_SSE42 "Enable sse4.2 instructions" OFF)
option(USE_VNNI "Enable vnni instructions" OFF)

##### Create stockfish library #####

add_library(stockfish
    src/nnue/architectures/halfkp_256x2-32-32.h
    src/nnue/evaluate_nnue.cpp
    src/nnue/evaluate_nnue.h
    src/nnue/features/features_common.h
    src/nnue/features/feature_set.h
    src/nnue/features/half_kp.cpp
    src/nnue/features/half_kp.h
    src/nnue/features/index_list.h
    src/nnue/layers/affine_transform.h
    src/nnue/layers/clipped_relu.h
    src/nnue/layers/input_slice.h
    src/nnue/nnue_accumulator.h
    src/nnue/nnue_architecture.h
    src/nnue/nnue_common.h
    src/nnue/nnue_feature_transformer.h
    src/syzygy/tbprobe.cpp
    src/syzygy/tbprobe.h
    src/benchmark.cpp
    src/bitbase.cpp
    src/bitboard.cpp
    src/bitboard.h
    src/endgame.cpp
    src/endgame.h
    src/evaluate.cpp
    src/evaluate.h
    src/material.cpp
    src/material.h
    src/misc.cpp
    src/misc.h
    src/movegen.cpp
    src/movegen.h
    src/movepick.cpp
    src/movepick.h
    src/pawns.cpp
    src/pawns.h
    src/position.cpp
    src/position.h
    src/psqt.cpp
    src/search.cpp
    src/search.h
    src/thread.cpp
    src/thread.h
    src/thread_win32_osx.h
    src/timeman.cpp
    src/timeman.h
    src/tt.cpp
    src/tt.h
    src/tune.cpp
    src/tune.h
    src/types.h
    src/uci.cpp
    src/uci.h
    src/ucioption.cpp
)
add_library(Stockfish::Stockfish ALIAS stockfish)

##### Create stockfish binary #####

add_executable(stockfishbin src/main.cpp)

##### Download the neural network #####

add_custom_target(net
    COMMAND ${CMAKE_COMMAND}
        -D SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -D BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DownloadNet.cmake
    DEPENDS src/ucioption.cpp
    BYPRODUCTS "nn-*.nnue"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Downloading network"
)

##### Populate flags #####

enable_flags(stockfish)
enable_flags(stockfishbin)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    enable_optimize(stockfish)
    enable_optimize(stockfishbin)
endif()

if(ENABLE_LTO)
    enable_lto(stockfish)
    enable_lto(stockfishbin)
endif()

if(ENABLE_SANITIZE)
    enable_sanitizer(stockfish "${ENABLE_SANITIZE}")
    enable_sanitizer(stockfishbin "${ENABLE_SANITIZE}")
endif()

if(ENABLE_PROFILE)
    enable_profiling(stockfish "${ENABLE_PROFILE}")
    enable_profiling(stockfishbin "${ENABLE_PROFILE}")
endif()

# Set output binary name to be "stockfish" (not "stockfishbin")
set_property(TARGET stockfishbin PROPERTY OUTPUT_NAME stockfish)

# Make stockfishbin depend on the stockfish library.
target_link_libraries(stockfishbin PUBLIC Stockfish::Stockfish)

##### Install #####

# Install the stockfish binary
install(TARGETS stockfishbin DESTINATION ${CMAKE_INSTALL_BINDIR})
