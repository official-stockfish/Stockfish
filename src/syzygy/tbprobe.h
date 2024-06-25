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

#ifndef TBPROBE_H
#define TBPROBE_H

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
    FAIL              = 0,   // Probe failed (missing file table)
    OK                = 1,   // Probe successful
    CHANGE_STM        = -1,  // DTZ should check the other side
    ZEROING_BEST_MOVE = 2    // Best move zeroes DTZ (capture or pawn move)
};

extern int MaxCardinality;


void     init(const std::string& paths);
WDLScore probe_wdl(Position& pos, ProbeState* result);
int      probe_dtz(Position& pos, ProbeState* result);
bool     root_probe(Position& pos, Search::RootMoves& rootMoves, bool rule50, bool rankDTZ);
bool     root_probe_wdl(Position& pos, Search::RootMoves& rootMoves, bool rule50);
Config   rank_root_moves(const OptionsMap&  options,
                         Position&          pos,
                         Search::RootMoves& rootMoves,
                         bool               rankDTZ = false);

}  // namespace Stockfish::Tablebases

#endif
