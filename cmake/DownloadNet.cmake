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


# CMake script for downloading the neural network file.
#
# NOTE: Run this in CMake script mode (`cmake -P`)!!

file(STRINGS "${SOURCE_DIR}/src/ucioption.cpp" EVALFILE REGEX "[^\n]+EvalFile[^\n]+")
string(REGEX MATCH "(nn-[a-z0-9]+.nnue)" NNUE "${EVALFILE}")
if(NOT NNUE)
    message(FATAL_ERROR "Could not extract NNUE filename")
endif()

message("Default net: ${NNUE}")
set(NNUE_FILE "${BUILD_DIR}/${NNUE}")
set(NNUE_URL "https://tests.stockfishchess.org/api/nn/${NNUE}")

if(NOT EXISTS "${NNUE}")
    message("Downloading ${NNUE_URL}")
    file(DOWNLOAD "${NNUE_URL}" "${NNUE_FILE}")

    file(SHA256 ${NNUE_FILE} SHASUM)
    string(SUBSTRING ${SHASUM} 0 12 ACTUAL)
    string(SUBSTRING ${NNUE} 3 12 EXPECTED)

    if(NOT ACTUAL STREQUAL EXPECTED)
        message(FATAL_ERROR "Download failed or corrupted - please delete ${NNUE_FILE}")
    endif()
else()
    message("Already available.")
endif()
