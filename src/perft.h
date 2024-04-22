/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#ifndef PERFT_H_INCLUDED
#define PERFT_H_INCLUDED

#include <cstdint>

#include "movegen.h"
#include "position.h"
#include "types.h"
#include "uci.h"

namespace Stockfish::Benchmark {

// Utility to verify move generation. All the leaf nodes up
// to the given depth are generated and counted, and the sum is returned.
template<bool Root>
uint64_t perft(Position& pos, Depth depth) {

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    uint64_t   cnt, nodes = 0;
    const bool leaf = (depth == 2);

    for (const auto& m : MoveList<LEGAL>(pos))
    {
        if (Root && depth <= 1)
            cnt = 1, nodes++;
        else
        {
            pos.do_move(m, st);
            cnt = leaf ? MoveList<LEGAL>(pos).size() : perft<false>(pos, depth - 1);
            nodes += cnt;
            pos.undo_move(m);
        }
        if (Root)
            sync_cout << UCIEngine::move(m, pos.is_chess960()) << ": " << cnt << sync_endl;
    }
    return nodes;
}

inline uint64_t perft(const std::string& fen, Depth depth, bool isChess960) {
    StateListPtr states(new std::deque<StateInfo>(1));
    Position     p;
    p.set(fen, isChess960, &states->back());

    return perft<true>(p, depth);
}
}

#endif  // PERFT_H_INCLUDED
