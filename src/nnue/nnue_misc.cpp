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

// Code for calculating NNUE evaluation function

#include "nnue_misc.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iosfwd>
#include <iostream>
#include <sstream>

#include "../position.h"
#include "../misc.h"
#include "../types.h"
#include "../uci.h"
#include "network.h"
#include "nnue_accumulator.h"

namespace Stockfish::Eval::NNUE {


namespace {


// Converts a Value into pawns, always keeping two decimals
void format_cp_aligned_dot(Value v, std::stringstream& stream, const Position& pos) {

    const double pawns = std::abs(0.01 * UCIEngine::to_cp(v, pos));

    stream << (v < 0   ? '-'
               : v > 0 ? '+'
                       : ' ')
           << std::setiosflags(std::ios::fixed) << std::setw(6) << std::setprecision(2) << pawns;
}
}


// Returns a string with the value of each piece on a board,
// and a table for (PSQT, Layers) values bucket by bucket.
std::string
trace(Position& pos, const Eval::NNUE::Network& network, Eval::NNUE::AccumulatorCaches& caches) {

    std::stringstream ss;

    auto accumulators = std::make_unique<AccumulatorStack>();
    accumulators->reset();

    auto t = network.trace_evaluate(pos, *accumulators, caches);

    ss << "NNUE network contributions (Normalized, "
       << (pos.side_to_move() == WHITE ? "White to move)" : "Black to move)") << std::endl
       << "+------------+------------+------------+------------+\n"
       << "|   Bucket   |  Material  | Positional |   Total    |\n"
       << "|            |   (PSQT)   |  (Layers)  |            |\n"
       << "+------------+------------+------------+------------+\n";

    for (usize bucket = 0; bucket < LayerStacks; ++bucket)
    {
        ss << "|  " << bucket << "        "  //
           << " |  ";
        format_cp_aligned_dot(t.psqt[bucket], ss, pos);
        ss << "  "  //
           << " |  ";
        format_cp_aligned_dot(t.positional[bucket], ss, pos);
        ss << "  "  //
           << " |  ";
        format_cp_aligned_dot(t.psqt[bucket] + t.positional[bucket], ss, pos);
        ss << "  "  //
           << " |";
        if (bucket == t.correctBucket)
            ss << " <-- this bucket is used";
        ss << '\n';
    }

    ss << "+------------+------------+------------+------------+\n";

    return ss.str();
}


}  // namespace Stockfish::Eval::NNUE
