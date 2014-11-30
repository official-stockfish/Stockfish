#ifndef TBPROBE_H
#define TBPROBE_H

#include "../search.h"

namespace Tablebases {

extern int MaxCardinality;

void init(const std::string& path);
int probe_wdl(Position& pos, int *success);
int probe_dtz(Position& pos, int *success);
bool root_probe(Position& pos, Search::RootMoveVector& rootMoves, Value& score);
bool root_probe_wdl(Position& pos, Search::RootMoveVector& rootMoves, Value& score);

}

#endif
