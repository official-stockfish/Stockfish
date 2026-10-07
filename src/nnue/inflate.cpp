/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "inflate.h"

#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_MALLOC

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "miniz.h"
#include "miniz.c"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace Stockfish::Eval::NNUE {

bool decompress_zlib(const void* src, usize srcLen, void* dst, usize dstLen) {
    size_t inLen = srcLen;
    size_t result = tinfl_decompress_mem_to_mem(
        dst, dstLen,
        src, inLen,
        TINFL_FLAG_PARSE_ZLIB_HEADER
    );
    return result == dstLen;
}

}  // namespace Stockfish::Eval::NNUE
