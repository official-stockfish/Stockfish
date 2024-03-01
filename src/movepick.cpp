/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#include "movepick.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

#include "bitboard.h"
#include "position.h"

namespace Stockfish {

namespace {

enum Stages {
    // generate main search moves
    MAIN_TT,
    CAPTURE_INIT,
    GOOD_CAPTURE,
    REFUTATION,
    QUIET_INIT,
    GOOD_QUIET,
    BAD_CAPTURE,
    BAD_QUIET,

    // generate evasion moves
    EVASION_TT,
    EVASION_INIT,
    EVASION,

    // generate probcut moves
    PROBCUT_TT,
    PROBCUT_INIT,
    PROBCUT,

    // generate qsearch moves
    QSEARCH_TT,
    QCAPTURE_INIT,
    QCAPTURE,
    QCHECK_INIT,
    QCHECK
};

// Sort moves in descending order up to and including
// a given limit. The order of moves smaller than the limit is left unspecified.
void partial_insertion_sort(ExtMove* begin, ExtMove* end, int limit) {

    for (ExtMove *sortedEnd = begin, *p = begin + 1; p < end; ++p)
        if (p->value >= limit)
        {
            ExtMove tmp = *p, *q;
            *p          = *++sortedEnd;
            for (q = sortedEnd; q != begin && *(q - 1) < tmp; --q)
                *q = *(q - 1);
            *q = tmp;
        }
}

}  // namespace


// Constructors of the MovePicker class. As arguments, we pass information
// to help it return the (presumably) good moves first, to decide which
// moves to return (in the quiescence search, for instance, we only want to
// search captures, promotions, and some checks) and how important a good
// move ordering is at the current node.

// MovePicker constructor for the main search
MovePicker::MovePicker(const Position&              p,
                       Move                         ttm,
                       Depth                        d,
                       const ButterflyHistory*      mh,
                       const CapturePieceToHistory* cph,
                       const PieceToHistory**       ch,
                       const PawnHistory*           ph,
                       Move                         cm,
                       const Move*                  killers) :
    pos(p),
    mainHistory(mh),
    captureHistory(cph),
    continuationHistory(ch),
    pawnHistory(ph),
    ttMove(ttm),
    refutations{{killers[0], 0}, {killers[1], 0}, {cm, 0}},
    depth(d) {
    assert(d > 0);

    stage = (pos.checkers() ? EVASION_TT : MAIN_TT) + !(ttm && pos.pseudo_legal(ttm));
}

// Constructor for quiescence search
MovePicker::MovePicker(const Position&              p,
                       Move                         ttm,
                       Depth                        d,
                       const ButterflyHistory*      mh,
                       const CapturePieceToHistory* cph,
                       const PieceToHistory**       ch,
                       const PawnHistory*           ph) :
    pos(p),
    mainHistory(mh),
    captureHistory(cph),
    continuationHistory(ch),
    pawnHistory(ph),
    ttMove(ttm),
    depth(d) {
    assert(d <= 0);

    stage = (pos.checkers() ? EVASION_TT : QSEARCH_TT) + !(ttm && pos.pseudo_legal(ttm));
}

// Constructor for ProbCut: we generate captures with SEE greater
// than or equal to the given threshold.
MovePicker::MovePicker(const Position& p, Move ttm, int th, const CapturePieceToHistory* cph) :
    pos(p),
    captureHistory(cph),
    ttMove(ttm),
    threshold(th) {
    assert(!pos.checkers());

    stage = PROBCUT_TT
          + !(ttm && pos.capture_stage(ttm) && pos.pseudo_legal(ttm) && pos.see_ge(ttm, threshold));
}

// Assigns a numerical value to each move in a list, used
// for sorting. Captures are ordered by Most Valuable Victim (MVV), preferring
// captures with a good history. Quiets moves are ordered using the history tables.
template<GenType Type>
void MovePicker::score() {

    static_assert(Type == CAPTURES || Type == QUIETS || Type == EVASIONS, "Wrong type");

    [[maybe_unused]] Bitboard threatenedByPawn, threatenedByMinor, threatenedByRook,
      threatenedPieces;
    if constexpr (Type == QUIETS)
    {
        Color us = pos.side_to_move();

        threatenedByPawn = pos.attacks_by<PAWN>(~us);
        threatenedByMinor =
          pos.attacks_by<KNIGHT>(~us) | pos.attacks_by<BISHOP>(~us) | threatenedByPawn;
        threatenedByRook = pos.attacks_by<ROOK>(~us) | threatenedByMinor;

        // Pieces threatened by pieces of lesser material value
        threatenedPieces = (pos.pieces(us, QUEEN) & threatenedByRook)
                         | (pos.pieces(us, ROOK) & threatenedByMinor)
                         | (pos.pieces(us, KNIGHT, BISHOP) & threatenedByPawn);
    }

    for (auto& m : *this)
        if constexpr (Type == CAPTURES)
            m.value =
              7 * int(PieceValue[pos.piece_on(m.to_sq())])
              + (*captureHistory)[pos.moved_piece(m)][m.to_sq()][type_of(pos.piece_on(m.to_sq()))];

        else if constexpr (Type == QUIETS)
        {
            Piece     pc   = pos.moved_piece(m);
            PieceType pt   = type_of(pc);
            Square    from = m.from_sq();
            Square    to   = m.to_sq();

            // histories
            m.value = 2 * (*mainHistory)[pos.side_to_move()][m.from_to()];
            m.value += 2 * (*pawnHistory)[pawn_structure_index(pos)][pc][to];
            m.value += 2 * (*continuationHistory[0])[pc][to];
            m.value += (*continuationHistory[1])[pc][to];
            m.value += (*continuationHistory[2])[pc][to] / 4;
            m.value += (*continuationHistory[3])[pc][to];
            m.value += (*continuationHistory[5])[pc][to];

            // bonus for checks
            m.value += bool(pos.check_squares(pt) & to) * 16384;

            // bonus for escaping from capture
            m.value += threatenedPieces & from ? (pt == QUEEN && !(to & threatenedByRook)   ? 50000
                                                  : pt == ROOK && !(to & threatenedByMinor) ? 25000
                                                  : !(to & threatenedByPawn)                ? 15000
                                                                                            : 0)
                                               : 0;

            // malus for putting piece en prise
            m.value -= !(threatenedPieces & from)
                       ? (pt == QUEEN ? bool(to & threatenedByRook) * 50000
                                          + bool(to & threatenedByMinor) * 10000
                          : pt == ROOK ? bool(to & threatenedByMinor) * 25000
                          : pt != PAWN ? bool(to & threatenedByPawn) * 15000
                                       : 0)
                       : 0;
        }

        else  // Type == EVASIONS
        {
            if (pos.capture_stage(m))
                m.value =
                  PieceValue[pos.piece_on(m.to_sq())] - type_of(pos.moved_piece(m)) + (1 << 28);
            else
                m.value = (*mainHistory)[pos.side_to_move()][m.from_to()]
                        + (*continuationHistory[0])[pos.moved_piece(m)][m.to_sq()]
                        + (*pawnHistory)[pawn_structure_index(pos)][pos.moved_piece(m)][m.to_sq()];
        }
}

// Returns the next move satisfying a predicate function.
// It never returns the TT move.
template<MovePicker::PickType T, typename Pred>
Move MovePicker::select(Pred filter) {

    while (cur < endMoves)
    {
        if constexpr (T == Best)
            std::swap(*cur, *std::max_element(cur, endMoves));

        if (*cur != ttMove && filter())
            return *cur++;

        cur++;
    }
    return Move::none();
}

// Most important method of the MovePicker class. It
// returns a new pseudo-legal move every time it is called until there are no more
// moves left, picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move(bool skipQuiets) {

    auto quiet_threshold = [](Depth d) { return -3330 * d; };

top:
    switch (stage)
    {

    case MAIN_TT :
    case EVASION_TT :
    case QSEARCH_TT :
    case PROBCUT_TT :
        ++stage;
        return ttMove;

    case CAPTURE_INIT :
    case PROBCUT_INIT :
    case QCAPTURE_INIT :
        cur = endBadCaptures = moves;
        endMoves             = generate<CAPTURES>(pos, cur);

        score<CAPTURES>();
        partial_insertion_sort(cur, endMoves, std::numeric_limits<int>::min());
        ++stage;
        goto top;

    case GOOD_CAPTURE :
        if (select<Next>([&]() {
                // Move losing capture to endBadCaptures to be tried later
                return pos.see_ge(*cur, -cur->value / 18) ? true
                                                          : (*endBadCaptures++ = *cur, false);
            }))
            return *(cur - 1);

        // Prepare the pointers to loop over the refutations array
        cur      = std::begin(refutations);
        endMoves = std::end(refutations);

        // If the countermove is the same as a killer, skip it
        if (refutations[0] == refutations[2] || refutations[1] == refutations[2])
            --endMoves;

        ++stage;
        [[fallthrough]];

    case REFUTATION :
        if (select<Next>([&]() {
                return *cur != Move::none() && !pos.capture_stage(*cur) && pos.pseudo_legal(*cur);
            }))
            return *(cur - 1);
        ++stage;
        [[fallthrough]];

    case QUIET_INIT :
        if (!skipQuiets)
        {
            cur      = endBadCaptures;
            endMoves = beginBadQuiets = endBadQuiets = generate<QUIETS>(pos, cur);

            score<QUIETS>();
            partial_insertion_sort(cur, endMoves, quiet_threshold(depth));
        }

        ++stage;
        [[fallthrough]];

    case GOOD_QUIET :
        if (!skipQuiets && select<Next>([&]() {
                return *cur != refutations[0] && *cur != refutations[1] && *cur != refutations[2];
            }))
        {
            if ((cur - 1)->value > -8000 || (cur - 1)->value <= quiet_threshold(depth))
                return *(cur - 1);

            // Remaining quiets are bad
            beginBadQuiets = cur - 1;
        }

        // Prepare the pointers to loop over the bad captures
        cur      = moves;
        endMoves = endBadCaptures;

        ++stage;
        [[fallthrough]];

    case BAD_CAPTURE :
        if (select<Next>([]() { return true; }))
            return *(cur - 1);

        // Prepare the pointers to loop over the bad quiets
        cur      = beginBadQuiets;
        endMoves = endBadQuiets;

        ++stage;
        [[fallthrough]];

    case BAD_QUIET :
        if (!skipQuiets)
            return select<Next>([&]() {
                return *cur != refutations[0] && *cur != refutations[1] && *cur != refutations[2];
            });

        return Move::none();

    case EVASION_INIT :
        cur      = moves;
        endMoves = generate<EVASIONS>(pos, cur);

        score<EVASIONS>();
        ++stage;
        [[fallthrough]];

    case EVASION :
        return select<Best>([]() { return true; });

    case PROBCUT :
        return select<Next>([&]() { return pos.see_ge(*cur, threshold); });

    case QCAPTURE :
        if (select<Next>([]() { return true; }))
            return *(cur - 1);

        // If we did not find any move and we do not try checks, we have finished
        if (depth != DEPTH_QS_CHECKS)
            return Move::none();

        ++stage;
        [[fallthrough]];

    case QCHECK_INIT :
        cur      = moves;
        endMoves = generate<QUIET_CHECKS>(pos, cur);

        ++stage;
        [[fallthrough]];

    case QCHECK :
        return select<Next>([]() { return true; });
    }

    assert(false);
    return Move::none();  // Silence warning
}

}  // namespace Stockfish
