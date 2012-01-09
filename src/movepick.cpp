/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <algorithm>
#include <cassert>

#include "movegen.h"
#include "movepick.h"
#include "search.h"
#include "types.h"

namespace {

  enum MovegenPhase {
    PH_TT_MOVE,       // Transposition table move
    PH_GOOD_CAPTURES, // Queen promotions and captures with SEE values >= captureThreshold (captureThreshold <= 0)
    PH_GOOD_PROBCUT,  // Queen promotions and captures with SEE values > captureThreshold (captureThreshold >= 0)
    PH_KILLERS,       // Killer moves from the current ply
    PH_NONCAPTURES_1, // Non-captures and underpromotions with positive score
    PH_NONCAPTURES_2, // Non-captures and underpromotions with non-positive score
    PH_BAD_CAPTURES,  // Queen promotions and captures with SEE values < captureThreshold (captureThreshold <= 0)
    PH_EVASIONS,      // Check evasions
    PH_QCAPTURES,     // Captures in quiescence search
    PH_QRECAPTURES,   // Recaptures in quiescence search
    PH_QCHECKS,       // Non-capture checks in quiescence search
    PH_STOP
  };

  CACHE_LINE_ALIGNMENT
  const uint8_t MainSearchTable[] = { PH_TT_MOVE, PH_GOOD_CAPTURES, PH_KILLERS, PH_NONCAPTURES_1, PH_NONCAPTURES_2, PH_BAD_CAPTURES, PH_STOP };
  const uint8_t EvasionTable[] = { PH_TT_MOVE, PH_EVASIONS, PH_STOP };
  const uint8_t QsearchWithChecksTable[] = { PH_TT_MOVE, PH_QCAPTURES, PH_QCHECKS, PH_STOP };
  const uint8_t QsearchWithoutChecksTable[] = { PH_TT_MOVE, PH_QCAPTURES, PH_STOP };
  const uint8_t QsearchRecapturesTable[] = { PH_TT_MOVE, PH_QRECAPTURES, PH_STOP };
  const uint8_t ProbCutTable[] = { PH_TT_MOVE, PH_GOOD_PROBCUT, PH_STOP };

  // Unary predicate used by std::partition to split positive scores from remaining
  // ones so to sort separately the two sets, and with the second sort delayed.
  inline bool has_positive_score(const MoveStack& move) { return move.score > 0; }

  // Picks and pushes to the front the best move in range [firstMove, lastMove),
  // it is faster than sorting all the moves in advance when moves are few, as
  // normally are the possible captures.
  inline MoveStack* pick_best(MoveStack* firstMove, MoveStack* lastMove)
  {
      std::swap(*firstMove, *std::max_element(firstMove, lastMove));
      return firstMove;
  }
}

/// Constructors for the MovePicker class. As arguments we pass information
/// to help it to return the presumably good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions and some checks) and about how important good
/// move ordering is at the current node.

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const History& h,
                       Search::Stack* ss, Value beta) : pos(p), H(h), depth(d) {
  captureThreshold = 0;
  badCaptures = moves + MAX_MOVES;

  assert(d > DEPTH_ZERO);

  if (p.in_check())
  {
      killers[0].move = killers[1].move = MOVE_NONE;
      phasePtr = EvasionTable;
  }
  else
  {
      killers[0].move = ss->killers[0];
      killers[1].move = ss->killers[1];

      // Consider sligtly negative captures as good if at low depth and far from beta
      if (ss && ss->eval < beta - PawnValueMidgame && d < 3 * ONE_PLY)
          captureThreshold = -PawnValueMidgame;

      // Consider negative captures as good if still enough to reach beta
      else if (ss && ss->eval > beta)
          captureThreshold = beta - ss->eval;

      phasePtr = MainSearchTable;
  }

  ttMove = (ttm && pos.is_pseudo_legal(ttm) ? ttm : MOVE_NONE);
  phasePtr += int(ttMove == MOVE_NONE) - 1;
  go_next_phase();
}

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const History& h, Square recaptureSq)
                      : pos(p), H(h) {

  assert(d <= DEPTH_ZERO);

  if (p.in_check())
      phasePtr = EvasionTable;
  else if (d >= DEPTH_QS_CHECKS)
      phasePtr = QsearchWithChecksTable;
  else if (d >= DEPTH_QS_RECAPTURES)
  {
      phasePtr = QsearchWithoutChecksTable;

      // Skip TT move if is not a capture or a promotion, this avoids
      // qsearch tree explosion due to a possible perpetual check or
      // similar rare cases when TT table is full.
      if (ttm != MOVE_NONE && !pos.is_capture_or_promotion(ttm))
          ttm = MOVE_NONE;
  }
  else
  {
      phasePtr = QsearchRecapturesTable;
      recaptureSquare = recaptureSq;
      ttm = MOVE_NONE;
  }

  ttMove = (ttm && pos.is_pseudo_legal(ttm) ? ttm : MOVE_NONE);
  phasePtr += int(ttMove == MOVE_NONE) - 1;
  go_next_phase();
}

MovePicker::MovePicker(const Position& p, Move ttm, const History& h, PieceType parentCapture)
                       : pos(p), H(h) {

  assert (!pos.in_check());

  // In ProbCut we consider only captures better than parent's move
  captureThreshold = PieceValueMidgame[Piece(parentCapture)];
  phasePtr = ProbCutTable;

  if (   ttm != MOVE_NONE
      && (!pos.is_capture(ttm) ||  pos.see(ttm) <= captureThreshold))
      ttm = MOVE_NONE;

  ttMove = (ttm && pos.is_pseudo_legal(ttm) ? ttm : MOVE_NONE);
  phasePtr += int(ttMove == MOVE_NONE) - 1;
  go_next_phase();
}


/// MovePicker::go_next_phase() generates, scores and sorts the next bunch
/// of moves when there are no more moves to try for the current phase.

void MovePicker::go_next_phase() {

  curMove = moves;
  phase = *(++phasePtr);
  switch (phase) {

  case PH_TT_MOVE:
      lastMove = curMove + 1;
      return;

  case PH_GOOD_CAPTURES:
  case PH_GOOD_PROBCUT:
      lastMove = generate<MV_CAPTURE>(pos, moves);
      score_captures();
      return;

  case PH_KILLERS:
      curMove = killers;
      lastMove = curMove + 2;
      return;

  case PH_NONCAPTURES_1:
      lastNonCapture = lastMove = generate<MV_NON_CAPTURE>(pos, moves);
      score_noncaptures();
      lastMove = std::partition(curMove, lastMove, has_positive_score);
      sort<MoveStack>(curMove, lastMove);
      return;

  case PH_NONCAPTURES_2:
      curMove = lastMove;
      lastMove = lastNonCapture;
      if (depth >= 3 * ONE_PLY)
          sort<MoveStack>(curMove, lastMove);
      return;

  case PH_BAD_CAPTURES:
      // Bad captures SEE value is already calculated so just pick
      // them in order to get SEE move ordering.
      curMove = badCaptures;
      lastMove = moves + MAX_MOVES;
      return;

  case PH_EVASIONS:
      assert(pos.in_check());
      lastMove = generate<MV_EVASION>(pos, moves);
      score_evasions();
      return;

  case PH_QCAPTURES:
      lastMove = generate<MV_CAPTURE>(pos, moves);
      score_captures();
      return;

  case PH_QRECAPTURES:
      lastMove = generate<MV_CAPTURE>(pos, moves);
      return;

  case PH_QCHECKS:
      lastMove = generate<MV_NON_CAPTURE_CHECK>(pos, moves);
      return;

  case PH_STOP:
      lastMove = curMove + 1; // Avoid another go_next_phase() call
      return;

  default:
      assert(false);
      return;
  }
}


/// MovePicker::score_captures(), MovePicker::score_noncaptures() and
/// MovePicker::score_evasions() assign a numerical move ordering score
/// to each move in a move list.  The moves with highest scores will be
/// picked first by next_move().

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
      cur->score =  PieceValueMidgame[pos.piece_on(to_sq(m))]
                  - type_of(pos.piece_moved(m));

      if (is_promotion(m))
          cur->score += PieceValueMidgame[Piece(promotion_piece_type(m))];
  }
}

void MovePicker::score_noncaptures() {

  Move m;
  Square from;

  for (MoveStack* cur = moves; cur != lastMove; cur++)
  {
      m = cur->move;
      from = from_sq(m);
      cur->score = H.value(pos.piece_on(from), to_sq(m));
  }
}

void MovePicker::score_evasions() {
  // Try good captures ordered by MVV/LVA, then non-captures if
  // destination square is not under attack, ordered by history
  // value, and at the end bad-captures and non-captures with a
  // negative SEE. This last group is ordered by the SEE score.
  Move m;
  int seeScore;

  // Skip if we don't have at least two moves to order
  if (lastMove < moves + 2)
      return;

  for (MoveStack* cur = moves; cur != lastMove; cur++)
  {
      m = cur->move;
      if ((seeScore = pos.see_sign(m)) < 0)
          cur->score = seeScore - History::MaxValue; // Be sure we are at the bottom
      else if (pos.is_capture(m))
          cur->score =  PieceValueMidgame[pos.piece_on(to_sq(m))]
                      - type_of(pos.piece_moved(m)) + History::MaxValue;
      else
          cur->score = H.value(pos.piece_moved(m), to_sq(m));
  }
}

/// MovePicker::next_move() is the most important method of the MovePicker class.
/// It returns a new pseudo legal move every time it is called, until there
/// are no more moves left. It picks the move with the biggest score from a list
/// of generated moves taking care not to return the tt move if has already been
/// searched previously. Note that this function is not thread safe so should be
/// lock protected by caller when accessed through a shared MovePicker object.

Move MovePicker::next_move() {

  Move move;

  while (true)
  {
      while (curMove == lastMove)
          go_next_phase();

      switch (phase) {

      case PH_TT_MOVE:
          curMove++;
          return ttMove;
          break;

      case PH_GOOD_CAPTURES:
          move = pick_best(curMove++, lastMove)->move;
          if (move != ttMove)
          {
              assert(captureThreshold <= 0); // Otherwise we must use see instead of see_sign

              // Check for a non negative SEE now
              int seeValue = pos.see_sign(move);
              if (seeValue >= captureThreshold)
                  return move;

              // Losing capture, move it to the tail of the array
              (--badCaptures)->move = move;
              badCaptures->score = seeValue;
          }
          break;

     case PH_GOOD_PROBCUT:
          move = pick_best(curMove++, lastMove)->move;
          if (   move != ttMove
              && pos.see(move) > captureThreshold)
              return move;
          break;

      case PH_KILLERS:
          move = (curMove++)->move;
          if (   move != MOVE_NONE
              && pos.is_pseudo_legal(move)
              && move != ttMove
              && !pos.is_capture(move))
              return move;
          break;

      case PH_NONCAPTURES_1:
      case PH_NONCAPTURES_2:
          move = (curMove++)->move;
          if (   move != ttMove
              && move != killers[0].move
              && move != killers[1].move)
              return move;
          break;

      case PH_BAD_CAPTURES:
          move = pick_best(curMove++, lastMove)->move;
          return move;

      case PH_EVASIONS:
      case PH_QCAPTURES:
          move = pick_best(curMove++, lastMove)->move;
          if (move != ttMove)
              return move;
          break;

      case PH_QRECAPTURES:
          move = (curMove++)->move;
          if (to_sq(move) == recaptureSquare)
              return move;
          break;

      case PH_QCHECKS:
          move = (curMove++)->move;
          if (move != ttMove)
              return move;
          break;

      case PH_STOP:
          return MOVE_NONE;

      default:
          assert(false);
          break;
      }
  }
}
