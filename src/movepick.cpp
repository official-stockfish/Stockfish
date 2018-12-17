/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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
    MAIN_TT, CAPTURE_INIT, GOOD_CAPTURE, REFUTATION, QUIET_INIT, QUIET, BAD_CAPTURE,
    EVASION_TT, EVASION_INIT, EVASION,
    PROBCUT_TT, PROBCUT_INIT, PROBCUT,
    QSEARCH_TT, QCAPTURE_INIT, QCAPTURE, QCHECK_INIT, QCHECK
  };

  // Helper filter used with select()
  const auto Any = [](){ return true; };

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

} // namespace


/// Constructors of the MovePicker class. As arguments we pass information
/// to help it to return the (presumably) good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions, and some checks) and how important good move
/// ordering is at the current node.

/// MovePicker constructor for the main search
MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const ButterflyHistory* mh,
                       const CapturePieceToHistory* cph, const PieceToHistory** ch, Move cm, Move* killers)
           : pos(p), mainHistory(mh), captureHistory(cph), continuationHistory(ch),
             refutations{{killers[0], 0}, {killers[1], 0}, {cm, 0}}, depth(d) {

  assert(d > DEPTH_ZERO);

  stage = pos.checkers() ? EVASION_TT : MAIN_TT;
  ttMove = ttm && pos.pseudo_legal(ttm) ? ttm : MOVE_NONE;
  stage += (ttMove == MOVE_NONE);
}

/// MovePicker constructor for quiescence search
MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const ButterflyHistory* mh,
                       const CapturePieceToHistory* cph, const PieceToHistory** ch, Square rs)
           : pos(p), mainHistory(mh), captureHistory(cph), continuationHistory(ch), recaptureSquare(rs), depth(d) {

  assert(d <= DEPTH_ZERO);

  stage = pos.checkers() ? EVASION_TT : QSEARCH_TT;
  ttMove =    ttm
           && pos.pseudo_legal(ttm)
           && (depth > DEPTH_QS_RECAPTURES || to_sq(ttm) == recaptureSquare) ? ttm : MOVE_NONE;
  stage += (ttMove == MOVE_NONE);
}

/// MovePicker constructor for ProbCut: we generate captures with SEE greater
/// than or equal to the given threshold.
MovePicker::MovePicker(const Position& p, Move ttm, Value th, const CapturePieceToHistory* cph)
           : pos(p), captureHistory(cph), threshold(th) {

  assert(!pos.checkers());

  stage = PROBCUT_TT;
  ttMove =   ttm
          && pos.pseudo_legal(ttm)
          && pos.capture(ttm)
          && pos.see_ge(ttm, threshold) ? ttm : MOVE_NONE;
  stage += (ttMove == MOVE_NONE);
}

/// MovePicker::score() assigns a numerical value to each move in a list, used
/// for sorting. Captures are ordered by Most Valuable Victim (MVV), preferring
/// captures with a good history. Quiets moves are ordered using the histories.
template<GenType Type>
void MovePicker::score() {

  static_assert(Type == CAPTURES || Type == QUIETS || Type == EVASIONS, "Wrong type");

  for (auto& m : *this)
      if (Type == CAPTURES)
          m.value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                   + (*captureHistory)[pos.moved_piece(m)][to_sq(m)][type_of(pos.piece_on(to_sq(m)))] / 8;

      else if (Type == QUIETS)
          m.value =  (*mainHistory)[pos.side_to_move()][from_to(m)]
                   + (*continuationHistory[0])[pos.moved_piece(m)][to_sq(m)]
                   + (*continuationHistory[1])[pos.moved_piece(m)][to_sq(m)]
                   + (*continuationHistory[3])[pos.moved_piece(m)][to_sq(m)];

      else // Type == EVASIONS
      {
          if (pos.capture(m))
              m.value =  PieceValue[MG][pos.piece_on(to_sq(m))]
                       - Value(type_of(pos.moved_piece(m)));
          else
              m.value =  (*mainHistory)[pos.side_to_move()][from_to(m)]
                       + (*continuationHistory[0])[pos.moved_piece(m)][to_sq(m)]
                       - (1 << 28);
      }
}

/// MovePicker::select() returns the next move satisfying a predicate function.
/// It never returns the TT move.
template<MovePicker::PickType T, typename Pred>
Move MovePicker::select(Pred filter) {

  while (cur < endMoves)
  {
      if (T == Best)
          std::swap(*cur, *std::max_element(cur, endMoves));

      move = *cur++;

      if (move != ttMove && filter())
          return move;
  }
  return move = MOVE_NONE;
}

/// MovePicker::next_move() is the most important method of the MovePicker class. It
/// returns a new pseudo legal move every time it is called until there are no more
/// moves left, picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move(bool skipQuiets) {

top:
  switch (stage) {

  case MAIN_TT:
  case EVASION_TT:
  case QSEARCH_TT:
  case PROBCUT_TT:
      ++stage;
      return ttMove;

  case CAPTURE_INIT:
  case PROBCUT_INIT:
  case QCAPTURE_INIT:
      cur = endBadCaptures = moves;
      endMoves = generate<CAPTURES>(pos, cur);

      score<CAPTURES>();
      ++stage;
      goto top;

  case GOOD_CAPTURE:
      if (select<Best>([&](){
                       return pos.see_ge(move, Value(-55 * (cur-1)->value / 1024)) ?
                              // Move losing capture to endBadCaptures to be tried later
                              true : (*endBadCaptures++ = move, false); }))
          return move;

      // Prepare the pointers to loop over the refutations array
      cur = std::begin(refutations);
      endMoves = std::end(refutations);

      // If the countermove is the same as a killer, skip it
      if (   refutations[0].move == refutations[2].move
          || refutations[1].move == refutations[2].move)
          --endMoves;

      ++stage;
      /* fallthrough */

  case REFUTATION:
      if (select<Next>([&](){ return    move != MOVE_NONE
                                    && !pos.capture(move)
                                    &&  pos.pseudo_legal(move); }))
          return move;
      ++stage;
      /* fallthrough */

  case QUIET_INIT:
      cur = endBadCaptures;
      endMoves = generate<QUIETS>(pos, cur);

      score<QUIETS>();
      partial_insertion_sort(cur, endMoves, -4000 * depth / ONE_PLY);
      ++stage;
      /* fallthrough */

  case QUIET:
      if (   !skipQuiets
          && select<Next>([&](){return   move != refutations[0]
                                      && move != refutations[1]
                                      && move != refutations[2];}))
          return move;

      // Prepare the pointers to loop over the bad captures
      cur = moves;
      endMoves = endBadCaptures;

      ++stage;
      /* fallthrough */

  case BAD_CAPTURE:
      return select<Next>(Any);

  case EVASION_INIT:
      cur = moves;
      endMoves = generate<EVASIONS>(pos, cur);

      score<EVASIONS>();
      ++stage;
      /* fallthrough */

  case EVASION:
      return select<Best>(Any);

  case PROBCUT:
      return select<Best>([&](){ return pos.see_ge(move, threshold); });

  case QCAPTURE:
      if (select<Best>([&](){ return   depth > DEPTH_QS_RECAPTURES
                                    || to_sq(move) == recaptureSquare; }))
          return move;

      // If we did not find any move and we do not try checks, we have finished
      if (depth != DEPTH_QS_CHECKS)
          return MOVE_NONE;

      ++stage;
      /* fallthrough */

  case QCHECK_INIT:
      cur = moves;
      endMoves = generate<QUIET_CHECKS>(pos, cur);

      ++stage;
      /* fallthrough */

  case QCHECK:
      return select<Next>(Any);
  }

  assert(false);
  return MOVE_NONE; // Silence warning
}
