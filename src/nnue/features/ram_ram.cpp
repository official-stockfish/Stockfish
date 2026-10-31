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


#include "ram_ram.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"
#include "../nnue_feature_transformer.h"

namespace Stockfish::Eval::NNUE::Features {

namespace {

sf_always_inline IndexType make_ram_id(Color perspective, u8 orientation, Square w) {
    const Square lower = perspective == WHITE ? w : w + NORTH;
    assert((u8(lower) ^ orientation) >= SQ_A2 && (u8(lower) ^ orientation) <= SQ_H6);
    return (u8(lower) ^ orientation) - SQ_A2;
}

// Pairs each ram in newRams with every ram in oldRams and with every other ram in newRams
void append_pairs(Color                   perspective,
                  Square                  ksq,
                  Bitboard                newRams,
                  Bitboard                oldRams,
                  RamRam::IndexList&      list,
                  const ThreatWeightType* prefetchBase   = nullptr,
                  IndexType               prefetchStride = 0) {
    const u8 orientation = OrientTBL[ksq] ^ (56 * perspective);

    while (newRams)
    {
        const IndexType a = make_ram_id(perspective, orientation, pop_lsb(newRams));

        for (Bitboard others = oldRams | newRams; others;)
        {
            const IndexType b     = make_ram_id(perspective, orientation, pop_lsb(others));
            const IndexType hi    = std::max(a, b);
            const IndexType lo    = std::min(a, b);
            const IndexType index = FeatureTransformer::RamIndexBase + hi * (hi - 1) / 2 + lo;

            if (prefetchBase)
                prefetch<PrefetchRw::READ, PrefetchLoc::LOW>(reinterpret_cast<const void*>(
                  reinterpret_cast<uintptr_t>(prefetchBase) + uintptr_t(index) * prefetchStride));
            list.push_back(index);
        }
    }
}

}  // namespace

void RamRam::append_active_indices(Color perspective, const Position& pos, IndexList& active) {
    const Bitboard pawns[COLOR_NB] = {pos.pieces(WHITE, PAWN), pos.pieces(BLACK, PAWN)};
    append_pairs(perspective, pos.square<KING>(perspective), rams(pawns), 0, active);
}

void RamRam::append_ram_changes(Color                   perspective,
                                Square                  ksq,
                                const DiffType&         diff,
                                IndexList&              removed,
                                IndexList&              added,
                                const ThreatWeightType* prefetchBase,
                                IndexType               prefetchStride) {
    const Bitboard before = rams(diff.before);
    const Bitboard after  = rams(diff.after);
    const Bitboard kept   = before & after;

    append_pairs(perspective, ksq, before & ~after, kept, removed, prefetchBase, prefetchStride);
    append_pairs(perspective, ksq, after & ~before, kept, added, prefetchBase, prefetchStride);
}

}  // namespace Stockfish::Eval::NNUE::Features
