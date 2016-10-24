/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef SEARCH_H_INCLUDED
#define SEARCH_H_INCLUDED

#include <atomic>
#include <vector>

#include "misc.h"
#include "movepick.h"
#include "types.h"

class Position;

namespace Search {

/// Stack struct keeps track of the information we need to remember from nodes
/// shallower and deeper in the tree during the search. Each search thread has
/// its own array of Stack objects, indexed by the current ply.

struct Stack {
  Move* pv;
  int ply;
  Move currentMove;
  Move excludedMove;
  Move killers[2];
  Value staticEval;
  Value history;
  bool skipEarlyPruning;
  int moveCount;
  CounterMoveStats* counterMoves;
};


/// RootMove struct is used for moves at the root of the tree. For each root move
/// we store a score and a PV (really a refutation in the case of moves which
/// fail low). Score is normally set at -VALUE_INFINITE for all non-pv moves.

struct RootMove {

  explicit RootMove(Move m) : pv(1, m) {}

  bool operator<(const RootMove& m) const { return m.score < score; } // Descending sort
  bool operator==(const Move& m) const { return pv[0] == m; }
  bool extract_ponder_from_tt(Position& pos);

  Value score = -VALUE_INFINITE;
  Value previousScore = -VALUE_INFINITE;
  std::vector<Move> pv;
};

typedef std::vector<RootMove> RootMoves;


/// LimitsType struct stores information sent by GUI about available time to
/// search the current move, maximum depth/time, if we are in analysis mode or
/// if we have to ponder while it's our opponent's turn to move.

struct LimitsType {

  LimitsType() { // Init explicitly due to broken value-initialization of non POD in MSVC
    nodes = time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] =
    npmsec = movestogo = depth = movetime = mate = infinite = ponder = 0;
  }

  bool use_time_management() const {
    return !(mate | movetime | depth | nodes | infinite);
  }

  std::vector<Move> searchmoves;
  int time[COLOR_NB], inc[COLOR_NB], npmsec, movestogo, depth, movetime, mate, infinite, ponder;
  int64_t nodes;
  TimePoint startTime;
};


/// SignalsType struct stores atomic flags updated during the search, typically
/// in an async fashion e.g. to stop the search by the GUI.

struct SignalsType {
  std::atomic_bool stop, stopOnPonderhit;
};

extern SignalsType Signals;
extern LimitsType Limits;

void init();
void clear();
template<bool Root = true> uint64_t perft(Position& pos, Depth depth);

} // namespace Search

#endif // #ifndef SEARCH_H_INCLUDED
