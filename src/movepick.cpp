/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

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

namespace Stockfish {

namespace {

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

MovePicker::MovePicker(const Position& p, Move ttm) :
    pos(p),
    ttMove(ttm) {
}

/// MovePicker constructor for the main search
MovePicker_Main::MovePicker_Main(const Position& p,
                                 Move ttm,
                                 Depth d,
                                 const ButterflyHistory* mh,
                                 const LowPlyHistory* lp,
                                 const CapturePieceToHistory* cph,
                                 const PieceToHistory** ch,
                                 Move cm,
                                 const Move* killers,
                                 int pl) :
    MovePicker(p, ttm),
    mainHistory(mh),
    lowPlyHistory(lp),
    captureHistory(cph),
    continuationHistory(ch),
    refutations{ { killers[0], 0 }, { killers[1], 0 }, { cm, 0 } },
    depth(d),
    ply(pl) {

  assert(d > 0);

  const bool evasion = pos.checkers();
  const bool tt      = ttm && pos.pseudo_legal(ttm);

  stage = get_start_stage(evasion, tt);
}

MovePicker_Main::Stages MovePicker_Main::get_start_stage(bool evasion, bool tt) {
  static constexpr Stages startStages[2][2] = {
    { CAPTURE_INIT, MAIN_TT },
    { EVASION_INIT, EVASION_TT }
  };

  return startStages[evasion][tt];
}

/// MovePicker constructor for quiescence search
MovePicker_Quiescence::MovePicker_Quiescence(const Position& p,
                                             Move ttm,
                                             Depth d,
                                             const ButterflyHistory* mh,
                                             const CapturePieceToHistory* cph,
                                             const PieceToHistory** ch,
                                             Square rs) :
    MovePicker(p, ttm),
    mainHistory(mh),
    captureHistory(cph),
    continuationHistory(ch),
    recaptureSquare(rs),
    depth(d) {

  assert(d <= 0);

  const bool evasion = pos.checkers();
  const bool tt      =    ttm
                       && (   pos.checkers()
                           || depth > DEPTH_QS_RECAPTURES
                           || to_sq(ttm) == recaptureSquare)
                       && pos.pseudo_legal(ttm);

  stage = get_start_stage(evasion, tt);
}

MovePicker_Quiescence::Stages MovePicker_Quiescence::get_start_stage(bool evasion, bool tt) {
  static constexpr Stages startStages[2][2] = {
    { QCAPTURE_INIT, QSEARCH_TT },
    { EVASION_INIT, EVASION_TT }
  };

  return startStages[evasion][tt];
}

/// MovePicker constructor for ProbCut: we generate captures with SEE greater
/// than or equal to the given threshold.
MovePicker_ProbCut::MovePicker_ProbCut(const Position& p,
                                       Move ttm,
                                       Value th,
                                       const CapturePieceToHistory* cph) :
    MovePicker(p, ttm),
    captureHistory(cph),
    threshold(th) {

  assert(!pos.checkers());

  const bool tt =    ttm
                  && pos.capture(ttm)
                  && pos.pseudo_legal(ttm)
                  && pos.see_ge(ttm, threshold);

  stage = get_start_stage(tt);
}

MovePicker_ProbCut::Stages MovePicker_ProbCut::get_start_stage(bool tt) {
  static constexpr Stages startStages[2] = {
    PROBCUT_INIT, PROBCUT_TT
  };

  return startStages[tt];
}

/// MovePicker::select() returns the next move satisfying a predicate function.
/// It never returns the TT move.
template<MovePicker::PickType T, typename Pred>
Move MovePicker::select(Pred filter) {

  while (cur < endMoves)
  {
      if (T == Best)
          std::swap(*cur, *std::max_element(cur, endMoves));

      if (*cur != ttMove && filter())
          return *cur++;

      cur++;
  }
  return MOVE_NONE;
}

/// MovePicker_*::next_move() is the most important method of the MovePicker_* classes. It
/// returns a new pseudo-legal move every time it is called until there are no more
/// moves left, picking the move with the highest score from a list of generated moves.

Move MovePicker_Main::next_move(bool skipQuiets) {

  switch (stage) {

  case MAIN_TT:
      ++stage;
      return ttMove;

  case CAPTURE_INIT:
      cur = endBadCaptures = moves;
      endMoves = generate<CAPTURES>(pos, cur);

      score_captures();
      ++stage;
      [[fallthrough]];

  case GOOD_CAPTURE:
      if (select<Best>([&](){
                       return pos.see_ge(*cur, Value(-69 * cur->value / 1024)) ?
                              // Move losing capture to endBadCaptures to be tried later
                              true : (*endBadCaptures++ = *cur, false); }))
          return *(cur - 1);

      // Prepare the pointers to loop over the refutations array
      cur = std::begin(refutations);
      endMoves = std::end(refutations);

      // If the countermove is the same as a killer, skip it
      if (   refutations[0].move == refutations[2].move
          || refutations[1].move == refutations[2].move)
          --endMoves;

      ++stage;
      [[fallthrough]];

  case REFUTATION:
      if (select<Next>([&](){ return    *cur != MOVE_NONE
                                    && !pos.capture(*cur)
                                    &&  pos.pseudo_legal(*cur); }))
          return *(cur - 1);
      ++stage;
      [[fallthrough]];

  case QUIET_INIT:
      if (!skipQuiets)
      {
          cur = endBadCaptures;
          endMoves = generate<QUIETS>(pos, cur);

          score_quiets();
          partial_insertion_sort(cur, endMoves, -3000 * depth);
      }

      ++stage;
      [[fallthrough]];

  case QUIET:
      if (   !skipQuiets
          && select<Next>([&](){return   *cur != refutations[0].move
                                      && *cur != refutations[1].move
                                      && *cur != refutations[2].move;}))
          return *(cur - 1);

      // Prepare the pointers to loop over the bad captures
      cur = moves;
      endMoves = endBadCaptures;

      ++stage;
      [[fallthrough]];

  case BAD_CAPTURE:
      return select<Next>([](){ return true; });



  case EVASION_TT:
      ++stage;
      return ttMove;

  case EVASION_INIT:
      cur = moves;
      endMoves = generate<EVASIONS>(pos, cur);

      score_evasions();
      ++stage;
      [[fallthrough]];

  case EVASION:
      return select<Best>([](){ return true; });
  }

  assert(false);
  return MOVE_NONE; // Silence warning
}

Move MovePicker_Quiescence::next_move() {

  switch (stage) {

  case QSEARCH_TT:
      ++stage;
      return ttMove;

  case QCAPTURE_INIT:
      cur = endBadCaptures = moves;
      endMoves = generate<CAPTURES>(pos, cur);

      score_captures();
      ++stage;
      [[fallthrough]];

  case QCAPTURE:
      if (select<Best>([&](){ return   depth > DEPTH_QS_RECAPTURES
                                    || to_sq(*cur) == recaptureSquare; }))
          return *(cur - 1);

      // If we did not find any move and we do not try checks, we have finished
      if (depth != DEPTH_QS_CHECKS)
          return MOVE_NONE;

      ++stage;
      [[fallthrough]];

  case QCHECK_INIT:
      cur = moves;
      endMoves = generate<QUIET_CHECKS>(pos, cur);

      ++stage;
      [[fallthrough]];

  case QCHECK:
      return select<Next>([](){ return true; });



  case EVASION_TT:
      ++stage;
      return ttMove;

  case EVASION_INIT:
      cur = moves;
      endMoves = generate<EVASIONS>(pos, cur);

      score_evasions();
      ++stage;
      [[fallthrough]];

  case EVASION:
      return select<Best>([](){ return true; });
  }

  assert(false);
  return MOVE_NONE; // Silence warning
}

Move MovePicker_ProbCut::next_move() {

  switch (stage) {

  case PROBCUT_TT:
      ++stage;
      return ttMove;

  case PROBCUT_INIT:
      cur = endBadCaptures = moves;
      endMoves = generate<CAPTURES>(pos, cur);

      score_captures();
      ++stage;
      [[fallthrough]];

  case PROBCUT:
      return select<Best>([&](){ return pos.see_ge(*cur, threshold); });
  }

  assert(false);
  return MOVE_NONE; // Silence warning
}

} // namespace Stockfish
