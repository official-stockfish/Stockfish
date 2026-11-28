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

#include "tablebases.h"

#include <iomanip>
#include <memory>
#include <ostream>

#include "bitboard.h"
#include "position.h"
#include "types.h"
#include "ucioption.h"

namespace Stockfish::Tablebases {

int MaxCardinality;

namespace {

#ifdef USE_SYZYGY
constexpr Backend Default = SYZYGY;
#else
constexpr Backend Default = CHESSTB;
#endif

Backend active = Default;

std::string name(Backend b) { return b == SYZYGY ? "Syzygy" : "ChessTB"; }

std::string path(const OptionsMap& options, Backend b) {
    const std::string option = name(b) + "Path";
    return options.count(option) ? std::string(options[option]) : "";
}

void init([[maybe_unused]] Backend b, [[maybe_unused]] const std::string& paths) {
#ifdef USE_SYZYGY
    if (b == SYZYGY)
        Syzygy::init(paths);
#endif
#ifdef USE_CHESSTB
    if (b == CHESSTB)
        ChessTB::init(paths);
#endif
}

}  // namespace

Backend backend() { return active; }

std::optional<std::string> set_path(const OptionsMap& options, Backend b) {
    const Backend other = b == SYZYGY ? CHESSTB : SYZYGY;
    if (active == other && !path(options, other).empty())
        return name(b) + "Path is not used while " + name(other) + "Path is set";

    init(b, path(options, b));
    if (!path(options, b).empty())
        active = b;
    else if (!path(options, other).empty())
    {
        init(other, path(options, other));
        active = other;
    }
    else
        active = Default;
    return std::nullopt;
}

WDLScore probe_wdl([[maybe_unused]] Position& pos, ProbeState* result) {
#ifdef USE_SYZYGY
    if (active == SYZYGY)
        return Syzygy::probe_wdl(pos, result);
#endif
#ifdef USE_CHESSTB
    if (active == CHESSTB)
        return ChessTB::probe_wdl(pos, result);
#endif
    *result = FAIL;
    return WDLDraw;
}

Config rank_root_moves([[maybe_unused]] const OptionsMap&            options,
                       [[maybe_unused]] Position&                    pos,
                       [[maybe_unused]] Search::RootMoves&           rootMoves,
                       [[maybe_unused]] bool                         rankDistance,
                       [[maybe_unused]] const std::function<bool()>& time_abort) {
#ifdef USE_SYZYGY
    if (active == SYZYGY)
        return Syzygy::rank_root_moves(options, pos, rootMoves, rankDistance, time_abort);
#endif
#ifdef USE_CHESSTB
    if (active == CHESSTB)
        return ChessTB::rank_root_moves(options, pos, rootMoves, rankDistance, time_abort);
#endif
    return Config{};
}

void extend_pv([[maybe_unused]] const OptionsMap&         options,
               [[maybe_unused]] const Search::LimitsType& limits,
               [[maybe_unused]] Position&                 pos,
               [[maybe_unused]] Search::RootMove&         rootMove,
               [[maybe_unused]] Value&                    v,
               [[maybe_unused]] std::size_t               multiPV) {
#ifdef USE_SYZYGY
    if (active == SYZYGY)
        Syzygy::extend_pv(options, limits, pos, rootMove, v, multiPV);
#endif
#ifdef USE_CHESSTB
    if (active == CHESSTB)
        ChessTB::extend_pv(options, limits, pos, rootMove, v, multiPV);
#endif
}

void print([[maybe_unused]] std::ostream& os, const Position& pos) {
    if (MaxCardinality < popcount(pos.pieces())
        || (active == SYZYGY && pos.can_castle(ANY_CASTLING)))
        return;

    // A copy to probe, as probes make moves
    std::unique_ptr<Position> p = std::make_unique<Position>();
    p->set(pos.fen(), pos.is_chess960());
#ifdef USE_SYZYGY
    if (active == SYZYGY)
    {
        ProbeState s1, s2;
        WDLScore   wdl = Syzygy::probe_wdl(*p, &s1);
        int        dtz = Syzygy::probe_dtz(*p, &s2);
        os << "\nTablebases WDL: " << std::setw(4) << wdl << " (" << s1 << ")"
           << "\nTablebases DTZ: " << std::setw(4) << dtz << " (" << s2 << ")";
    }
#endif
#ifdef USE_CHESSTB
    if (active == CHESSTB)
    {
        ProbeState s;
        WDLScore   wdl = ChessTB::probe_wdl(*p, &s);
        int        dtz, dtc, order, dtm, dtm50;
        ChessTB::probe_dtc(*p, dtz, dtc, order);
        ChessTB::probe_dtm(*p, true, dtm, dtm50);
        os << "\nTablebases WDL: " << std::setw(4) << wdl << " (" << s << ")"
           << "\nTablebases DTZ: " << std::setw(4) << dtz << "\nTablebases DTC: " << std::setw(4)
           << dtc << ", order " << order << "\nTablebases DTM: " << std::setw(4) << dtm
           << "\nTablebases DTM50: " << std::setw(4) << dtm50;
    }
#endif
}

}  // namespace Stockfish::Tablebases
