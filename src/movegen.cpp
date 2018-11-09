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

#include "movegen.h"
#include "position.h"

namespace {

  template<Color Us, CastlingSide Cs, bool Checks, bool Chess960>
  ExtMove* generate_castling(const Position& pos, ExtMove* moveList) {

    constexpr CastlingRight Cr = Us | Cs;
    constexpr bool KingSide = (Cr == WHITE_OO || Cr == BLACK_OO);

    if (pos.castling_impeded(Cr) || !pos.can_castle(Cr))
        return moveList;

    // After castling, the rook and king final positions are the same in Chess960
    // as they would be in standard chess.
    Square kfrom = pos.square<KING>(Us);
    Square rfrom = pos.castling_rook_square(Cr);
    Square kto = relative_square(Us, KingSide ? SQ_G1 : SQ_C1);
    Bitboard enemies = pos.pieces(~Us);

    assert(!pos.checkers());

    const Direction step = Chess960 ? kto > kfrom ? WEST : EAST
                                    : KingSide    ? WEST : EAST;

    for (Square s = kto; s != kfrom; s += step)
        if (pos.attackers_to(s) & enemies)
            return moveList;

    // Because we generate only legal castling moves we need to verify that
    // when moving the castling rook we do not discover some hidden checker.
    // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
    if (Chess960 && (attacks_bb<ROOK>(kto, pos.pieces() ^ rfrom) & pos.pieces(~Us, ROOK, QUEEN)))
        return moveList;

    Move m = make<CASTLING>(kfrom, rfrom);

    if (Checks && !pos.gives_check(m))
        return moveList;

    *moveList++ = m;
    return moveList;
  }


  template<GenType Type, Direction D>
  ExtMove* make_promotions(ExtMove* moveList, Square to, Square ksq) {

    if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
        *moveList++ = make<PROMOTION>(to - D, to, QUEEN);

    if (Type == QUIETS || Type == EVASIONS || Type == NON_EVASIONS)
    {
        *moveList++ = make<PROMOTION>(to - D, to, ROOK);
        *moveList++ = make<PROMOTION>(to - D, to, BISHOP);
        *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
    }

    // Knight promotion is the only promotion that can give a direct check
    // that's not already included in the queen promotion.
    if (Type == QUIET_CHECKS && (PseudoAttacks[KNIGHT][to] & ksq))
        *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
    else
        (void)ksq; // Silence a warning under MSVC

    return moveList;
  }


  template<Color Us, GenType Type>
  ExtMove* generate_pawn_moves(const Position& pos, ExtMove* moveList, Bitboard target) {

    // Compute our parametrized parameters at compile time, named according to
    // the point of view of white side.
    constexpr Color     Them     = (Us == WHITE ? BLACK      : WHITE);
    constexpr Bitboard  TRank8BB = (Us == WHITE ? Rank8BB    : Rank1BB);
    constexpr Bitboard  TRank7BB = (Us == WHITE ? Rank7BB    : Rank2BB);
    constexpr Bitboard  TRank3BB = (Us == WHITE ? Rank3BB    : Rank6BB);
    constexpr Direction Up       = (Us == WHITE ? NORTH      : SOUTH);
    constexpr Direction UpRight  = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft   = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);

    Bitboard emptySquares;

    Bitboard pawnsOn7    = pos.pieces(Us, PAWN) &  TRank7BB;
    Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~TRank7BB;

    Bitboard enemies = (Type == EVASIONS ? pos.pieces(Them) & target:
                        Type == CAPTURES ? target : pos.pieces(Them));

    // Single and double pawn pushes, no promotions
    if (Type != CAPTURES)
    {
        emptySquares = (Type == QUIETS || Type == QUIET_CHECKS ? target : ~pos.pieces());

        Bitboard b1 = shift<Up>(pawnsNotOn7)   & emptySquares;
        Bitboard b2 = shift<Up>(b1 & TRank3BB) & emptySquares;

        if (Type == EVASIONS) // Consider only blocking squares
        {
            b1 &= target;
            b2 &= target;
        }

        if (Type == QUIET_CHECKS)
        {
            Square ksq = pos.square<KING>(Them);

            b1 &= pos.attacks_from<PAWN>(ksq, Them);
            b2 &= pos.attacks_from<PAWN>(ksq, Them);

            // Add pawn pushes which give discovered check. This is possible only
            // if the pawn is not on the same file as the enemy king, because we
            // don't generate captures. Note that a possible discovery check
            // promotion has been already generated amongst the captures.
            Bitboard dcCandidates = pos.blockers_for_king(Them);
            if (pawnsNotOn7 & dcCandidates)
            {
                Bitboard dc1 = shift<Up>(pawnsNotOn7 & dcCandidates) & emptySquares & ~file_bb(ksq);
                Bitboard dc2 = shift<Up>(dc1 & TRank3BB) & emptySquares;

                b1 |= dc1;
                b2 |= dc2;
            }
        }

        while (b1)
        {
            Square to = pop_lsb(&b1);
            *moveList++ = make_move(to - Up, to);
        }

        while (b2)
        {
            Square to = pop_lsb(&b2);
            *moveList++ = make_move(to - Up - Up, to);
        }
    }

    // Promotions and underpromotions
    if (pawnsOn7 && (Type != EVASIONS || (target & TRank8BB)))
    {
        if (Type == CAPTURES)
            emptySquares = ~pos.pieces();

        if (Type == EVASIONS)
            emptySquares &= target;

        Bitboard b1 = shift<UpRight>(pawnsOn7) & enemies;
        Bitboard b2 = shift<UpLeft >(pawnsOn7) & enemies;
        Bitboard b3 = shift<Up     >(pawnsOn7) & emptySquares;

        Square ksq = pos.square<KING>(Them);

        while (b1)
            moveList = make_promotions<Type, UpRight>(moveList, pop_lsb(&b1), ksq);

        while (b2)
            moveList = make_promotions<Type, UpLeft >(moveList, pop_lsb(&b2), ksq);

        while (b3)
            moveList = make_promotions<Type, Up     >(moveList, pop_lsb(&b3), ksq);
    }

    // Standard and en-passant captures
    if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
    {
        Bitboard b1 = shift<UpRight>(pawnsNotOn7) & enemies;
        Bitboard b2 = shift<UpLeft >(pawnsNotOn7) & enemies;

        while (b1)
        {
            Square to = pop_lsb(&b1);
            *moveList++ = make_move(to - UpRight, to);
        }

        while (b2)
        {
            Square to = pop_lsb(&b2);
            *moveList++ = make_move(to - UpLeft, to);
        }

        if (pos.ep_square() != SQ_NONE)
        {
            assert(rank_of(pos.ep_square()) == relative_rank(Us, RANK_6));

            // An en passant capture can be an evasion only if the checking piece
            // is the double pushed pawn and so is in the target. Otherwise this
            // is a discovery check and we are forced to do otherwise.
            if (Type == EVASIONS && !(target & (pos.ep_square() - Up)))
                return moveList;

            b1 = pawnsNotOn7 & pos.attacks_from<PAWN>(pos.ep_square(), Them);

            assert(b1);

            while (b1)
                *moveList++ = make<ENPASSANT>(pop_lsb(&b1), pos.ep_square());
        }
    }

    return moveList;
  }


  template<PieceType Pt, bool Checks>
  ExtMove* generate_moves(const Position& pos, ExtMove* moveList, Color us,
                          Bitboard target) {

    assert(Pt != KING && Pt != PAWN);

    const Square* pl = pos.squares<Pt>(us);

    for (Square from = *pl; from != SQ_NONE; from = *++pl)
    {
        if (Checks)
        {
            if (    (Pt == BISHOP || Pt == ROOK || Pt == QUEEN)
                && !(PseudoAttacks[Pt][from] & target & pos.check_squares(Pt)))
                continue;

            if (pos.blockers_for_king(~us) & from)
                continue;
        }

        Bitboard b = pos.attacks_from<Pt>(from) & target;

        if (Checks)
            b &= pos.check_squares(Pt);

        while (b)
            *moveList++ = make_move(from, pop_lsb(&b));
    }

    return moveList;
  }


  template<Color Us, GenType Type>
  ExtMove* generate_all(const Position& pos, ExtMove* moveList, Bitboard target) {

    constexpr bool Checks = Type == QUIET_CHECKS;

    moveList = generate_pawn_moves<Us, Type>(pos, moveList, target);
    moveList = generate_moves<KNIGHT, Checks>(pos, moveList, Us, target);
    moveList = generate_moves<BISHOP, Checks>(pos, moveList, Us, target);
    moveList = generate_moves<  ROOK, Checks>(pos, moveList, Us, target);
    moveList = generate_moves< QUEEN, Checks>(pos, moveList, Us, target);

    if (Type != QUIET_CHECKS && Type != EVASIONS)
    {
        Square ksq = pos.square<KING>(Us);
        Bitboard b = pos.attacks_from<KING>(ksq) & target;
        while (b)
            *moveList++ = make_move(ksq, pop_lsb(&b));
    }

    if (Type != CAPTURES && Type != EVASIONS && pos.can_castle(Us))
    {
        if (pos.is_chess960())
        {
            moveList = generate_castling<Us, KING_SIDE, Checks, true>(pos, moveList);
            moveList = generate_castling<Us, QUEEN_SIDE, Checks, true>(pos, moveList);
        }
        else
        {
            moveList = generate_castling<Us, KING_SIDE, Checks, false>(pos, moveList);
            moveList = generate_castling<Us, QUEEN_SIDE, Checks, false>(pos, moveList);
        }
    }

    return moveList;
  }

} // namespace


/// generate<CAPTURES> generates all pseudo-legal captures and queen
/// promotions. Returns a pointer to the end of the move list.
///
/// generate<QUIETS> generates all pseudo-legal non-captures and
/// underpromotions. Returns a pointer to the end of the move list.
///
/// generate<NON_EVASIONS> generates all pseudo-legal captures and
/// non-captures. Returns a pointer to the end of the move list.

template<GenType Type>
ExtMove* generate(const Position& pos, ExtMove* moveList) {

  assert(Type == CAPTURES || Type == QUIETS || Type == NON_EVASIONS);
  assert(!pos.checkers());

  Color us = pos.side_to_move();

  Bitboard target =  Type == CAPTURES     ?  pos.pieces(~us)
                   : Type == QUIETS       ? ~pos.pieces()
                   : Type == NON_EVASIONS ? ~pos.pieces(us) : 0;

  return us == WHITE ? generate_all<WHITE, Type>(pos, moveList, target)
                     : generate_all<BLACK, Type>(pos, moveList, target);
}

// Explicit template instantiations
template ExtMove* generate<CAPTURES>(const Position&, ExtMove*);
template ExtMove* generate<QUIETS>(const Position&, ExtMove*);
template ExtMove* generate<NON_EVASIONS>(const Position&, ExtMove*);


/// generate<QUIET_CHECKS> generates all pseudo-legal non-captures and knight
/// underpromotions that give check. Returns a pointer to the end of the move list.
template<>
ExtMove* generate<QUIET_CHECKS>(const Position& pos, ExtMove* moveList) {

  assert(!pos.checkers());

  Color us = pos.side_to_move();
  Bitboard dc = pos.blockers_for_king(~us) & pos.pieces(us);

  while (dc)
  {
     Square from = pop_lsb(&dc);
     PieceType pt = type_of(pos.piece_on(from));

     if (pt == PAWN)
         continue; // Will be generated together with direct checks

     Bitboard b = pos.attacks_from(pt, from) & ~pos.pieces();

     if (pt == KING)
         b &= ~PseudoAttacks[QUEEN][pos.square<KING>(~us)];

     while (b)
         *moveList++ = make_move(from, pop_lsb(&b));
  }

  return us == WHITE ? generate_all<WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                     : generate_all<BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
}


/// generate<EVASIONS> generates all pseudo-legal check evasions when the side
/// to move is in check. Returns a pointer to the end of the move list.
template<>
ExtMove* generate<EVASIONS>(const Position& pos, ExtMove* moveList) {

  assert(pos.checkers());

  Color us = pos.side_to_move();
  Square ksq = pos.square<KING>(us);
  Bitboard sliderAttacks = 0;
  Bitboard sliders = pos.checkers() & ~pos.pieces(KNIGHT, PAWN);

  // Find all the squares attacked by slider checkers. We will remove them from
  // the king evasions in order to skip known illegal moves, which avoids any
  // useless legality checks later on.
  while (sliders)
  {
      Square checksq = pop_lsb(&sliders);
      sliderAttacks |= LineBB[checksq][ksq] ^ checksq;
  }

  // Generate evasions for king, capture and non capture moves
  Bitboard b = pos.attacks_from<KING>(ksq) & ~pos.pieces(us) & ~sliderAttacks;
  while (b)
      *moveList++ = make_move(ksq, pop_lsb(&b));

  if (more_than_one(pos.checkers()))
      return moveList; // Double check, only a king move can save the day

  // Generate blocking evasions or captures of the checking piece
  Square checksq = lsb(pos.checkers());
  Bitboard target = between_bb(checksq, ksq) | checksq;

  return us == WHITE ? generate_all<WHITE, EVASIONS>(pos, moveList, target)
                     : generate_all<BLACK, EVASIONS>(pos, moveList, target);
}


/// generate<LEGAL> generates all the legal moves in the given position

template<>
ExtMove* generate<LEGAL>(const Position& pos, ExtMove* moveList) {

  Color us = pos.side_to_move();
  Bitboard pinned = pos.blockers_for_king(us) & pos.pieces(us);
  Square ksq = pos.square<KING>(us);
  ExtMove* cur = moveList;

  moveList = pos.checkers() ? generate<EVASIONS    >(pos, moveList)
                            : generate<NON_EVASIONS>(pos, moveList);
  while (cur != moveList)
      if (   (pinned || from_sq(*cur) == ksq || type_of(*cur) == ENPASSANT)
          && !pos.legal(*cur))
          *cur = (--moveList)->move;
      else
          ++cur;

  return moveList;
}
