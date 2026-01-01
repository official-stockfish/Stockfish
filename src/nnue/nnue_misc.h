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

#include <cstddef>
#include <memory>
#include <string>

#include "../misc.h"
#include "../types.h"
#include "nnue_architecture.h"

namespace Stockfish {

class Position;

namespace Eval::NNUE {

// EvalFile uses fixed string types because it's part of the network structure which must be trivial.
struct EvalFile {
    // Default net name, will use one of the EvalFileDefaultName* macros defined
    // in evaluate.h
    FixedString<256> defaultName;
    // Selected net name, either via uci option or default
    FixedString<256> current;
    // Net description extracted from the net file
    FixedString<256> netDescription;
};

struct NnueEvalTrace {
    static_assert(LayerStacks == PSQTBuckets);

    Value       psqt[LayerStacks];
    Value       positional[LayerStacks];
    std::size_t correctBucket;
};

struct Networks;
struct AccumulatorCaches;

std::string trace(Position& pos, const Networks& networks, AccumulatorCaches& caches);

}  // namespace Stockfish::Eval::NNUE
}  // namespace Stockfish

template<>
struct std::hash<Stockfish::Eval::NNUE::EvalFile> {
    std::size_t operator()(const Stockfish::Eval::NNUE::EvalFile& evalFile) const noexcept {
        std::size_t h = 0;
        Stockfish::hash_combine(h, evalFile.defaultName);
        Stockfish::hash_combine(h, evalFile.current);
        Stockfish::hash_combine(h, evalFile.netDescription);
        return h;
    }
};

#endif  // #ifndef NNUE_MISC_H_INCLUDED
