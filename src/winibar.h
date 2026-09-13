/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Winibar Extension
  Copyright (C) 2025-2026 Heriniaina Andry Raboanary

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#ifndef WINIBAR_H_INCLUDED
#define WINIBAR_H_INCLUDED

#include <string>
#include <vector>

#include "misc.h"
#include "types.h"

namespace Stockfish {

class Position;

namespace Search {
class Worker;
}

namespace Winibar {

struct Metrics {
    double hdi     = 0;
    double psave   = 0;
    double s       = 0;  // safe-move count
    double n       = 0;  // moves considered
    double t       = 0;  // trap / blunder density
    double v       = 0;  // volatility (normalized)
    double c       = 0;  // comeback potential
    double d       = 0;  // distance-to-collapse (normalized)
    double e       = 0;  // completeness stub in v0
    bool   partial = false;
};

struct ScoredMove {
    Move  move  = Move::none();
    Value score = -VALUE_INFINITE;
};

std::string format_info(const Metrics& m);

// Friend of Search::Worker; owns post-search analysis helpers.
struct Access {
    static Metrics compute(Search::Worker& worker);

    static Value shallow_probe(Search::Worker& worker,
                               Position&       pos,
                               Depth           depth,
                               Value           alpha,
                               Value           beta,
                               int             ply,
                               int             maxPly,
                               TimePoint       deadline,
                               bool&           timedOut);

    static std::vector<ScoredMove> order_replies(Search::Worker& worker,
                                                 Position&       pos,
                                                 TimePoint       deadline,
                                                 bool&           timedOut);
};

}  // namespace Winibar
}  // namespace Stockfish

#endif  // #ifndef WINIBAR_H_INCLUDED
