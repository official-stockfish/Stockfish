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

#ifndef NNUE_FEATURES_PP_3WIDE_INCLUDED
#define NNUE_FEATURES_PP_3WIDE_INCLUDED

#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish {
class Position;
}

namespace Stockfish::Eval::NNUE::Features {

class PP_3Wide {
   public:
    static constexpr u32 HashValue = 0x86f2b1ddu;

    static constexpr IndexType PawnIds    = COLOR_NB * 48;
    static constexpr IndexType Dimensions = PawnIds * (PawnIds - 1) / 2;

    // Pawn pair feature indices are concatenated to threats, so this must equal ThreatFeatureSet::Dimensions;
    // see nnue_feature_transformer.h
    static constexpr IndexType IndexBase = 59808;
    using IndexList                      = ValueList<u16, 256>;
    using DiffType                       = DirtyPawnPairs;

    static IndexType make_index(
      Color perspective, Color color, Square from, Square to, Color pairedColor, Square ksq);

    static void append_active_indices(Color perspective, const Position& pos, IndexList& active);

    static void append_changed_indices(Color                   perspective,
                                       Square                  ksq,
                                       const DiffType&         diff,
                                       IndexList&              removed,
                                       IndexList&              added,
                                       const ThreatWeightType* prefetchBase   = nullptr,
                                       IndexType               prefetchStride = 0);
};

}  // namespace Stockfish::Eval::NNUE::Features

#endif  // #ifndef NNUE_FEATURES_PP_3WIDE_INCLUDED
