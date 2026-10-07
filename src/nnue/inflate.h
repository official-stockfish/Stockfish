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

#ifndef NNUE_INFLATE_H_INCLUDED
#define NNUE_INFLATE_H_INCLUDED

#include <cstddef>
#include "../types.h"

namespace Stockfish::Eval::NNUE {

// Decompress zlib-wrapped deflate data from src into dst.
// Returns true on success, false on failure or corrupted stream.
bool decompress_zlib(const void* src, usize srcLen, void* dst, usize dstLen);

}  // namespace Stockfish::Eval::NNUE

#endif  // #ifndef NNUE_INFLATE_H_INCLUDED
