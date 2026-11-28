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

#ifndef TABLEBASES_H_INCLUDED
#define TABLEBASES_H_INCLUDED

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace Stockfish {
class Position;
class OptionsMap;

using Depth = int;
using Value = int;

namespace Search {
struct RootMove;
struct LimitsType;
using RootMoves = std::vector<RootMove>;
}
}

namespace Stockfish::Tablebases {

struct Config {
    int   cardinality = 0;
    bool  rootInTB    = false;
    bool  useRule50   = false;
    Depth probeDepth  = 0;
    // Whether the tables cover castling rights, and read the distance to mate
    // in search from mateProbeDepth on.
    bool  castling       = false;
    bool  mates          = false;
    Depth mateProbeDepth = 0;
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

// One backend answers: the first given a path, until that path is cleared.
enum Backend {
    SYZYGY,
    CHESSTB
};

extern int MaxCardinality;  // of the backend in use

Backend backend();
// Applies a change to backend b's path option; a message when b is not used.
std::optional<std::string> set_path(const OptionsMap& options, Backend b);

WDLScore probe_wdl(Position& pos, ProbeState* result);
Config   rank_root_moves(
    const OptionsMap&            options,
    Position&                    pos,
    Search::RootMoves&           rootMoves,
    bool                         rankDistance = false,
    const std::function<bool()>& time_abort   = []() { return false; });

// The tables' answers for pos, as the lines of the 'd' command.
void print(std::ostream& os, const Position& pos);

// Extends rootMove's PV with the tables' line, within Move Overhead; may update v.
void extend_pv(const OptionsMap&         options,
               const Search::LimitsType& limits,
               Position&                 pos,
               Search::RootMove&         rootMove,
               Value&                    v,
               std::size_t               multiPV);

}  // namespace Stockfish::Tablebases

#ifdef USE_SYZYGY
namespace Stockfish::Syzygy {

void                 init(const std::string& paths);
Tablebases::WDLScore probe_wdl(Position& pos, Tablebases::ProbeState* result);
int                  probe_dtz(Position& pos, Tablebases::ProbeState* result);
Tablebases::Config   rank_root_moves(const OptionsMap&            options,
                                     Position&                    pos,
                                     Search::RootMoves&           rootMoves,
                                     bool                         rankDistance,
                                     const std::function<bool()>& time_abort);
void                 extend_pv(const OptionsMap&         options,
                               const Search::LimitsType& limits,
                               Position&                 pos,
                               Search::RootMove&         rootMove,
                               Value&                    v,
                               std::size_t               multiPV);

}  // namespace Stockfish::Syzygy
#endif

#ifdef USE_CHESSTB
namespace Stockfish::ChessTB {

void                 init(const std::string& paths);
void                 set_cache_size(std::size_t mb);
Tablebases::WDLScore probe_wdl(Position& pos, Tablebases::ProbeState* result);
void                 probe_dtm(Position& pos, bool rule50, int& dtm, int& dtm50);
void                 probe_dtc(Position& pos, int& dtz, int& dtc, int& order);
Tablebases::Config   rank_root_moves(const OptionsMap&            options,
                                     Position&                    pos,
                                     Search::RootMoves&           rootMoves,
                                     bool                         rankDistance,
                                     const std::function<bool()>& time_abort);
void                 extend_pv(const OptionsMap&         options,
                               const Search::LimitsType& limits,
                               Position&                 pos,
                               Search::RootMove&         rootMove,
                               Value&                    v,
                               std::size_t               multiPV);

}  // namespace Stockfish::ChessTB
#endif

#endif  // TABLEBASES_H_INCLUDED
