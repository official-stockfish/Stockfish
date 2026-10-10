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

#include "movegen.h"

#include <cassert>
#include <initializer_list>

#include "attacks.h"
#include "bitboard.h"
#include "position.h"

#if defined(USE_AVX512ICL)
    #include <array>
    #include <algorithm>
    #include <immintrin.h>
#endif

namespace Stockfish {

namespace {

#if defined(USE_AVX512ICL)

template<Direction offset>
inline Move* splat_pawn_moves(Move* moveList, Bitboard to_bb) {
    assert(popcount(to_bb) <= 8);  // <= 8 pawns per side

    const __m128i toSquares =
      _mm_cvtepi8_epi16(_mm512_castsi512_si128(_mm512_maskz_compress_epi8(to_bb, AllSquares)));
    const __m128i fromSquares = _mm_subs_epi16(toSquares, _mm_set1_epi16(offset));
    const __m128i moves       = _mm_or_si128(_mm_slli_epi16(fromSquares, Move::FromSqShift),
                                             _mm_slli_epi16(toSquares, Move::ToSqShift));

    _mm_storeu_si128(reinterpret_cast<__m128i*>(moveList), moves);
    return moveList + popcount(to_bb);
}

inline Move* splat_moves(Move* moveList, Square from, Bitboard to_bb) {
    assert(popcount(to_bb) <= 32);  // Q can attack up to 27 squares

    const __m512i fromVec = _mm512_set1_epi16(Move(from, SQUARE_ZERO).raw());
    const __m512i toSquares =
      _mm512_cvtepi8_epi16(_mm512_castsi512_si256(_mm512_maskz_compress_epi8(to_bb, AllSquares)));
    const __m512i moves = _mm512_or_si512(fromVec, _mm512_slli_epi16(toSquares, Move::ToSqShift));

    _mm512_storeu_si512(moveList, moves);
    return moveList + popcount(to_bb);
}

#else

template<Direction offset>
inline Move* splat_pawn_moves(Move* moveList, Bitboard to_bb) {
    while (to_bb)
    {
        Square to   = pop_lsb(to_bb);
        *moveList++ = Move(to - offset, to);
    }
    return moveList;
}

inline Move* splat_moves(Move* moveList, Square from, Bitboard to_bb) {
    while (to_bb)
        *moveList++ = Move(from, pop_lsb(to_bb));
    return moveList;
}

#endif

template<GenType Type, Direction D, bool Enemy>
Move* make_promotions(Move* moveList, [[maybe_unused]] Square to) {

    constexpr bool          all  = Type == EVASIONS || Type == ALL;
    [[maybe_unused]] Square from = to - D;

    if constexpr (Type == CAPTURES || all)
        *moveList++ = Move::make<PROMOTION>(from, to, QUEEN);

    if constexpr ((Type == CAPTURES && Enemy) || (Type == QUIETS && !Enemy) || all)
    {
        *moveList++ = Move::make<PROMOTION>(from, to, ROOK);
        *moveList++ = Move::make<PROMOTION>(from, to, BISHOP);
        *moveList++ = Move::make<PROMOTION>(from, to, KNIGHT);
    }

    return moveList;
}


template<Color Us, GenType Type>
Move* generate_pawn_moves(const Position& pos, Move* moveList, Bitboard target) {

    static_assert(Type != ALL, "Unsupported type in generate_pawn_moves()");

    constexpr Color     Them     = ~Us;
    constexpr Bitboard  TRank7BB = (Us == WHITE ? Rank7BB : Rank2BB);
    constexpr Bitboard  TRank3BB = (Us == WHITE ? Rank3BB : Rank6BB);
    constexpr Direction Up       = pawn_push(Us);
    constexpr Direction UpRight  = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft   = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);

    const Bitboard emptySquares = ~pos.pieces();
    const Bitboard enemies      = Type == EVASIONS ? pos.checkers() : pos.pieces(Them);
    const Bitboard pinned = pos.blockers_for_king(Us);
    const Square ksq = pos.square<KING>(Us);

    const Bitboard pawnsOn7    = pos.pieces(Us, PAWN) & TRank7BB;
    const Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~TRank7BB;
    const Bitboard pushable        = ~pinned | file_bb(ksq);
    const Bitboard canCaptureLeft  = ~pinned | Attacks::antidiag_bb(ksq);
    const Bitboard canCaptureRight = ~pinned | Attacks::diag_bb(ksq);

    // Single and double pawn pushes, no promotions
    if constexpr (Type != CAPTURES)
    {
        Bitboard b1 = shift(pawnsNotOn7 & pushable, Up) & emptySquares;
        Bitboard b2 = shift(b1 & TRank3BB, Up) & emptySquares;

        if constexpr (Type == EVASIONS)  // Consider only blocking squares
        {
            b1 &= target;
            b2 &= target;
        }

        moveList = splat_pawn_moves<Up>(moveList, b1);
        moveList = splat_pawn_moves<Up + Up>(moveList, b2);
    }

    // Promotions and underpromotions
    if (pawnsOn7)
    {
        Bitboard b1 = shift(pawnsOn7 & canCaptureRight, UpRight) & enemies;
        Bitboard b2 = shift(pawnsOn7 & canCaptureLeft, UpLeft) & enemies;
        Bitboard b3 = shift(pawnsOn7 & pushable, Up) & emptySquares;

        if constexpr (Type == EVASIONS)
            b3 &= target;

        while (b1)
            moveList = make_promotions<Type, UpRight, true>(moveList, pop_lsb(b1));

        while (b2)
            moveList = make_promotions<Type, UpLeft, true>(moveList, pop_lsb(b2));

        while (b3)
            moveList = make_promotions<Type, Up, false>(moveList, pop_lsb(b3));
    }

    // Standard and en passant captures
    if constexpr (Type == CAPTURES || Type == EVASIONS)
    {
        Bitboard b1 = shift(pawnsNotOn7 & canCaptureRight, UpRight) & enemies;
        Bitboard b2 = shift(pawnsNotOn7 & canCaptureLeft, UpLeft) & enemies;

        moveList = splat_pawn_moves<UpRight>(moveList, b1);
        moveList = splat_pawn_moves<UpLeft>(moveList, b2);

        const Square epSq = pos.ep_square();

        if (epSq != SQ_NONE
            // An en passant capture cannot resolve a discovered check
            && !(Type == EVASIONS && (target & (epSq + Up))) )
        {
            assert(rank_of(epSq) == relative_rank(Us, RANK_6));

            b1 =  pawnsNotOn7 & ( (canCaptureRight & shift(square_bb(epSq), -UpRight))
                                | (canCaptureLeft  & shift(square_bb(epSq), -UpLeft )) );
            assert(b1);

            while (b1)
                *moveList++ = Move::make<EN_PASSANT>(pop_lsb(b1), epSq);
        }
    }

    return moveList;
}


template<PieceType Pt>
Move* generate_moves(const Position& pos, Move* moveList, Bitboard target, Color us) {

    static_assert(Pt != KING && Pt != PAWN, "Unsupported piece type in generate_moves()");

    const Square   ksq    = pos.square<KING>(us);
    const Bitboard pinned = pos.blockers_for_king(us);

    Bitboard bb = pos.pieces(us, Pt) & ~pinned;
    while (bb)
    {
        Square   from = pop_lsb(bb);
        Bitboard b    = Attacks::attacks_bb(Pt, from, pos.pieces()) & target;

        moveList = splat_moves(moveList, from, b);
    }

    if constexpr (Pt != KNIGHT) // pinned knights cannot move
    {
        bb = pos.pieces(us, Pt) & pinned;
        while (bb)
        {
            Square from = pop_lsb(bb);
            Bitboard b  = Attacks::attacks_bb(Pt, from, pos.pieces()) & target;
            b &= Attacks::line_bb(ksq, from);

            moveList = splat_moves(moveList, from, b);
        }
    }

    return moveList;
}


template<GenType Type>
Move* generate_all(const Position& pos, Move* moveList, Color us) {

    static_assert(Type != ALL, "Unsupported type in generate_all()");

    const Square ksq = pos.square<KING>(us);
    Bitboard     target;

    // Skip generating non-king moves when in double check
    if (Type != EVASIONS || !more_than_one(pos.checkers()))
    {
        target = Type == EVASIONS     ? Attacks::between_bb(ksq, lsb(pos.checkers()))
               : Type == CAPTURES     ? pos.pieces(~us)
                                      : ~pos.pieces();  // QUIETS

        moveList = us == WHITE ? generate_pawn_moves<WHITE, Type>(pos, moveList, target)
                               : generate_pawn_moves<BLACK, Type>(pos, moveList, target);
        moveList = generate_moves<KNIGHT>(pos, moveList, target, us);
        moveList = generate_moves<BISHOP>(pos, moveList, target, us);
        moveList = generate_moves<ROOK>(pos, moveList, target, us);
        moveList = generate_moves<QUEEN>(pos, moveList, target, us);
    }

    if constexpr (Type == EVASIONS)
        target = ~pos.pieces(us);

    target &= ~pos.threats_by(ALL_PIECES);

    Bitboard b = Attacks::attacks_bb(KING, ksq) & target;
    moveList = splat_moves(moveList, ksq, b);

    if (Type == QUIETS && pos.can_castle(us & ANY_CASTLING)) {
        for (CastlingRights cr : {us & KING_SIDE, us & QUEEN_SIDE}) {
            Square rookSquare = pos.castling_rook_square(cr);
            Square to = relative_square(us, rookSquare > ksq ? SQ_G1 : SQ_C1);

            if (!pos.castling_impeded(cr) && pos.can_castle(cr)
                && !(Attacks::between_bb(ksq, to) & pos.threats_by(ALL_PIECES))
                && !(pos.blockers_for_king(us) & rookSquare))
                *moveList++ = Move::make<CASTLING>(ksq, rookSquare);
        }
    }

    return moveList;
}

}  // namespace


// <CAPTURES>     Generates all legal captures plus queen promotions
// <QUIETS>       Generates all legal non-captures and underpromotions
// <EVASIONS>     Generates all legal check evasions
//
// Returns a pointer to the end of the move list.
template<GenType Type>
Move* generate(const Position& pos, Move* moveList) {

    static_assert(Type != ALL, "Unsupported type in generate()");
    assert((Type == EVASIONS) == bool(pos.checkers()));

    Color us = pos.side_to_move();

    return generate_all<Type>(pos, moveList, us);
}

// Explicit template instantiations
template Move* generate<CAPTURES>(const Position&, Move*);
template Move* generate<QUIETS>(const Position&, Move*);
template Move* generate<EVASIONS>(const Position&, Move*);

// generate<ALL> generates all the legal moves in the given position

template<>
Move* generate<ALL>(const Position& pos, Move* moveList) {
    return pos.checkers() ? generate<EVASIONS>(pos, moveList)
                          : generate<QUIETS>(pos, generate<CAPTURES>(pos, moveList));
}

}  // namespace Stockfish
