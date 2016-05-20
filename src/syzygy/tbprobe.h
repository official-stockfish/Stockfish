/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (c) 2013 Ronald de Man
  Copyright (C) 2016 Marco Costalba, Lucas Braesch

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

#include "../search.h"

namespace Tablebases {

enum WDLScore {
    WDLLoss       = -2, // Loss
    WDLCursedLoss = -1, // Loss, but draw under 50-move rule
    WDLDraw       =  0, // Draw
    WDLCursedWin  =  1, // Win, but draw under 50-move rule
    WDLWin        =  2, // Win

    WDLScoreNone  = -1000
};

extern size_t MaxCardinality;

void init(const std::string& paths);
WDLScore probe_wdl(Position& pos, int* success);
bool root_probe(Position& pos, Search::RootMoves& rootMoves, Value& score);
bool root_probe_wdl(Position& pos, Search::RootMoves& rootMoves, Value& score);

}

#endif
