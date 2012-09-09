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
#include "thread.h"

namespace {

  enum Sequencer {
    MAIN_SEARCH, CAPTURES_S1, KILLERS_S1, QUIETS_1_S1, QUIETS_2_S1, BAD_CAPTURES_S1,
    EVASION,     EVASIONS_S2,
    QSEARCH_0,   CAPTURES_S3, QUIET_CHECKS_S3,
    QSEARCH_1,   CAPTURES_S4,
    PROBCUT,     CAPTURES_S5,
    RECAPTURE,   CAPTURES_S6,
    STOP
  };

  // Unary predicate used by std::partition to split positive scores from remaining
  // ones so to sort separately the two sets, and with the second sort delayed.
  inline bool has_positive_score(const MoveStack& ms) { return ms.score > 0; }

  // Picks and moves to the front the best move in the range [begin, end),
  // it is faster than sorting all the moves in advance when moves are few, as
  // normally are the possible captures.
  inline MoveStack* pick_best(MoveStack* begin, MoveStack* end)
  {
      std::swap(*begin, *std::max_element(begin, end));
      return begin;
  }
}


/// Constructors of the MovePicker class. As arguments we pass information
/// to help it to return the presumably good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions and some checks) and about how important good
/// move ordering is at the current node.

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const History& h,
                       Search::Stack* s, Value beta) : pos(p), H(h), depth(d) {

  assert(d > DEPTH_ZERO);

  captureThreshold = 0;
  cur = end = moves;
  endBadCaptures = moves + MAX_MOVES - 1;
  ss = s;

  if (p.in_check())
      phase = EVASION;

  else
  {
      phase = MAIN_SEARCH;

      killers[0].move = ss->killers[0];
      killers[1].move = ss->killers[1];

      // Consider sligtly negative captures as good if at low depth and far from beta
      if (ss && ss->eval < beta - PawnValueMg && d < 3 * ONE_PLY)
          captureThreshold = -PawnValueMg;

      // Consider negative captures as good if still enough to reach beta
      else if (ss && ss->eval > beta)
          captureThreshold = beta - ss->eval;
  }

  ttMove = (ttm && pos.is_pseudo_legal(ttm) ? ttm : MOVE_NONE);
  end += (ttMove != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const History& h,
                       Square sq) : pos(p), H(h), cur(moves), end(moves) {

  assert(d <= DEPTH_ZERO);

  if (p.in_check())
      phase = EVASION;

  else if (d > DEPTH_QS_NO_CHECKS)
      phase = QSEARCH_0;

  else if (d > DEPTH_QS_RECAPTURES)
  {
      phase = QSEARCH_1;

      // Skip TT move if is not a capture or a promotion, this avoids qsearch
      // tree explosion due to a possible perpetual check or similar rare cases
      // when TT table is full.
      if (ttm && !pos.is_capture_or_promotion(ttm))
          ttm = MOVE_NONE;
  }
  else
  {
      phase = RECAPTURE;
      recaptureSquare = sq;
      ttm = MOVE_NONE;
  }

  ttMove = (ttm && pos.is_pseudo_legal(ttm) ? ttm : MOVE_NONE);
  end += (ttMove != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, const History& h, PieceType pt)
                       : pos(p), H(h), cur(moves), end(moves) {

  assert(!pos.in_check());

  phase = PROBCUT;

  // In ProbCut we generate only captures better than parent's captured piece
  captureThreshold = PieceValue[Mg][pt];
  ttMove = (ttm && pos.is_pseudo_legal(ttm) ? ttm : MOVE_NONE);

  if (ttMove && (!pos.is_capture(ttMove) ||  pos.see(ttMove) <= captureThreshold))
      ttMove = MOVE_NONE;

  end += (ttMove != MOVE_NONE);
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

  for (MoveStack* it = moves; it != end; ++it)
  {
      m = it->move;
      it->score =  PieceValue[Mg][pos.piece_on(to_sq(m))]
                 - type_of(pos.piece_moved(m));

      if (type_of(m) == PROMOTION)
          it->score += PieceValue[Mg][promotion_type(m)];
  }
}

void MovePicker::score_noncaptures() {

  Move m;

  for (MoveStack* it = moves; it != end; ++it)
  {
      m = it->move;
      it->score = H.value(pos.piece_moved(m), to_sq(m));
  }
}

void MovePicker::score_evasions() {
  // Try good captures ordered by MVV/LVA, then non-captures if destination square
  // is not under attack, ordered by history value, then bad-captures and quiet
  // moves with a negative SEE. This last group is ordered by the SEE score.
  Move m;
  int seeScore;

  if (end < moves + 2)
      return;

  for (MoveStack* it = moves; it != end; ++it)
  {
      m = it->move;
      if ((seeScore = pos.see_sign(m)) < 0)
          it->score = seeScore - History::MaxValue; // Be sure we are at the bottom
      else if (pos.is_capture(m))
          it->score =  PieceValue[Mg][pos.piece_on(to_sq(m))]
                     - type_of(pos.piece_moved(m)) + History::MaxValue;
      else
          it->score = H.value(pos.piece_moved(m), to_sq(m));
  }
}


/// MovePicker::generate_next() generates, scores and sorts the next bunch of moves,
/// when there are no more moves to try for the current phase.

void MovePicker::generate_next() {

  cur = moves;

  switch (++phase) {

  case CAPTURES_S1: case CAPTURES_S3: case CAPTURES_S4: case CAPTURES_S5: case CAPTURES_S6:
      end = generate<CAPTURES>(pos, moves);
      score_captures();
      return;

  case KILLERS_S1:
      cur = killers;
      end = cur + 2;
      return;

  case QUIETS_1_S1:
      endQuiets = end = generate<QUIETS>(pos, moves);
      score_noncaptures();
      end = std::partition(cur, end, has_positive_score);
      sort<MoveStack>(cur, end);
      return;

  case QUIETS_2_S1:
      cur = end;
      end = endQuiets;
      if (depth >= 3 * ONE_PLY)
          sort<MoveStack>(cur, end);
      return;

  case BAD_CAPTURES_S1:
      // Just pick them in reverse order to get MVV/LVA ordering
      cur = moves + MAX_MOVES - 1;
      end = endBadCaptures;
      return;

  case EVASIONS_S2:
      end = generate<EVASIONS>(pos, moves);
      score_evasions();
      return;

  case QUIET_CHECKS_S3:
      end = generate<QUIET_CHECKS>(pos, moves);
      return;

  case EVASION: case QSEARCH_0: case QSEARCH_1: case PROBCUT: case RECAPTURE:
      phase = STOP;
  case STOP:
      end = cur + 1; // Avoid another next_phase() call
      return;

  default:
      assert(false);
  }
}


/// MovePicker::next_move() is the most important method of the MovePicker class.
/// It returns a new pseudo legal move every time it is called, until there
/// are no more moves left. It picks the move with the biggest score from a list
/// of generated moves taking care not to return the tt move if has already been
/// searched previously.
template<>
Move MovePicker::next_move<false>() {

  Move move;

  while (true)
  {
      while (cur == end)
          generate_next();

      switch (phase) {

      case MAIN_SEARCH: case EVASION: case QSEARCH_0: case QSEARCH_1: case PROBCUT:
          cur++;
          return ttMove;

      case CAPTURES_S1:
          move = pick_best(cur++, end)->move;
          if (move != ttMove)
          {
              assert(captureThreshold <= 0); // Otherwise we cannot use see_sign()

              if (pos.see_sign(move) >= captureThreshold)
                  return move;

              // Losing capture, move it to the tail of the array
              (endBadCaptures--)->move = move;
          }
          break;

      case KILLERS_S1:
          move = (cur++)->move;
          if (    move != MOVE_NONE
              &&  pos.is_pseudo_legal(move)
              &&  move != ttMove
              && !pos.is_capture(move))
              return move;
          break;

      case QUIETS_1_S1: case QUIETS_2_S1:
          move = (cur++)->move;
          if (   move != ttMove
              && move != killers[0].move
              && move != killers[1].move)
              return move;
          break;

      case BAD_CAPTURES_S1:
          return (cur--)->move;

      case EVASIONS_S2: case CAPTURES_S3: case CAPTURES_S4:
          move = pick_best(cur++, end)->move;
          if (move != ttMove)
              return move;
          break;

      case CAPTURES_S5:
           move = pick_best(cur++, end)->move;
           if (move != ttMove && pos.see(move) > captureThreshold)
               return move;
           break;

      case CAPTURES_S6:
          move = pick_best(cur++, end)->move;
          if (to_sq(move) == recaptureSquare)
              return move;
          break;

      case QUIET_CHECKS_S3:
          move = (cur++)->move;
          if (move != ttMove)
              return move;
          break;

      case STOP:
          return MOVE_NONE;

      default:
          assert(false);
      }
  }
}


/// Version of next_move() to use at split point nodes where the move is grabbed
/// from the split point's shared MovePicker object. This function is not thread
/// safe so should be lock protected by the caller.
template<>
Move MovePicker::next_move<true>() { return ss->sp->mp->next_move<false>(); }
