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

#ifndef TBPROBE_H
#define TBPROBE_H

#include <functional>
#include <string>
#include <vector>


namespace Stockfish {
class Position;
class OptionsMap;

using Depth = int;

namespace Search {
struct RootMove;
using RootMoves = std::vector<RootMove>;
}
}

namespace Stockfish::Tablebases {

struct Config {
    int   cardinality = 0;
    bool  rootInTB    = false;
    bool  useRule50   = false;
    Depth probeDepth  = 0;
};

enum WDLScore {
    WDLLoss        = -2,  // Loss
    WDLBlessedLoss = -1,  // Loss, but draw under 50-move rule
    WDLDraw        = 0,   // Draw
    WDLCursedWin   = 1,   // Win, but draw under 50-move rule
    WDLWin         = 2,   // Win
};

// Possible states after a probing operation
enum ProbeState {
    FAIL = 0,  // Probe failed (missing file table)
    OK   = 1,  // Probe successful
#ifdef USE_SYZYGY
    CHANGE_STM        = -1,  // DTZ should check the other side
    ZEROING_BEST_MOVE = 2    // Best move zeroes DTZ (capture or pawn move)
#endif
};

#ifdef USE_SYZYGY
extern int MaxCardinality;


void     init(const std::string& paths);
WDLScore probe_wdl(Position& pos, ProbeState* result);
int      probe_dtz(Position& pos, ProbeState* result);
bool     root_probe(Position&                    pos,
                    Search::RootMoves&           rootMoves,
                    bool                         rule50,
                    bool                         rankDTZ,
                    const std::function<bool()>& time_abort);
bool     root_probe_wdl(Position& pos, Search::RootMoves& rootMoves, bool rule50);
Config   rank_root_moves(
    const OptionsMap&            options,
    Position&                    pos,
    Search::RootMoves&           rootMoves,
    bool                         rankDTZ    = false,
    const std::function<bool()>& time_abort = []() { return false; });

#else
inline int MaxCardinality = 0;

inline void init(const std::string&) {}

inline WDLScore probe_wdl(Position&, ProbeState* result) {
    *result = FAIL;
    return WDLDraw;
}

inline int probe_dtz(Position&, ProbeState* result) {
    *result = FAIL;
    return 0;
}

inline bool root_probe(Position&, Search::RootMoves&, bool, bool, const std::function<bool()>&) {
    return false;
}

inline bool root_probe_wdl(Position&, Search::RootMoves&, bool) { return false; }

inline Config rank_root_moves(
  const OptionsMap&,
  Position&,
  Search::RootMoves&,
  bool                         = false,
  const std::function<bool()>& = []() { return false; }) {
    return Config{};
}
#endif

}  // namespace Stockfish::Tablebases

#endif
