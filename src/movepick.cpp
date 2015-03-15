/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad

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

  // Our insertion sort, which is guaranteed to be stable, as it should be
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

  // pick_best() finds the best move in the range (begin, end) and moves it to
  // the front. It's faster than sorting all the moves in advance when there
  // are few moves e.g. the possible captures.
  inline Move pick_best(ExtMove* begin, ExtMove* end)
  {
      std::swap(*begin, *std::max_element(begin, end));
      return *begin;
  }

} // namespace


/// Constructors of the MovePicker class. As arguments we pass information
/// to help it to return the (presumably) good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions and some checks) and how important good move
/// ordering is at the current node.

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const HistoryStats& h, const CounterMovesHistoryStats& cmh,
                       Move* cm, Move* fm, Search::Stack* s) : pos(p), history(h), counterMovesHistory(cmh), depth(d) {

  assert(d > DEPTH_ZERO);

  endBadCaptures = moves + MAX_MOVES - 1;
  countermoves = cm;
  followupmoves = fm;
  ss = s;

  if (pos.checkers())
      stage = EVASION;

  else
      stage = MAIN_SEARCH;

  ttMove = (ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE);
  endMoves += (ttMove != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const HistoryStats& h, const CounterMovesHistoryStats& cmh,
                       Square s) : pos(p), history(h), counterMovesHistory(cmh) {

  assert(d <= DEPTH_ZERO);

  if (pos.checkers())
      stage = EVASION;

  else if (d > DEPTH_QS_NO_CHECKS)
      stage = QSEARCH_0;

  else if (d > DEPTH_QS_RECAPTURES)
      stage = QSEARCH_1;

  else
  {
      stage = RECAPTURE;
      recaptureSquare = s;
      ttm = MOVE_NONE;
  }

  ttMove = (ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE);
  endMoves += (ttMove != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, const HistoryStats& h, const CounterMovesHistoryStats& cmh, PieceType pt)
                       : pos(p), history(h), counterMovesHistory(cmh) {

  assert(!pos.checkers());

  stage = PROBCUT;

  // In ProbCut we generate only captures that are better than the parent's
  // captured piece.
  captureThreshold = PieceValue[MG][pt];
  ttMove = (ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE);

  if (ttMove && (!pos.capture(ttMove) || pos.see(ttMove) <= captureThreshold))
      ttMove = MOVE_NONE;

  endMoves += (ttMove != MOVE_NONE);
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
  for (auto& m : *this)
      if (type_of(m) == ENPASSANT)
          m.value = PieceValue[MG][PAWN] - Value(PAWN);

      else if (type_of(m) == PROMOTION)
          m.value =  PieceValue[MG][pos.piece_on(to_sq(m))] - Value(PAWN)
                   + PieceValue[MG][promotion_type(m)] - PieceValue[MG][PAWN];
      else
          m.value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                   - Value(type_of(pos.moved_piece(m)));
}

template<>
void MovePicker::score<QUIETS>() {

  Square prevSq = to_sq((ss-1)->currentMove);
  const HistoryStats& cmh = counterMovesHistory[pos.piece_on(prevSq)][prevSq];

  for (auto& m : *this)
      m.value =  history[pos.moved_piece(m)][to_sq(m)]
               + cmh[pos.moved_piece(m)][to_sq(m)];
}

template<>
void MovePicker::score<EVASIONS>() {
  // Try good captures ordered by MVV/LVA, then non-captures if destination square
  // is not under attack, ordered by history value, then bad-captures and quiet
  // moves with a negative SEE. This last group is ordered by the SEE value.
  Value see;

  for (auto& m : *this)
      if ((see = pos.see_sign(m)) < VALUE_ZERO)
          m.value = see - HistoryStats::Max; // At the bottom

      else if (pos.capture(m))
          m.value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                   - Value(type_of(pos.moved_piece(m))) + HistoryStats::Max;
      else
          m.value = history[pos.moved_piece(m)][to_sq(m)];
}


/// generate_next_stage() generates, scores and sorts the next bunch of moves,
/// when there are no more moves to try for the current stage.

void MovePicker::generate_next_stage() {

  cur = moves;

  switch (++stage) {

  case CAPTURES_S1: case CAPTURES_S3: case CAPTURES_S4: case CAPTURES_S5: case CAPTURES_S6:
      endMoves = generate<CAPTURES>(pos, moves);
      score<CAPTURES>();
      break;

  case KILLERS_S1:
      cur = killers;
      endMoves = cur + 2;

      killers[0] = ss->killers[0];
      killers[1] = ss->killers[1];
      killers[2].move = killers[3].move = MOVE_NONE;
      killers[4].move = killers[5].move = MOVE_NONE;

      // In SMP case countermoves[] and followupmoves[] could have duplicated entries
      // in rare cases (less than 1 out of a million). This is harmless.

      // Be sure countermoves and followupmoves are different from killers
      for (int i = 0; i < 2; ++i)
          if (   countermoves[i] != killers[0]
              && countermoves[i] != killers[1])
              *endMoves++ = countermoves[i];

      for (int i = 0; i < 2; ++i)
          if (   followupmoves[i] != killers[0]
              && followupmoves[i] != killers[1]
              && followupmoves[i] != killers[2]
              && followupmoves[i] != killers[3])
              *endMoves++ = followupmoves[i];
      break;

  case QUIETS_1_S1:
      endQuiets = endMoves = generate<QUIETS>(pos, moves);
      score<QUIETS>();
      endMoves = std::partition(cur, endMoves, [](const ExtMove& m) { return m.value > VALUE_ZERO; });
      insertion_sort(cur, endMoves);
      break;

  case QUIETS_2_S1:
      cur = endMoves;
      endMoves = endQuiets;
      if (depth >= 3 * ONE_PLY)
          insertion_sort(cur, endMoves);
      break;

  case BAD_CAPTURES_S1:
      // Just pick them in reverse order to get MVV/LVA ordering
      cur = moves + MAX_MOVES - 1;
      endMoves = endBadCaptures;
      break;

  case EVASIONS_S2:
      endMoves = generate<EVASIONS>(pos, moves);
      if (endMoves - moves > 1)
          score<EVASIONS>();
      break;

  case QUIET_CHECKS_S3:
      endMoves = generate<QUIET_CHECKS>(pos, moves);
      break;

  case EVASION: case QSEARCH_0: case QSEARCH_1: case PROBCUT: case RECAPTURE:
      stage = STOP;
      /* Fall through */

  case STOP:
      endMoves = cur + 1; // Avoid another generate_next_stage() call
      break;

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
      while (cur == endMoves)
          generate_next_stage();

      switch (stage) {

      case MAIN_SEARCH: case EVASION: case QSEARCH_0: case QSEARCH_1: case PROBCUT:
          ++cur;
          return ttMove;

      case CAPTURES_S1:
          move = pick_best(cur++, endMoves);
          if (move != ttMove)
          {
              if (pos.see_sign(move) >= VALUE_ZERO)
                  return move;

              // Losing capture, move it to the tail of the array
              *endBadCaptures-- = move;
          }
          break;

      case KILLERS_S1:
          move = *cur++;
          if (    move != MOVE_NONE
              &&  move != ttMove
              &&  pos.pseudo_legal(move)
              && !pos.capture(move))
              return move;
          break;

      case QUIETS_1_S1: case QUIETS_2_S1:
          move = *cur++;
          if (   move != ttMove
              && move != killers[0]
              && move != killers[1]
              && move != killers[2]
              && move != killers[3]
              && move != killers[4]
              && move != killers[5])
              return move;
          break;

      case BAD_CAPTURES_S1:
          return *cur--;

      case EVASIONS_S2: case CAPTURES_S3: case CAPTURES_S4:
          move = pick_best(cur++, endMoves);
          if (move != ttMove)
              return move;
          break;

      case CAPTURES_S5:
           move = pick_best(cur++, endMoves);
           if (move != ttMove && pos.see(move) > captureThreshold)
               return move;
           break;

      case CAPTURES_S6:
          move = pick_best(cur++, endMoves);
          if (to_sq(move) == recaptureSquare)
              return move;
          break;

      case QUIET_CHECKS_S3:
          move = *cur++;
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
