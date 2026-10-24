/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

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

#include <cassert>
#include <limits>

#include "bitboard.h"
#include "misc.h"
#include "position.h"

namespace Stockfish {

enum Stages : i8 {
    TT,
    INIT_FIRST_STAGE,

    GOOD_CAPTURE,
    GOOD_QUIET,
    BAD_MOVES,

    QSEARCH,

    EVASION,

    PROBCUT,
};

namespace {

#ifdef USE_AVX512
// Load the Move, and the ExtMove value, into all lanes of 512-bit registers
static void splat_extmove(const ExtMove& m, __m512i& move, __m512i& value) {
    move  = _mm512_set1_epi32(m.raw());
    value = _mm512_set1_epi32(m.value);
}

// Sorts up to 16 moves.
struct MoveSorter {
    static constexpr int MAX_ELEMENTS = 16;
    __m512i              sortedValues, sortedMoves;

    explicit MoveSorter(const ExtMove& first) {
        splat_extmove(first, sortedMoves, sortedValues);

        // Set the uninitialized move values to INT_MIN, so that they sort less than any other move
        sortedValues = _mm512_mask_set1_epi32(sortedValues, ~1, std::numeric_limits<int>::min());
    }

    void insert(const ExtMove& m) {
        __m512i move, value;
        splat_extmove(m, move, value);

        // Mask of all elements except the insertion point
        assert(m.value != std::numeric_limits<int>::min());
        const u16 expand = _kadd_mask16(_mm512_cmplt_epi32_mask(sortedValues, value), -1);

        sortedValues = _mm512_mask_expand_epi32(value, expand, sortedValues);
        sortedMoves  = _mm512_mask_expand_epi32(move, expand, sortedMoves);
    }

    void write_sorted(ExtMove* moves, isize count) const {
        static_assert(sizeof(ExtMove) == 8);
        assert(count <= MAX_ELEMENTS);

        // Because values and moves are stored separately, we need to reassemble the ExtMoves
        auto write = [&](int offset, const __m512i indices) {
            const __m512i extMoves = _mm512_permutex2var_epi32(sortedMoves, indices, sortedValues);
            const isize   storeCount = count - offset;

            if (storeCount > 0)
                _mm512_mask_storeu_epi64(moves + offset, (1 << storeCount) - 1, extMoves);
        };

        write(0, _mm512_setr_epi32(0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23));
        write(8, _mm512_setr_epi32(8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31));
    }
};
#endif


// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void partial_insertion_sort(ExtMove* begin, ExtMove* end, int limit) {
    ExtMove *sortedEnd = begin, *p = begin + 1;

#ifdef USE_AVX512
    if (begin == end)
        return;

    MoveSorter sorter(*begin);
    for (; p < end; ++p)
    {
        if (p->value >= limit)
        {
            if (sortedEnd - begin + 1 >= MoveSorter::MAX_ELEMENTS)  // sorter full
                break;

            sorter.insert(*p);
            *p = *++sortedEnd;
        }
    }
    sorter.write_sorted(begin, sortedEnd - begin + 1);
    // Use scalar implementation for any remaining elements
#endif

    for (; p < end; ++p)
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
// to decide which class of moves to emit, to help sorting the (presumably)
// good moves first, and how important move ordering is at the current node.

// MovePicker constructor for the main search and for the quiescence search
MovePicker::MovePicker(const Position&              p,
                       Move                         ttm,
                       Depth                        d,
                       const ButterflyHistory*      mh,
                       const LowPlyHistory*         lph,
                       const CapturePieceToHistory* cph,
                       const PieceToHistory**       ch,
                       const SharedHistories*       sh,
                       int                          pl) :
    pos(p),
    mainHistory(mh),
    lowPlyHistory(lph),
    captureHistory(cph),
    continuationHistory(ch),
    sharedHistory(sh),
    ttMove(ttm),
    depth(d),
    ply(pl) {

    if (pos.checkers())
        first_stage = EVASION;
    else
        first_stage = depth > 0 ? GOOD_CAPTURE : QSEARCH;

    stage = ttm && pos.legal(ttm) ? TT : INIT_FIRST_STAGE;
}

// MovePicker constructor for ProbCut: we generate captures with Static Exchange
// Evaluation (SEE) greater than or equal to the given threshold.
MovePicker::MovePicker(const Position& p, Move ttm, int th, const CapturePieceToHistory* cph) :
    pos(p),
    captureHistory(cph),
    ttMove(ttm),
    threshold(th) {

    assert(!pos.checkers());

    first_stage = PROBCUT;
    stage       = ttm && pos.capture_stage(ttm) && pos.legal(ttm) ? TT : INIT_FIRST_STAGE;
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures are ordered by Most Valuable Victim (MVV), preferring captures
// with a good history. Quiet moves are ordered using the history tables.
template<GenType Type>
ExtMove* MovePicker::generate_and_score() {

    static_assert(Type == CAPTURES || Type == QUIETS || Type == EVASIONS, "Wrong type");

    Color us = pos.side_to_move();

    [[maybe_unused]] Bitboard threatByLesser[KING + 1];
    if constexpr (Type == QUIETS)
    {
        threatByLesser[PAWN]   = 0;
        threatByLesser[KNIGHT] = threatByLesser[BISHOP] = pos.threats_by(PAWN);
        threatByLesser[ROOK] =
          pos.threats_by(KNIGHT) | pos.threats_by(BISHOP) | threatByLesser[KNIGHT];
        threatByLesser[QUEEN] = pos.threats_by(ROOK) | threatByLesser[ROOK];
        threatByLesser[KING]  = 0;
    }

    MoveList<Type> ml(pos);  // move generation

    ExtMove* it = cur;
    for (auto move : ml)
    {
        ExtMove& m = *it++;
        m          = move;

        const Square    from          = m.from_sq();
        const Square    to            = m.to_sq();
        const Piece     pc            = pos.moved_piece(m);
        const PieceType pt            = type_of(pc);
        const Piece     capturedPiece = pos.piece_on(to);

        if constexpr (Type == CAPTURES)
            m.value = (*captureHistory)[pc][to][type_of(capturedPiece)]
                    + 7 * int(PieceValue[capturedPiece]);

        else if constexpr (Type == QUIETS)
        {
            // Use a local accumulator to eliminate redundant stores to m.value

            // histories
            int value = 2 * (*mainHistory)[us][m.raw()];
            value += 2 * sharedHistory->pawn_entry(pos)[pc][to];
            value += (*continuationHistory[0])[pc][to];
            value += (*continuationHistory[1])[pc][to];
            value += (*continuationHistory[2])[pc][to];
            value += (*continuationHistory[3])[pc][to];
            value += (*continuationHistory[5])[pc][to];

            // bonus for checks
            value += ((pos.check_squares(pt) & to) && pos.see_ge(m, -75)) * 16384;

            // penalty for moving to a square threatened by a lesser piece
            // or bonus for escaping an attack by a lesser piece.
            int v = 20 * (bool(threatByLesser[pt] & from) - bool(threatByLesser[pt] & to));
            value += PieceValue[pt] * v;


            if (ply < LOW_PLY_HISTORY_SIZE)
                value += 8 * (*lowPlyHistory)[ply][m.raw()] / (1 + ply);

            m.value = value;
        }

        else  // Type == EVASIONS
        {
            if (pos.capture_stage(m))
                m.value = PieceValue[capturedPiece] + (1 << 28);
            else
                m.value = (*mainHistory)[us][m.raw()] + (*continuationHistory[0])[pc][to];
        }
    }
    return it;
}

// Returns the next move satisfying a predicate function.
// This never returns the TT move, as it was emitted before.
template<typename Pred>
Move MovePicker::select(Pred filter) {

    for (; cur < endCur; ++cur)
        if (*cur != ttMove && filter())
            return *cur++;

    return Move::none();
}

// This is the most important method of the MovePicker class. We emit one
// new legal move on every call until there are no more moves left,
// picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move() {

    constexpr int goodQuietThreshold = -14000;
dispatch:
    switch (stage)
    {

    case TT :
        stage = INIT_FIRST_STAGE;
        return ttMove;

    case INIT_FIRST_STAGE :
        cur = endBadMoves = moves;

        if (first_stage == EVASION)
            endCur = generate_and_score<EVASIONS>();
        else
            endCur = generate_and_score<CAPTURES>();

        partial_insertion_sort(cur, endCur, std::numeric_limits<int>::min());

        stage = first_stage;
        goto dispatch;

    case GOOD_CAPTURE :
        if (select([&]() {
                if (pos.see_ge(*cur, -cur->value / 18))
                    return true;
                *endBadMoves++ = *cur;
                return false;
            }))
            return *(cur - 1);

        endBadCaptures = endBadMoves;

        if (!skipQuiets)
        {
            // initialize quiet moves
            cur    = endBadMoves;
            endCur = generate_and_score<QUIETS>();

            partial_insertion_sort(cur, endCur, -3560 * depth);

            stage = GOOD_QUIET;
            [[fallthrough]];

        case GOOD_QUIET :
            if (select([&]() {
                    if (cur->value > goodQuietThreshold)
                        return true;
                    *endBadMoves++ = *cur;
                    return false;
                }))
                return *(cur - 1);
        }

        // Prepare the pointers to loop over the bad captures
        cur    = moves;
        endCur = endBadMoves;

        stage = BAD_MOVES;
        [[fallthrough]];

    case BAD_MOVES :
    case QSEARCH :
    case EVASION :
        return select([]() { return true; });

    case PROBCUT :
        return select([&]() { return pos.see_ge(*cur, threshold); });

    default :
        assert(false);
        sf_unreachable();
    }
}

void MovePicker::skip_quiet_moves() {
    skipQuiets = true;
    if (stage == BAD_MOVES || stage == GOOD_QUIET)
    {
        endCur      = endBadCaptures;  // discards any quiet moves in the current stage
        endBadMoves = endBadCaptures;  // discards bad quiet moves
    }
}

}  // namespace Stockfish
