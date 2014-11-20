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

#include "movegen.h"
#include "position.h"

namespace {

  template<CastlingRight CastlingRight, bool Checks, bool Chess960>
  ExtMove* generate_castling(const Position& pos, ExtMove* moveList, Color us, const CheckInfo* checkInfo) {

    static const bool KingSide = (CastlingRight == WHITE_OO || CastlingRight == BLACK_OO);

    if (pos.castling_impeded(CastlingRight) || !pos.can_castle(CastlingRight))
        return moveList;

    // After castling, the rook and king final positions are the same in Chess960
    // as they would be in standard chess.
    Square kingFrom = pos.king_square(us);
    Square rookFrom = pos.castling_rook_square(CastlingRight);
    Square kingTo = relative_square(us, KingSide ? SQ_G1 : SQ_C1);
    Bitboard enemies = pos.pieces(~us);

    assert(!pos.checkers());

    const Square K = Chess960 ? kingTo > kingFrom ? DELTA_W : DELTA_E
                              : KingSide          ? DELTA_W : DELTA_E;

    for (Square square = kingTo; square != kingFrom; square += K)
        if (pos.attackers_to(square) & enemies)
            return moveList;

    // Because we generate only legal castling moves we need to verify that
    // when moving the castling rook we do not discover some hidden checker.
    // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
    if (Chess960 && (attacks_bb<ROOK>(kingTo, pos.pieces() ^ rookFrom) & pos.pieces(~us, ROOK, QUEEN)))
        return moveList;

    Move move = make<CASTLING>(kingFrom, rookFrom);

    if (Checks && !pos.gives_check(move, *checkInfo))
        return moveList;

    (moveList++)->move = move;

    return moveList;
  }


  template<GenType Type, Square Delta>
  inline ExtMove* generate_promotions(ExtMove* moveList, Bitboard pawnsOn7,
                                      Bitboard target, const CheckInfo* checkInfo) {

    Bitboard bitboard = shift_bb<Delta>(pawnsOn7) & target;

    while (bitboard)
    {
        Square to = pop_lsb(&bitboard);

        if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
            (moveList++)->move = make<PROMOTION>(to - Delta, to, QUEEN);

        if (Type == QUIETS || Type == EVASIONS || Type == NON_EVASIONS)
        {
            (moveList++)->move = make<PROMOTION>(to - Delta, to, ROOK);
            (moveList++)->move = make<PROMOTION>(to - Delta, to, BISHOP);
            (moveList++)->move = make<PROMOTION>(to - Delta, to, KNIGHT);
        }

        // Knight promotion is the only promotion that can give a direct check
        // that's not already included in the queen promotion.
        if (Type == QUIET_CHECKS && (StepAttacksBB[W_KNIGHT][to] & checkInfo->kingSquare))
            (moveList++)->move = make<PROMOTION>(to - Delta, to, KNIGHT);
        else
            (void)checkInfo; // Silence a warning under MSVC
    }

    return moveList;
  }


  template<Color Us, GenType Type>
  ExtMove* generate_pawn_moves(const Position& pos, ExtMove* moveList,
                               Bitboard target, const CheckInfo* checkInfo) {

    // Compute our parametrized parameters at compile time, named according to
    // the point of view of white side.
    const Color    Them         = (Us == WHITE ? BLACK    : WHITE);
    const Bitboard WhiteRank8BB = (Us == WHITE ? Rank8BB  : Rank1BB);
    const Bitboard WhiteRank7BB = (Us == WHITE ? Rank7BB  : Rank2BB);
    const Bitboard WhiteRank3BB = (Us == WHITE ? Rank3BB  : Rank6BB);
    const Square   Up           = (Us == WHITE ? DELTA_N  : DELTA_S);
    const Square   Right        = (Us == WHITE ? DELTA_NE : DELTA_SW);
    const Square   Left         = (Us == WHITE ? DELTA_NW : DELTA_SE);

    Bitboard bitboard1, bitboard2, discoveredCheck1, discoveredCheck2, emptySquares;

    Bitboard pawnsOn7    = pos.pieces(Us, PAWN) &  WhiteRank7BB;
    Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~WhiteRank7BB;

    Bitboard enemies = (Type == EVASIONS ? pos.pieces(Them) & target:
                        Type == CAPTURES ? target : pos.pieces(Them));

    // Single and double pawn pushes, no promotions
    if (Type != CAPTURES)
    {
        emptySquares = (Type == QUIETS || Type == QUIET_CHECKS ? target : ~pos.pieces());

        bitboard1 = shift_bb<Up>(pawnsNotOn7)   & emptySquares;
        bitboard2 = shift_bb<Up>(bitboard1 & WhiteRank3BB) & emptySquares;

        if (Type == EVASIONS) // Consider only blocking squares
        {
            bitboard1 &= target;
            bitboard2 &= target;
        }

        if (Type == QUIET_CHECKS)
        {
            bitboard1 &= pos.attacks_from<PAWN>(checkInfo->kingSquare, Them);
            bitboard2 &= pos.attacks_from<PAWN>(checkInfo->kingSquare, Them);

            // Add pawn pushes which give discovered check. This is possible only
            // if the pawn is not on the same file as the enemy king, because we
            // don't generate captures. Note that a possible discovery check
            // promotion has been already generated amongst the captures.
            if (pawnsNotOn7 & checkInfo->discoveredCheckCandidates)
            {
                discoveredCheck1 = shift_bb<Up>(pawnsNotOn7 & checkInfo->discoveredCheckCandidates) & emptySquares & ~file_bb(checkInfo->kingSquare);
                discoveredCheck2 = shift_bb<Up>(discoveredCheck1 & WhiteRank3BB) & emptySquares;

                bitboard1 |= discoveredCheck1;
                bitboard2 |= discoveredCheck2;
            }
        }

        while (bitboard1)
        {
            Square to = pop_lsb(&bitboard1);
            (moveList++)->move = make_move(to - Up, to);
        }

        while (bitboard2)
        {
            Square to = pop_lsb(&bitboard2);
            (moveList++)->move = make_move(to - Up - Up, to);
        }
    }

    // Promotions and underpromotions
    if (pawnsOn7 && (Type != EVASIONS || (target & WhiteRank8BB)))
    {
        if (Type == CAPTURES)
            emptySquares = ~pos.pieces();

        if (Type == EVASIONS)
            emptySquares &= target;

        moveList = generate_promotions<Type, Right>(moveList, pawnsOn7, enemies, checkInfo);
        moveList = generate_promotions<Type, Left >(moveList, pawnsOn7, enemies, checkInfo);
        moveList = generate_promotions<Type, Up>(moveList, pawnsOn7, emptySquares, checkInfo);
    }

    // Standard and en-passant captures
    if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
    {
        bitboard1 = shift_bb<Right>(pawnsNotOn7) & enemies;
        bitboard2 = shift_bb<Left >(pawnsNotOn7) & enemies;

        while (bitboard1)
        {
            Square to = pop_lsb(&bitboard1);
            (moveList++)->move = make_move(to - Right, to);
        }

        while (bitboard2)
        {
            Square to = pop_lsb(&bitboard2);
            (moveList++)->move = make_move(to - Left, to);
        }

        if (pos.ep_square() != SQ_NONE)
        {
            assert(rank_of(pos.ep_square()) == relative_rank(Us, RANK_6));

            // An en passant capture can be an evasion only if the checking piece
            // is the double pushed pawn and so is in the target. Otherwise this
            // is a discovery check and we are forced to do otherwise.
            if (Type == EVASIONS && !(target & (pos.ep_square() - Up)))
                return moveList;

            bitboard1 = pawnsNotOn7 & pos.attacks_from<PAWN>(pos.ep_square(), Them);

            assert(bitboard1);

            while (bitboard1)
                (moveList++)->move = make<ENPASSANT>(pop_lsb(&bitboard1), pos.ep_square());
        }
    }

    return moveList;
  }


  template<PieceType Pt, bool Checks> FORCE_INLINE
  ExtMove* generate_moves(const Position& pos, ExtMove* moveList, Color us,
                          Bitboard target, const CheckInfo* checkInfo) {

    assert(Pt != KING && Pt != PAWN);

    const Square* pieceList = pos.list<Pt>(us);

    for (Square from = *pieceList; from != SQ_NONE; from = *++pieceList)
    {
        if (Checks)
        {
            if (    (Pt == BISHOP || Pt == ROOK || Pt == QUEEN)
                && !(PseudoAttacks[Pt][from] & target & checkInfo->checkSquares[Pt]))
                continue;

            if (unlikely(checkInfo->discoveredCheckCandidates) && (checkInfo->discoveredCheckCandidates & from))
                continue;
        }

        Bitboard bitboard = pos.attacks_from<Pt>(from) & target;

        if (Checks)
            bitboard &= checkInfo->checkSquares[Pt];

        while (bitboard)
            (moveList++)->move = make_move(from, pop_lsb(&bitboard));
    }

    return moveList;
  }


  template<Color Us, GenType Type> FORCE_INLINE
  ExtMove* generate_all(const Position& pos, ExtMove* moveList, Bitboard target,
                        const CheckInfo* checkInfo = NULL) {

    const bool Checks = Type == QUIET_CHECKS;

    moveList = generate_pawn_moves<Us, Type>(pos, moveList, target, checkInfo);
    moveList = generate_moves<KNIGHT, Checks>(pos, moveList, Us, target, checkInfo);
    moveList = generate_moves<BISHOP, Checks>(pos, moveList, Us, target, checkInfo);
    moveList = generate_moves<  ROOK, Checks>(pos, moveList, Us, target, checkInfo);
    moveList = generate_moves< QUEEN, Checks>(pos, moveList, Us, target, checkInfo);

    if (Type != QUIET_CHECKS && Type != EVASIONS)
    {
        Square kingSquare = pos.king_square(Us);
        Bitboard bitboard = pos.attacks_from<KING>(kingSquare) & target;
        while (bitboard)
            (moveList++)->move = make_move(kingSquare, pop_lsb(&bitboard));
    }

    if (Type != CAPTURES && Type != EVASIONS && pos.can_castle(Us))
    {
        if (pos.is_chess960())
        {
            moveList = generate_castling<MakeCastling<Us,  KING_SIDE>::right, Checks, true>(pos, moveList, Us, checkInfo);
            moveList = generate_castling<MakeCastling<Us, QUEEN_SIDE>::right, Checks, true>(pos, moveList, Us, checkInfo);
        }
        else
        {
            moveList = generate_castling<MakeCastling<Us,  KING_SIDE>::right, Checks, false>(pos, moveList, Us, checkInfo);
            moveList = generate_castling<MakeCastling<Us, QUEEN_SIDE>::right, Checks, false>(pos, moveList, Us, checkInfo);
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

  Bitboard target = Type == CAPTURES     ?  pos.pieces(~us)
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
  CheckInfo checkInfo(pos);
  Bitboard discoveredCheck = checkInfo.discoveredCheckCandidates;

  while (discoveredCheck)
  {
     Square from = pop_lsb(&discoveredCheck);
     PieceType pieceType = type_of(pos.piece_on(from));

     if (pieceType == PAWN)
         continue; // Will be generated together with direct checks

     Bitboard bitboard = pos.attacks_from(Piece(pieceType), from) & ~pos.pieces();

     if (pieceType == KING)
         bitboard &= ~PseudoAttacks[QUEEN][checkInfo.kingSquare];

     while (bitboard)
         (moveList++)->move = make_move(from, pop_lsb(&bitboard));
  }

  return us == WHITE ? generate_all<WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces(), &checkInfo)
                     : generate_all<BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces(), &checkInfo);
}


/// generate<EVASIONS> generates all pseudo-legal check evasions when the side
/// to move is in check. Returns a pointer to the end of the move list.
template<>
ExtMove* generate<EVASIONS>(const Position& pos, ExtMove* moveList) {

  assert(pos.checkers());

  Color us = pos.side_to_move();
  Square kingSquare = pos.king_square(us);
  Bitboard sliderAttacks = 0;
  Bitboard sliders = pos.checkers() & ~pos.pieces(KNIGHT, PAWN);

  // Find all the squares attacked by slider checkers. We will remove them from
  // the king evasions in order to skip known illegal moves, which avoids any
  // useless legality checks later on.
  while (sliders)
  {
      Square checkSquares = pop_lsb(&sliders);
      sliderAttacks |= LineBB[checkSquares][kingSquare] ^ checkSquares;
  }

  // Generate evasions for king, capture and non capture moves
  Bitboard bitboard = pos.attacks_from<KING>(kingSquare) & ~pos.pieces(us) & ~sliderAttacks;
  while (bitboard)
      (moveList++)->move = make_move(kingSquare, pop_lsb(&bitboard));

  if (more_than_one(pos.checkers()))
      return moveList; // Double check, only a king move can save the day

  // Generate blocking evasions or captures of the checking piece
  Square checkSquares = lsb(pos.checkers());
  Bitboard target = between_bb(checkSquares, kingSquare) | checkSquares;

  return us == WHITE ? generate_all<WHITE, EVASIONS>(pos, moveList, target)
                     : generate_all<BLACK, EVASIONS>(pos, moveList, target);
}


/// generate<LEGAL> generates all the legal moves in the given position

template<>
ExtMove* generate<LEGAL>(const Position& pos, ExtMove* moveList) {

  ExtMove *end, *current = moveList;
  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());
  Square kingSquare = pos.king_square(pos.side_to_move());

  end = pos.checkers() ? generate<EVASIONS>(pos, moveList)
                       : generate<NON_EVASIONS>(pos, moveList);
  while (current != end)
      if (   (pinned || from_sq(current->move) == kingSquare || type_of(current->move) == ENPASSANT)
          && !pos.legal(current->move, pinned))
          current->move = (--end)->move;
      else
          ++current;

  return end;
}
