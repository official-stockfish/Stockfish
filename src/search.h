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

////
//// Includes
////

#include "depth.h"
#include "move.h"
#include "value.h"


////
//// Constants
////

const int PLY_MAX = 100;
const int PLY_MAX_PLUS_2 = 102;
const int KILLER_MAX = 2;


////
//// Types
////

/// The SearchStack struct keeps track of the information we need to remember
/// from nodes shallower and deeper in the tree during the search.  Each
/// search thread has its own array of SearchStack objects, indexed by the
/// current ply.
struct EvalInfo;

struct SearchStack {
  Move pv[PLY_MAX_PLUS_2];
  Move currentMove;
  Move mateKiller;
  Move threatMove;
  Move killers[KILLER_MAX];
  Depth reduction;
  Value eval;

  void init(int ply);
  void initKillers();
};


////
//// Prototypes
////

extern void init_search();
extern void init_threads();
extern void exit_threads();
extern bool think(const Position &pos, bool infinite, bool ponder, int side_to_move,
                  int time[], int increment[], int movesToGo, int maxDepth,
                  int maxNodes, int maxTime, Move searchMoves[]);
extern int perft(Position &pos, Depth depth);
extern int64_t nodes_searched();


#endif // !defined(SEARCH_H_INCLUDED)
