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


////
//// Includes
////

#include <algorithm>
#include <cassert>

#include "history.h"
#include "movegen.h"
#include "movepick.h"
#include "search.h"
#include "value.h"


////
//// Local definitions
////

namespace {

  CACHE_LINE_ALIGNMENT
  const MovegenPhaseT MainSearchPhaseTable[] = { PH_NULL_MOVE, PH_TT_MOVES, PH_GOOD_CAPTURES, PH_KILLERS, PH_NONCAPTURES, PH_BAD_CAPTURES, PH_STOP};
  const MovegenPhaseT MainSearchNoNullPhaseTable[] = { PH_TT_MOVES, PH_GOOD_CAPTURES, PH_KILLERS, PH_NONCAPTURES, PH_BAD_CAPTURES, PH_STOP};
  const MovegenPhaseT LowSearchPhaseTable[]  = { PH_TT_MOVES, PH_NULL_MOVE, PH_GOOD_CAPTURES, PH_KILLERS, PH_NONCAPTURES, PH_BAD_CAPTURES, PH_STOP};
  const MovegenPhaseT EvasionsPhaseTable[] = { PH_EVASIONS, PH_STOP};
  const MovegenPhaseT QsearchWithChecksPhaseTable[] = { PH_TT_MOVES, PH_QCAPTURES, PH_QCHECKS, PH_STOP};
  const MovegenPhaseT QsearchWithoutChecksPhaseTable[] = { PH_TT_MOVES, PH_QCAPTURES, PH_STOP};
}


////
//// Functions
////


/// Constructor for the MovePicker class. Apart from the position for which
/// it is asked to pick legal moves, MovePicker also wants some information
/// to help it to return the presumably good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions and some checks) and about how important good
/// move ordering is at the current node.

MovePicker::MovePicker(const Position& p, Move ttm, Depth d,
                       const History& h, SearchStack* ss, bool useNullMove) : pos(p), H(h) {
  ttMoves[0].move = ttm;
  if (ss)
  {
      ttMoves[1].move = (ss->mateKiller == ttm)? MOVE_NONE : ss->mateKiller;
      killers[0].move = ss->killers[0];
      killers[1].move = ss->killers[1];
  } else
      ttMoves[1].move = killers[0].move = killers[1].move = MOVE_NONE;

  finished = false;
  numOfBadCaptures = 0;

  Color us = pos.side_to_move();

  dc = p.discovered_check_candidates(us);
  pinned = p.pinned_pieces(us);

  if (p.is_check())
      phasePtr = EvasionsPhaseTable;
  else if (d >= Depth(3 * OnePly))
      phasePtr = useNullMove ? MainSearchPhaseTable : MainSearchNoNullPhaseTable;
  else if (d > Depth(0))
      phasePtr = useNullMove ? LowSearchPhaseTable : MainSearchNoNullPhaseTable;
  else if (d == Depth(0))
      phasePtr = QsearchWithChecksPhaseTable;
  else
      phasePtr = QsearchWithoutChecksPhaseTable;

  phasePtr--;
  go_next_phase();
}


/// MovePicker::go_next_phase() generates, scores and sorts the next bunch
/// of moves when there are no more moves to try for the current phase.

void MovePicker::go_next_phase() {

  curMove = moves;
  phase = *(++phasePtr);
  switch (phase) {

  case PH_NULL_MOVE:
      return;

  case PH_TT_MOVES:
      curMove = ttMoves;
      lastMove = curMove + 2;
      return;

  case PH_GOOD_CAPTURES:
      lastMove = generate_captures(pos, moves);
      score_captures();
      std::sort(moves, lastMove);
      return;

  case PH_KILLERS:
      curMove = killers;
      lastMove = curMove + 2;
      return;

  case PH_NONCAPTURES:
      lastMove = generate_noncaptures(pos, moves);
      score_noncaptures();
      std::sort(moves, lastMove);
      return;

  case PH_BAD_CAPTURES:
      // Bad captures SEE value is already calculated so just sort them
      // to get SEE move ordering.
      curMove = badCaptures;
      lastMove = badCaptures + numOfBadCaptures;
      std::sort(badCaptures, lastMove);
      return;

  case PH_EVASIONS:
      assert(pos.is_check());
      lastMove = generate_evasions(pos, moves, pinned);
      score_evasions();
      std::sort(moves, lastMove);
      return;

  case PH_QCAPTURES:
      lastMove = generate_captures(pos, moves);
      score_captures();
      std::sort(moves, lastMove);
      return;

  case PH_QCHECKS:
      // Perhaps we should order moves move here?  FIXME
      lastMove = generate_non_capture_checks(pos, moves, dc);
      return;

  case PH_STOP:
      return;

  default:
      assert(false);
      return;
  }
}


/// MovePicker::score_captures(), MovePicker::score_noncaptures() and
/// MovePicker::score_evasions() assign a numerical move ordering score
/// to each move in a move list.  The moves with highest scores will be
/// picked first by get_next_move().

void MovePicker::score_captures() {
  // Winning and equal captures in the main search are ordered by MVV/LVA.
  // Suprisingly, this appears to perform slightly better than SEE based
  // move ordering. The reason is probably that in a position with a winning
  // capture, capturing a more valuable (but sufficiently defended) piece
  // first usually doesn't hurt. The opponent will have to recapture, and
  // the hanging piece will still be hanging (except in the unusual cases
  // where it is possible to recapture with the hanging piece). Exchanging
  // big pieces before capturing a hanging piece probably helps to reduce
  // the subtree size.
  // In main search we want to push captures with negative SEE values to
  // badCaptures[] array, but instead of doing it now we delay till when
  // the move has been picked up in pick_move_from_list(), this way we save
  // some SEE calls in case we get a cutoff (idea from Pablo Vazquez).
  Move m;

  // Use MVV/LVA ordering
  for (MoveStack* cur = moves; cur != lastMove; cur++)
  {
      m = cur->move;
      if (move_is_promotion(m))
          cur->score = QueenValueMidgame;
      else
          cur->score = int(pos.midgame_value_of_piece_on(move_to(m)))
                      -int(pos.type_of_piece_on(move_from(m)));
  }
}

void MovePicker::score_noncaptures() {
  // First score by history, when no history is available then use
  // piece/square tables values. This seems to be better then a
  // random choice when we don't have an history for any move.
  Move m;
  Piece piece;
  Square from, to;
  int hs;

  for (MoveStack* cur = moves; cur != lastMove; cur++)
  {
      m = cur->move;
      from = move_from(m);
      to = move_to(m);
      piece = pos.piece_on(from);
      hs = H.move_ordering_score(piece, to);

      // Ensure history is always preferred to pst
      if (hs > 0)
          hs += 1000;

      // pst based scoring
      cur->score = hs + pos.pst_delta<Position::MidGame>(piece, from, to);
  }
}

void MovePicker::score_evasions() {

  Move m;

  for (MoveStack* cur = moves; cur != lastMove; cur++)
  {
      m = cur->move;
      if (m == ttMoves[0].move)
          cur->score = 2 * HistoryMax;
      else if (!pos.square_is_empty(move_to(m)))
      {
          int seeScore = pos.see(m);
          cur->score = seeScore + (seeScore >= 0 ? HistoryMax : 0);
      } else
          cur->score = H.move_ordering_score(pos.piece_on(move_from(m)), move_to(m));
  }
}

/// MovePicker::get_next_move() is the most important method of the MovePicker
/// class. It returns a new legal move every time it is called, until there
/// are no more moves left.
/// It picks the move with the biggest score from a list of generated moves taking
/// care not to return the tt move if that has already been serched previously.

Move MovePicker::get_next_move() {

  assert(!pos.is_check() || *phasePtr == PH_EVASIONS || *phasePtr == PH_STOP);
  assert( pos.is_check() || *phasePtr != PH_EVASIONS);

  while (true)
  {
      switch (phase) {

      case PH_NULL_MOVE:
          go_next_phase();
          return MOVE_NULL;

      case PH_TT_MOVES:
          while (curMove != lastMove)
          {
              Move move = (curMove++)->move;
              if (   move != MOVE_NONE
                  && move_is_legal(pos, move, pinned))
                  return move;
          }
          break;

      case PH_GOOD_CAPTURES:
          while (curMove != lastMove)
          {
              Move move = (curMove++)->move;
              if (   move != ttMoves[0].move
                  && move != ttMoves[1].move
                  && pos.pl_move_is_legal(move, pinned))
              {
                  // Check for a non negative SEE now
                  int seeValue = pos.see_sign(move);
                  if (seeValue >= 0)
                      return move;

                  // Losing capture, move it to the badCaptures[] array, note
                  // that move has now been already checked for legality.
                  assert(numOfBadCaptures < 63);
                  badCaptures[numOfBadCaptures].move = move;
                  badCaptures[numOfBadCaptures++].score = seeValue;
              }
          }
          break;

      case PH_KILLERS:
          while (curMove != lastMove)
          {
              Move move = (curMove++)->move;
              if (   move != MOVE_NONE
                  && move != ttMoves[0].move
                  && move != ttMoves[1].move
                  && move_is_legal(pos, move, pinned)
                  && !pos.move_is_capture(move))
                  return move;
          }
          break;

      case PH_NONCAPTURES:
          while (curMove != lastMove)
          {
              Move move = (curMove++)->move;
              if (   move != ttMoves[0].move
                  && move != ttMoves[1].move
                  && move != killers[0].move
                  && move != killers[1].move
                  && pos.pl_move_is_legal(move, pinned))
                  return move;
          }
          break;

      case PH_EVASIONS:
      case PH_BAD_CAPTURES:
          if (curMove != lastMove)
              return (curMove++)->move;
          break;

      case PH_QCAPTURES:
      case PH_QCHECKS:
          while (curMove != lastMove)
          {
              Move move = (curMove++)->move;
              // Maybe postpone the legality check until after futility pruning?
              if (   move != ttMoves[0].move
                  && pos.pl_move_is_legal(move, pinned))
                  return move;
          }
          break;

      case PH_STOP:
          return MOVE_NONE;

      default:
          assert(false);
          break;
      }
      go_next_phase();
  }
}

/// A variant of get_next_move() which takes a lock as a parameter, used to
/// prevent multiple threads from picking the same move at a split point.

Move MovePicker::get_next_move(Lock &lock) {

   lock_grab(&lock);
   if (finished)
   {
       lock_release(&lock);
       return MOVE_NONE;
   }
   Move m = get_next_move();
   if (m == MOVE_NONE)
       finished = true;

   lock_release(&lock);
   return m;
}
