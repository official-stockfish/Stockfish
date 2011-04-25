/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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

#if !defined(SEARCH_H_INCLUDED)
#define SEARCH_H_INCLUDED

#include <cstring>

#include "move.h"
#include "types.h"

class Position;
struct SplitPoint;

/// The SearchStack struct keeps track of the information we need to remember
/// from nodes shallower and deeper in the tree during the search.  Each
/// search thread has its own array of SearchStack objects, indexed by the
/// current ply.

struct SearchStack {
  int ply;
  Move currentMove;
  Move mateKiller;
  Move excludedMove;
  Move bestMove;
  Move killers[2];
  Depth reduction;
  Value eval;
  Value evalMargin;
  bool skipNullMove;
  SplitPoint* sp;
};


/// The SearchLimits struct stores information sent by GUI about available time
/// to search the current move, maximum depth/time, if we are in analysis mode
/// or if we have to ponder while is our opponent's side to move.

struct SearchLimits {

  SearchLimits() { memset(this, 0, sizeof(SearchLimits)); }

  SearchLimits(int t, int i, int mtg, int mt, int md, int mn, bool inf, bool pon)
              : time(t), increment(i), movesToGo(mtg), maxTime(mt), maxDepth(md),
                maxNodes(mn), infinite(inf), ponder(pon) {}

  bool useTimeManagement() const { return !(maxTime | maxDepth | maxNodes | int(infinite)); }

  int time, increment, movesToGo, maxTime, maxDepth, maxNodes;
  bool infinite, ponder;
};

extern void init_search();
extern int64_t perft(Position& pos, Depth depth);
extern bool think(Position& pos, const SearchLimits& limits, Move searchMoves[]);

#endif // !defined(SEARCH_H_INCLUDED)
