/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <cassert>

#include "movepick.h"
#include "thread.h"

namespace {

  enum Stages {
    MAIN_SEARCH, CAPTURES_S1, KILLERS_S1, QUIETS_1_S1, QUIETS_2_S1, BAD_CAPTURES_S1,
    EVASION,     EVASIONS_S2,
    QSEARCH_0,   CAPTURES_S3, QUIET_CHECKS_S3,
    QSEARCH_1,   CAPTURES_S4,
    PROBCUT,     CAPTURES_S5,
    RECAPTURE,   CAPTURES_S6,
    STOP
  };

  // Our insertion sort, which is guaranteed (and also needed) to be stable
  void insertion_sort(ExtMove* begin, ExtMove* end)
  {
    ExtMove tmp, *p, *q;

    for (p = begin + 1; p < end; ++p)
    {
        tmp = *p;
        for (q = p; q != begin && *(q-1) < tmp; --q)
            *q = *(q-1);
        *q = tmp;
    }
  }

  // Unary predicate used by std::partition to split positive values from remaining
  // ones so as to sort the two sets separately, with the second sort delayed.
  inline bool has_positive_value(const ExtMove& ms) { return ms.value > 0; }

  // Picks the best move in the range (begin, end) and moves it to the front.
  // It's faster than sorting all the moves in advance when there are few
  // moves e.g. possible captures.
  inline ExtMove* pick_best(ExtMove* begin, ExtMove* end)
  {
      std::swap(*begin, *std::max_element(begin, end));
      return begin;
  }
}


/// Constructors of the MovePicker class. As arguments we pass information
/// to help it to return the (presumably) good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions and some checks) and how important good move
/// ordering is at the current node.

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const HistoryStats& h,
                       Move* cm, Move* fm, Search::Stack* s) : pos(p), history(h), depth(d) {

  assert(d > DEPTH_ZERO);

  cur = end = moves;
  endBadCaptures = moves + MAX_MOVES - 1;
  countermoves = cm;
  followupmoves = fm;
  ss = s;

  if (pos.checkers())
      stage = EVASION;

  else
      stage = MAIN_SEARCH;

  ttMove = (ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE);
  end += (ttMove != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const HistoryStats& h,
                       Square s) : pos(p), history(h), cur(moves), end(moves) {

  assert(d <= DEPTH_ZERO);

  if (pos.checkers())
      stage = EVASION;

  else if (d > DEPTH_QS_NO_CHECKS)
      stage = QSEARCH_0;

  else if (d > DEPTH_QS_RECAPTURES)
  {
      stage = QSEARCH_1;

      // Skip TT move if is not a capture or a promotion. This avoids qsearch
      // tree explosion due to a possible perpetual check or similar rare cases
      // when TT table is full.
      if (ttm && !pos.capture_or_promotion(ttm))
          ttm = MOVE_NONE;
  }
  else
  {
      stage = RECAPTURE;
      recaptureSquare = s;
      ttm = MOVE_NONE;
  }

  ttMove = (ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE);
  end += (ttMove != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, const HistoryStats& h, PieceType pt)
                       : pos(p), history(h), cur(moves), end(moves) {

  assert(!pos.checkers());

  stage = PROBCUT;

  // In ProbCut we generate only captures that are better than the parent's
  // captured piece.
  captureThreshold = PieceValue[MG][pt];
  ttMove = (ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE);

  if (ttMove && (!pos.capture(ttMove) || pos.see(ttMove) <= captureThreshold))
      ttMove = MOVE_NONE;

  end += (ttMove != MOVE_NONE);
}


/// score() assign a numerical value to each move in a move list. The moves with
/// highest values will be picked first.
template<>
void MovePicker::score<CAPTURES>() {
  // Winning and equal captures in the main search are ordered by MVV/LVA.
  // Suprisingly, this appears to perform slightly better than SEE based
  // move ordering. The reason is probably that in a position with a winning
  // capture, capturing a more valuable (but sufficiently defended) piece
  // first usually doesn't hurt. The opponent will have to recapture, and
  // the hanging piece will still be hanging (except in the unusual cases
  // where it is possible to recapture with the hanging piece). Exchanging
  // big pieces before capturing a hanging piece probably helps to reduce
  // the subtree size.
  // In main search we want to push captures with negative SEE values to the
  // badCaptures[] array, but instead of doing it now we delay until the move
  // has been picked up in pick_move_from_list(). This way we save some SEE
  // calls in case we get a cutoff.
  Move m;

  for (ExtMove* it = moves; it != end; ++it)
  {
      m = it->move;
      it->value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                 - Value(type_of(pos.moved_piece(m)));

      if (type_of(m) == ENPASSANT)
          it->value += PieceValue[MG][PAWN];

      else if (type_of(m) == PROMOTION)
          it->value += PieceValue[MG][promotion_type(m)] - PieceValue[MG][PAWN];
  }
}

template<>
void MovePicker::score<QUIETS>() {

  Move m;

  for (ExtMove* it = moves; it != end; ++it)
  {
      m = it->move;
      it->value = history[pos.moved_piece(m)][to_sq(m)];
  }
}

template<>
void MovePicker::score<EVASIONS>() {
  // Try good captures ordered by MVV/LVA, then non-captures if destination square
  // is not under attack, ordered by history value, then bad-captures and quiet
  // moves with a negative SEE. This last group is ordered by the SEE value.
  Move m;
  Value see;

  for (ExtMove* it = moves; it != end; ++it)
  {
      m = it->move;
      if ((see = pos.see_sign(m)) < VALUE_ZERO)
          it->value = see - HistoryStats::Max; // At the bottom

      else if (pos.capture(m))
          it->value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                     - Value(type_of(pos.moved_piece(m))) + HistoryStats::Max;
      else
          it->value = history[pos.moved_piece(m)][to_sq(m)];
  }
}


/// generate_next_stage() generates, scores and sorts the next bunch of moves,
/// when there are no more moves to try for the current stage.

void MovePicker::generate_next_stage() {

  cur = moves;

  switch (++stage) {

  case CAPTURES_S1: case CAPTURES_S3: case CAPTURES_S4: case CAPTURES_S5: case CAPTURES_S6:
      end = generate<CAPTURES>(pos, moves);
      score<CAPTURES>();
      return;

  case KILLERS_S1:
      cur = killers;
      end = cur + 2;

      killers[0].move = ss->killers[0];
      killers[1].move = ss->killers[1];
      killers[2].move = killers[3].move = MOVE_NONE;
      killers[4].move = killers[5].move = MOVE_NONE;

      // Please note that following code is racy and could yield to rare (less
      // than 1 out of a million) duplicated entries in SMP case. This is harmless.

      // Be sure countermoves are different from killers
      for (int i = 0; i < 2; ++i)
          if (   countermoves[i] != (cur+0)->move
              && countermoves[i] != (cur+1)->move)
              (end++)->move = countermoves[i];

      // Be sure followupmoves are different from killers and countermoves
      for (int i = 0; i < 2; ++i)
          if (   followupmoves[i] != (cur+0)->move
              && followupmoves[i] != (cur+1)->move
              && followupmoves[i] != (cur+2)->move
              && followupmoves[i] != (cur+3)->move)
              (end++)->move = followupmoves[i];
      return;

  case QUIETS_1_S1:
      endQuiets = end = generate<QUIETS>(pos, moves);
      score<QUIETS>();
      end = std::partition(cur, end, has_positive_value);
      insertion_sort(cur, end);
      return;

  case QUIETS_2_S1:
      cur = end;
      end = endQuiets;
      if (depth >= 3 * ONE_PLY)
          insertion_sort(cur, end);
      return;

  case BAD_CAPTURES_S1:
      // Just pick them in reverse order to get MVV/LVA ordering
      cur = moves + MAX_MOVES - 1;
      end = endBadCaptures;
      return;

  case EVASIONS_S2:
      end = generate<EVASIONS>(pos, moves);
      if (end > moves + 1)
          score<EVASIONS>();
      return;

  case QUIET_CHECKS_S3:
      end = generate<QUIET_CHECKS>(pos, moves);
      return;

  case EVASION: case QSEARCH_0: case QSEARCH_1: case PROBCUT: case RECAPTURE:
      stage = STOP;
      /* Fall through */

  case STOP:
      end = cur + 1; // Avoid another next_phase() call
      return;

  default:
      assert(false);
  }
}


/// next_move() is the most important method of the MovePicker class. It returns
/// a new pseudo legal move every time it is called, until there are no more moves
/// left. It picks the move with the biggest value from a list of generated moves
/// taking care not to return the ttMove if it has already been searched.
template<>
Move MovePicker::next_move<false>() {

  Move move;

  while (true)
  {
      while (cur == end)
          generate_next_stage();

      switch (stage) {

      case MAIN_SEARCH: case EVASION: case QSEARCH_0: case QSEARCH_1: case PROBCUT:
          ++cur;
          return ttMove;

      case CAPTURES_S1:
          move = pick_best(cur++, end)->move;
          if (move != ttMove)
          {
              if (pos.see_sign(move) >= VALUE_ZERO)
                  return move;

              // Losing capture, move it to the tail of the array
              (endBadCaptures--)->move = move;
          }
          break;

      case KILLERS_S1:
          move = (cur++)->move;
          if (    move != MOVE_NONE
              &&  move != ttMove
              &&  pos.pseudo_legal(move)
              && !pos.capture(move))
              return move;
          break;

      case QUIETS_1_S1: case QUIETS_2_S1:
          move = (cur++)->move;
          if (   move != ttMove
              && move != killers[0].move
              && move != killers[1].move
              && move != killers[2].move
              && move != killers[3].move
              && move != killers[4].move
              && move != killers[5].move)
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
/// safe so must be lock protected by the caller.
template<>
Move MovePicker::next_move<true>() { return ss->splitPoint->movePicker->next_move<false>(); }
