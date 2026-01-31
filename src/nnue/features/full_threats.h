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

//Definition of input features Simplified_Threats of NNUE evaluation function

#ifndef NNUE_FEATURES_FULL_THREATS_INCLUDED
#define NNUE_FEATURES_FULL_THREATS_INCLUDED

#include <cstdint>

#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace Stockfish {
class Position;
}

namespace Stockfish::Eval::NNUE::Features {

static constexpr int numValidTargets[PIECE_NB] = {0, 6, 10, 8, 8, 10, 8, 0,
                                                  0, 6, 10, 8, 8, 10, 8, 0};

class FullThreats {
   public:
    // Feature name
    static constexpr const char* Name = "Full_Threats(Friend)";

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t HashValue = 0x8f234cb8u;

    // Number of feature dimensions
    static constexpr IndexType Dimensions = 66864;

    // clang-format off
    // Orient a square according to perspective (rotates by 180 for black)
    static constexpr std::int8_t OrientTBL[SQUARE_NB] = {
        SQ_A1, SQ_A1, SQ_A1, SQ_A1, SQ_H1, SQ_H1, SQ_H1, SQ_H1,
        SQ_A1, SQ_A1, SQ_A1, SQ_A1, SQ_H1, SQ_H1, SQ_H1, SQ_H1,
        SQ_A1, SQ_A1, SQ_A1, SQ_A1, SQ_H1, SQ_H1, SQ_H1, SQ_H1,
        SQ_A1, SQ_A1, SQ_A1, SQ_A1, SQ_H1, SQ_H1, SQ_H1, SQ_H1,
        SQ_A1, SQ_A1, SQ_A1, SQ_A1, SQ_H1, SQ_H1, SQ_H1, SQ_H1,
        SQ_A1, SQ_A1, SQ_A1, SQ_A1, SQ_H1, SQ_H1, SQ_H1, SQ_H1,
        SQ_A1, SQ_A1, SQ_A1, SQ_A1, SQ_H1, SQ_H1, SQ_H1, SQ_H1,
        SQ_A1, SQ_A1, SQ_A1, SQ_A1, SQ_H1, SQ_H1, SQ_H1, SQ_H1,
    };

    static constexpr int map[PIECE_TYPE_NB-2][PIECE_TYPE_NB-2] = {
      {0,  1, -1,  2, -1, -1},
      {0,  1,  2,  3,  4, -1},
      {0,  1,  2,  3, -1, -1},
      {0,  1,  2,  3, -1, -1},
      {0,  1,  2,  3,  4, -1},
      {0,  1,  2,  3, -1, -1}
    };
    // clang-format on

    struct FusedUpdateData {
        Bitboard dp2removedOriginBoard = 0;
        Bitboard dp2removedTargetBoard = 0;

        Square dp2removed;
    };

    // Maximum number of simultaneously active features.
    static constexpr IndexType MaxActiveDimensions = 128;
    using IndexList                                = ValueList<IndexType, MaxActiveDimensions>;
    using DiffType                                 = DirtyThreats;

    static IndexType
    make_index(Color perspective, Piece attkr, Square from, Square to, Piece attkd, Square ksq);

    // Get a list of indices for active features
    static void append_active_indices(Color perspective, const Position& pos, IndexList& active);

    // Get a list of indices for recently changed features
    static void append_changed_indices(Color            perspective,
                                       Square           ksq,
                                       const DiffType&  diff,
                                       IndexList&       removed,
                                       IndexList&       added,
                                       FusedUpdateData* fd    = nullptr,
                                       bool             first = false);

    // Returns whether the change stored in this DirtyPiece means
    // that a full accumulator refresh is required.
    static bool requires_refresh(const DiffType& diff, Color perspective);
};

}  // namespace Stockfish::Eval::NNUE::Features

#endif  // #ifndef NNUE_FEATURES_FULL_THREATS_INCLUDED
