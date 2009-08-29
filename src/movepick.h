/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2009 Marco Costalba

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


#if !defined MOVEPICK_H_INCLUDED
#define MOVEPICK_H_INCLUDED

////
//// Includes
////

#include "depth.h"
#include "history.h"
#include "lock.h"
#include "position.h"


////
//// Types
////

struct SearchStack;

enum MovegenPhase {
  PH_TT_MOVES,       // Transposition table move and mate killer
  PH_GOOD_CAPTURES,  // Queen promotions and captures with SEE values >= 0
  PH_KILLERS,        // Killer moves from the current ply
  PH_NONCAPTURES,    // Non-captures and underpromotions
  PH_BAD_CAPTURES,   // Queen promotions and captures with SEE values < 0
  PH_EVASIONS,       // Check evasions
  PH_QCAPTURES,      // Captures in quiescence search
  PH_QCHECKS,        // Non-capture checks in quiescence search
  PH_STOP
};

typedef uint8_t MovegenPhaseT;

/// MovePicker is a class which is used to pick one legal move at a time from
/// the current position. It is initialized with a Position object and a few
/// moves we have reason to believe are good. The most important method is
/// MovePicker::pick_next_move(), which returns a new legal move each time it
/// is called, until there are no legal moves left, when MOVE_NONE is returned.
/// In order to improve the efficiency of the alpha beta algorithm, MovePicker
/// attempts to return the moves which are most likely to be strongest first.

class MovePicker {

  MovePicker& operator=(const MovePicker&); // silence a warning under MSVC

public:
  MovePicker(const Position& p, Move ttm, Depth d, const History& h, SearchStack* ss = NULL);
  Move get_next_move();
  Move get_next_move(Lock& lock);
  int number_of_moves() const;
  Bitboard discovered_check_candidates() const;

private:
  void score_captures();
  void score_noncaptures();
  void score_evasions();
  void go_next_phase();

  const Position& pos;
  const History& H;
  MoveStack ttMoves[2], killers[2];
  bool finished;
  int numOfBadCaptures, phase;
  const MovegenPhaseT* phasePtr;
  MoveStack *curMove, *lastMove;
  Bitboard dc, pinned;
  MoveStack moves[256], badCaptures[64];
};


////
//// Inline functions
////

/// MovePicker::number_of_moves() simply returns the numOfMoves member
/// variable. It is intended to be used in positions where the side to move
/// is in check, for detecting checkmates or situations where there is only
/// a single reply to check.

inline int MovePicker::number_of_moves() const {
  return int(lastMove - moves);
}

/// MovePicker::discovered_check_candidates() returns a bitboard containing
/// all pieces which can possibly give discovered check. This bitboard is
/// computed by the constructor function.

inline Bitboard MovePicker::discovered_check_candidates() const {
  return dc;
}

#endif // !defined(MOVEPICK_H_INCLUDED)
