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

#include "evaluate.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

#include "misc.h"
#include "nnue/network.h"
#include "nnue/nnue_misc.h"
#include "position.h"
#include "types.h"
#include "uci.h"
#include "nnue/nnue_accumulator.h"

namespace Stockfish {

int Eval::simple_eval(const Position& pos) {
    const Color c = pos.side_to_move();
    return PawnValue * (pos.count<PAWN>(c) - pos.count<PAWN>(~c)) + pos.non_pawn_material(c)
         - pos.non_pawn_material(~c);
}

Value scale_evaluation(Value nnue, int optimism, const Position& pos);

Value Eval::evaluate(const Eval::NNUE::Network&     network,
                     const Position&                pos,
                     Eval::NNUE::AccumulatorStack&  accumulators,
                     Eval::NNUE::AccumulatorCaches& caches,
                     int                            optimism) {

    assert(!pos.checkers());
    Value nnue = network.evaluate(pos, accumulators, caches);
    return scale_evaluation(nnue, optimism, pos);
}

// Applies search-dependent scaling (optimism and rule50) to the raw NNUE eval
Value scale_evaluation(Value nnue, int optimism, const Position& pos) {
    int se = Eval::simple_eval(pos);

    // 1. Normalize the raw evaluations to [-1024, 1024] to measure their correlation.
    int se_norm   = (se * 1024) / (std::abs(se) + 1024);
    int nnue_norm = (nnue * 1024) / (std::abs(nnue) + 1024);

    // 2. Measure positional difficulty, "alignment". When NNUE and material agree, the position is
    // straightforward; otherwise, it involves complex compensation. In a representative sample,
    // raw_alignment averages -1 or so, i.e. well-centered in [-2048, 2048].
    int raw_alignment = (se_norm * nnue_norm) / 512;
    // Shift it to a positive range: [hard, average, easy] -> [0, 2048, 4096].
    int alignment = raw_alignment + 2048;

    // 3. Blend optimism and NNUE according to the alignment.
    // We favor easy positions by heavily boosting optimism when alignment is high.
    // Conversely, in hard positions, the optimism boost is minimized.
    // To maintain overall evaluation scale, the static NNUE score is dampened proportionally.
    // At average alignment of 2047, the optimism boost is around 1.5x.
    optimism += (optimism * alignment) / 4096;
    nnue     -= (i64(nnue) * alignment) / 131072;

    int base_eval = nnue + (optimism * 7674) / 90649;

    // 4. Scale the combined evaluation by material volume.
    // Higher material on the board amplifies the final evaluation magnitude.
    int material = 521 * pos.count<PAWN>() + pos.non_pawn_material();
    int v = (base_eval * i64(90649 + material)) / 90649;

    // 5. Damp down the evaluation linearly when shuffling
    v -= v * pos.rule50_count() / 189;

    // 6. Guarantee that the evaluation does not hit the tablebase range
    v = std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);

    return v;
}

// Like evaluate(), but instead of returning a value, it returns
// a string (suitable for outputting to stdout) that contains the detailed
// descriptions and values of each evaluation term. Useful for debugging.
// Trace scores are from white's point of view
std::string Eval::trace(Position& pos, const Eval::NNUE::Network& network) {

    if (pos.checkers())
        return "Final evaluation: none (in check)";

    auto accumulators = std::make_unique<Eval::NNUE::AccumulatorStack>();
    auto caches       = std::make_unique<Eval::NNUE::AccumulatorCaches>(network);

    std::stringstream ss;
    ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    ss << '\n' << NNUE::trace(pos, network, *caches) << '\n';

    ss << std::showpoint << std::showpos << std::fixed << std::setprecision(2) << std::setw(15);

    Value nnue = network.evaluate(pos, *accumulators, *caches);
    Value s_v  = scale_evaluation(nnue, VALUE_ZERO, pos); // requires stm perspective

    ss << "NNUE evaluation          " << nnue << " (side to move, internal units)\n";

    nnue = pos.side_to_move() == WHITE ? nnue : -nnue;
    s_v  = pos.side_to_move() == WHITE ? s_v : -s_v;

    ss << "NNUE evaluation        " << 0.01 * UCIEngine::to_cp(nnue, pos) << " (white side, pawns)\n";
    ss << "Final evaluation      ";
    ss << 0.01 * UCIEngine::to_cp(s_v, pos) << " (white side, pawns)";
    ss << " [with scaled NNUE, ...]\n";

    return ss.str();
}

}  // namespace Stockfish
