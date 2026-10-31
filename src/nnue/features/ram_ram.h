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


// Definition of input features Ram_Ram of NNUE evaluation function: pairs of rams,
// a ram being a pawn blocked head-on by an enemy pawn

#ifndef NNUE_FEATURES_RAM_RAM_INCLUDED
#define NNUE_FEATURES_RAM_RAM_INCLUDED

#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish {
class Position;
}

namespace Stockfish::Eval::NNUE::Features {

class RamRam {
   public:
    static constexpr u32 HashValue = 0x5e9fcac8u;

    // A ram is identified by the oriented square of its lower pawn (ranks 2 to 6)
    static constexpr IndexType RamIds     = 40;
    static constexpr IndexType Dimensions = RamIds * (RamIds - 1) / 2;

    using IndexList = ValueList<u16, 256>;
    using DiffType  = DirtyPawnPairs;

    static void append_active_indices(Color perspective, const Position& pos, IndexList& active);

    static void append_changed_indices(Color                   perspective,
                                       Square                  ksq,
                                       const DiffType&         diff,
                                       IndexList&              removed,
                                       IndexList&              added,
                                       const ThreatWeightType* prefetchBase   = nullptr,
                                       IndexType               prefetchStride = 0) {
        if (rams(diff.before) != rams(diff.after))
            append_ram_changes(perspective, ksq, diff, removed, added, prefetchBase,
                               prefetchStride);
    }

    static void append_changed_indices_both(Square                  white_ksq,
                                            Square                  black_ksq,
                                            const DiffType&         diff,
                                            IndexList&              white_removed,
                                            IndexList&              white_added,
                                            IndexList&              black_removed,
                                            IndexList&              black_added,
                                            const ThreatWeightType* prefetchBase   = nullptr,
                                            IndexType               prefetchStride = 0) {
        append_changed_indices(WHITE, white_ksq, diff, white_removed, white_added, prefetchBase,
                               prefetchStride);
        append_changed_indices(BLACK, black_ksq, diff, black_removed, black_added, prefetchBase,
                               prefetchStride);
    }

   private:
    static constexpr Bitboard rams(const Bitboard pawns[COLOR_NB]) {
        return pawns[WHITE] & (pawns[BLACK] >> 8);
    }

    static void append_ram_changes(Color                   perspective,
                                   Square                  ksq,
                                   const DiffType&         diff,
                                   IndexList&              removed,
                                   IndexList&              added,
                                   const ThreatWeightType* prefetchBase,
                                   IndexType               prefetchStride);
};

}  // namespace Stockfish::Eval::NNUE::Features

#endif  // #ifndef NNUE_FEATURES_RAM_RAM_INCLUDED
