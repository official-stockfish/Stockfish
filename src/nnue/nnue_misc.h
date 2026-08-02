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

#ifndef NNUE_MISC_H_INCLUDED
#define NNUE_MISC_H_INCLUDED

#include <string>
#include <string_view>
#include <optional>
#include <filesystem>

#include "../misc.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "../evaluate.h"

namespace Stockfish {

class Position;

namespace Eval::NNUE {

// NNUE file metadata uses fixed string types so it stays trivially copyable and cheap to move
// around between the engine and the network loader.
struct EvalFile {
    // Default net name, will use one of the EvalFileDefaultName* macros defined
    // in evaluate.h
    constexpr static std::string_view defaultName = EvalFileDefaultName;
    // Selected net name, either via uci option or default
    std::optional<std::filesystem::path> current;
    // Net description extracted from the net file
    std::string netDescription;
};

struct NnueEvalTrace {
    static_assert(LayerStacks == PSQTBuckets);

    Value psqt[LayerStacks];
    Value positional[LayerStacks];
    usize correctBucket;
};

class Network;
struct AccumulatorCaches;

std::string trace(Position& pos, const Network& network, AccumulatorCaches& caches);

}  // namespace Stockfish::Eval::NNUE
}  // namespace Stockfish

#endif  // #ifndef NNUE_MISC_H_INCLUDED
