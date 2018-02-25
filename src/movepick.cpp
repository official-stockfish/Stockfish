/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2018 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

namespace {

  enum Stages {
    MAIN_SEARCH, CAPTURES_INIT, GOOD_CAPTURES, KILLER0, KILLER1, COUNTERMOVE, QUIET_INIT, QUIET, BAD_CAPTURES,
    EVASION, EVASIONS_INIT, ALL_EVASIONS,
    PROBCUT, PROBCUT_CAPTURES_INIT, PROBCUT_CAPTURES,
    QSEARCH, QCAPTURES_INIT, QCAPTURES, QCHECKS, QSEARCH_RECAPTURES, QRECAPTURES
  };

  // partial_insertion_sort() sorts moves in descending order up to and including
  // a given limit. The order of moves smaller than the limit is left unspecified.
  void partial_insertion_sort(ExtMove* begin, ExtMove* end, int limit) {

    for (ExtMove *sortedEnd = begin, *p = begin + 1; p < end; ++p)
        if (p->value >= limit)
        {
            ExtMove tmp = *p, *q;
            *p = *++sortedEnd;
            for (q = sortedEnd; q != begin && *(q - 1) < tmp; --q)
                *q = *(q - 1);
            *q = tmp;
        }
  }

  // pick_best() finds the best move in the range (begin, end) and moves it to
  // the front. It's faster than sorting all the moves in advance when there
  // are few moves, e.g., the possible captures.
  Move pick_best(ExtMove* begin, ExtMove* end) {

    std::swap(*begin, *std::max_element(begin, end));
    return *begin;
  }

} // namespace


/// Constructors of the MovePicker class. As arguments we pass information
/// to help it to return the (presumably) good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions, and some checks) and how important good move
/// ordering is at the current node.

/// MovePicker constructor for the main search
MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const ButterflyHistory* mh,
                       const CapturePieceToHistory* cph, const PieceToHistory** ch, Move cm, Move* killers_p)
           : pos(p), mainHistory(mh), captureHistory(cph), contHistory(ch), countermove(cm),
             killers{killers_p[0], killers_p[1]}, depth(d){

  assert(d > DEPTH_ZERO);

  stage = pos.checkers() ? EVASION : MAIN_SEARCH;
  ttMove = ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE;
  stage += (ttMove == MOVE_NONE);
}

/// MovePicker constructor for quiescence search
MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const ButterflyHistory* mh,  const CapturePieceToHistory* cph, Square s)
           : pos(p), mainHistory(mh), captureHistory(cph), depth(d) {

  assert(d <= DEPTH_ZERO);

  if (pos.checkers())
      stage = EVASION;

  else if (d > DEPTH_QS_RECAPTURES)
      stage = QSEARCH;

  else
  {
      stage = QSEARCH_RECAPTURES;
      recaptureSquare = s;
      return;
  }

  ttMove = ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE;
  stage += (ttMove == MOVE_NONE);
}

/// MovePicker constructor for ProbCut: we generate captures with SEE higher
/// than or equal to the given threshold.
MovePicker::MovePicker(const Position& p, Move ttm, Value th, const CapturePieceToHistory* cph)
           : pos(p), captureHistory(cph), threshold(th) {

  assert(!pos.checkers());

  stage = PROBCUT;
  ttMove =   ttm
          && pos.pseudo_legal(ttm)
          && pos.capture(ttm)
          && pos.see_ge(ttm, threshold) ? ttm : MOVE_NONE;

  stage += (ttMove == MOVE_NONE);
}

/// score() assigns a numerical value to each move in a list, used for sorting.
/// Captures are ordered by Most Valuable Victim (MVV), preferring captures
/// with a good history. Quiets are ordered using the histories.
template<GenType Type>
void MovePicker::score() {

  static_assert(Type == CAPTURES || Type == QUIETS || Type == EVASIONS, "Wrong type");

  for (auto& m : *this)
      if (Type == CAPTURES)
          m.value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                   + Value((*captureHistory)[pos.moved_piece(m)][to_sq(m)][type_of(pos.piece_on(to_sq(m)))]);

      else if (Type == QUIETS)
          m.value =  (*mainHistory)[pos.side_to_move()][from_to(m)]
                   + (*contHistory[0])[pos.moved_piece(m)][to_sq(m)]
                   + (*contHistory[1])[pos.moved_piece(m)][to_sq(m)]
                   + (*contHistory[3])[pos.moved_piece(m)][to_sq(m)];

      else // Type == EVASIONS
      {
          if (pos.capture(m))
              m.value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                       - Value(type_of(pos.moved_piece(m)));
          else
              m.value = (*mainHistory)[pos.side_to_move()][from_to(m)] - (1 << 28);
      }
}

/// next_move() is the most important method of the MovePicker class. It returns
/// a new pseudo legal move every time it is called, until there are no more moves
/// left. It picks the move with the biggest value from a list of generated moves
/// taking care not to return the ttMove if it has already been searched.

Move MovePicker::next_move(bool skipQuiets) {

  Move move;

  switch (stage) {

  case MAIN_SEARCH: case EVASION: case QSEARCH: case PROBCUT:
      ++stage;
      return ttMove;

  case CAPTURES_INIT:
  case PROBCUT_CAPTURES_INIT:
  case QCAPTURES_INIT:
  case QSEARCH_RECAPTURES:
      endBadCaptures = cur = moves;
      endMoves = generate<CAPTURES>(pos, cur);
      score<CAPTURES>();
      ++stage;

      // Rebranch at the top of the switch via a recursive call
      return next_move(skipQuiets);

  case GOOD_CAPTURES:
      while (cur < endMoves)
      {
          move = pick_best(cur++, endMoves);
          if (move != ttMove)
          {
              if (pos.see_ge(move, Value(-55 * (cur-1)->value / 1024)))
                  return move;

              // Losing capture, move it to the beginning of the array
              *endBadCaptures++ = move;
          }
      }
      ++stage;
      /* fallthrough */

  case KILLER0:
  case KILLER1:
      do
      {
          move = killers[++stage - KILLER1];
          if (    move != MOVE_NONE
              &&  move != ttMove
              &&  pos.pseudo_legal(move)
              && !pos.capture(move))
              return move;
      } while (stage <= KILLER1);
      /* fallthrough */

  case COUNTERMOVE:
      ++stage;
      move = countermove;
      if (    move != MOVE_NONE
          &&  move != ttMove
          &&  move != killers[0]
          &&  move != killers[1]
          &&  pos.pseudo_legal(move)
          && !pos.capture(move))
          return move;
      /* fallthrough */

  case QUIET_INIT:
      cur = endBadCaptures;
      endMoves = generate<QUIETS>(pos, cur);
      score<QUIETS>();
      partial_insertion_sort(cur, endMoves, -4000 * depth / ONE_PLY);
      ++stage;
      /* fallthrough */

  case QUIET:
      if (!skipQuiets)
         while (cur < endMoves)
         {
             move = *cur++;

             if (   move != ttMove
                 && move != killers[0]
                 && move != killers[1]
                 && move != countermove)
                 return move;
         }
      ++stage;
      cur = moves; // Point to beginning of bad captures
      /* fallthrough */

  case BAD_CAPTURES:
      if (cur < endBadCaptures)
          return *cur++;
      break;

  case EVASIONS_INIT:
      cur = moves;
      endMoves = generate<EVASIONS>(pos, cur);
      score<EVASIONS>();
      ++stage;
      /* fallthrough */

  case ALL_EVASIONS:
      while (cur < endMoves)
      {
          move = pick_best(cur++, endMoves);
          if (move != ttMove)
              return move;
      }
      break;

  case PROBCUT_CAPTURES:
      while (cur < endMoves)
      {
          move = pick_best(cur++, endMoves);
          if (   move != ttMove
              && pos.see_ge(move, threshold))
              return move;
      }
      break;

  case QCAPTURES:
      while (cur < endMoves)
      {
          move = pick_best(cur++, endMoves);
          if (move != ttMove)
              return move;
      }
      if (depth <= DEPTH_QS_NO_CHECKS)
          break;
      cur = moves;
      endMoves = generate<QUIET_CHECKS>(pos, cur);
      ++stage;
      /* fallthrough */

  case QCHECKS:
      while (cur < endMoves)
      {
          move = cur++->move;
          if (move != ttMove)
              return move;
      }
      break;

  case QRECAPTURES:
      while (cur < endMoves)
      {
          move = *cur++;
          if (to_sq(move) == recaptureSquare)
              return move;
      }
      break;

  default:
      assert(false);
  }

  return MOVE_NONE;
}
