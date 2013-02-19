/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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
#include <memory>
#include <stack>
#include <vector>

#include "misc.h"
#include "position.h"
#include "types.h"

struct SplitPoint;

namespace Search {

/// The Stack struct keeps track of the information we need to remember from
/// nodes shallower and deeper in the tree during the search. Each search thread
/// has its own array of Stack objects, indexed by the current ply.

struct Stack {
  SplitPoint* sp;
  int ply;
  Move currentMove;
  Move excludedMove;
  Move killers[2];
  Depth reduction;
  Value staticEval;
  Value evalMargin;
  int skipNullMove;
};


/// RootMove struct is used for moves at the root of the tree. For each root
/// move we store a score, a node count, and a PV (really a refutation in the
/// case of moves which fail low). Score is normally set at -VALUE_INFINITE for
/// all non-pv moves.
struct RootMove {

  RootMove(Move m) : score(-VALUE_INFINITE), prevScore(-VALUE_INFINITE) {
    pv.push_back(m); pv.push_back(MOVE_NONE);
  }

  bool operator<(const RootMove& m) const { return score > m.score; } // Ascending sort
  bool operator==(const Move& m) const { return pv[0] == m; }

  void extract_pv_from_tt(Position& pos);
  void insert_pv_in_tt(Position& pos);

  Value score;
  Value prevScore;
  std::vector<Move> pv;
};


/// The LimitsType struct stores information sent by GUI about available time
/// to search the current move, maximum depth/time, if we are in analysis mode
/// or if we have to ponder while is our opponent's side to move.

struct LimitsType {

  LimitsType() { memset(this, 0, sizeof(LimitsType)); }
  bool use_time_management() const { return !(mate | movetime | depth | nodes | infinite); }

  int time[COLOR_NB], inc[COLOR_NB], movestogo, depth, nodes, movetime, mate, infinite, ponder;
};


/// The SignalsType struct stores volatile flags updated during the search
/// typically in an async fashion, for instance to stop the search by the GUI.

struct SignalsType {
  bool stopOnPonderhit, firstRootMove, stop, failedLowAtRoot;
};

typedef std::auto_ptr<std::stack<StateInfo> > StateStackPtr;

extern volatile SignalsType Signals;
extern LimitsType Limits;
extern std::vector<RootMove> RootMoves;
extern Position RootPos;
extern Color RootColor;
extern Time::point SearchTime;
extern StateStackPtr SetupStates;

extern void init();
extern size_t perft(Position& pos, Depth depth);
extern void think();

} // namespace Search

#endif // !defined(SEARCH_H_INCLUDED)
