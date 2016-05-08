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
};

extern int MaxCardinality;

void init(const std::string& paths);
WDLScore probe_wdl(Position& pos, int* success);
bool root_probe(Position& pos, Search::RootMoves& rootMoves, Value& score);
bool root_probe_wdl(Position& pos, Search::RootMoves& rootMoves, Value& score);

}

#endif
