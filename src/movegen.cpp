/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <algorithm>
#include <cassert>

#include "movegen.h"
#include "position.h"

/// Simple macro to wrap a very common while loop, no facny, no flexibility,
/// hardcoded names 'mlist' and 'from'.
#define SERIALIZE(b) while (b) (*mlist++).move = make_move(from, pop_lsb(&b))

/// Version used for pawns, where the 'from' square is given as a delta from the 'to' square
#define SERIALIZE_PAWNS(b, d) while (b) { Square to = pop_lsb(&b); \
                                         (*mlist++).move = make_move(to - (d), to); }
namespace {

  template<CastlingSide Side, bool OnlyChecks>
  MoveStack* generate_castle(const Position& pos, MoveStack* mlist, Color us) {

    if (pos.castle_impeded(us, Side) || !pos.can_castle(make_castle_right(us, Side)))
        return mlist;

    // After castling, the rook and king final positions are the same in Chess960
    // as they would be in standard chess.
    Square kfrom = pos.king_square(us);
    Square rfrom = pos.castle_rook_square(us, Side);
    Square kto = relative_square(us, Side == KING_SIDE ? SQ_G1 : SQ_C1);
    Bitboard enemies = pos.pieces(~us);

    assert(!pos.in_check());

    for (Square s = std::min(kfrom, kto), e = std::max(kfrom, kto); s <= e; s++)
        if (    s != kfrom // We are not in check
            && (pos.attackers_to(s) & enemies))
            return mlist;

    // Because we generate only legal castling moves we need to verify that
    // when moving the castling rook we do not discover some hidden checker.
    // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
    if (    pos.is_chess960()
        && (pos.attackers_to(kto, pos.pieces() ^ rfrom) & enemies))
            return mlist;

    (*mlist++).move = make<CASTLE>(kfrom, rfrom);

    if (OnlyChecks && !pos.move_gives_check((mlist - 1)->move, CheckInfo(pos)))
        mlist--;

    return mlist;
  }


  template<Square Delta>
  inline Bitboard move_pawns(Bitboard p) {

    return  Delta == DELTA_N  ?  p << 8
          : Delta == DELTA_S  ?  p >> 8
          : Delta == DELTA_NE ? (p & ~FileHBB) << 9
          : Delta == DELTA_SE ? (p & ~FileHBB) >> 7
          : Delta == DELTA_NW ? (p & ~FileABB) << 7
          : Delta == DELTA_SW ? (p & ~FileABB) >> 9 : 0;
  }


  template<GenType Type, Square Delta>
  inline MoveStack* generate_promotions(MoveStack* mlist, Bitboard pawnsOn7, Bitboard target, Square ksq) {

    Bitboard b = move_pawns<Delta>(pawnsOn7) & target;

    while (b)
    {
        Square to = pop_lsb(&b);

        if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
            (*mlist++).move = make<PROMOTION>(to - Delta, to, QUEEN);

        if (Type == QUIETS || Type == EVASIONS || Type == NON_EVASIONS)
        {
            (*mlist++).move = make<PROMOTION>(to - Delta, to, ROOK);
            (*mlist++).move = make<PROMOTION>(to - Delta, to, BISHOP);
            (*mlist++).move = make<PROMOTION>(to - Delta, to, KNIGHT);
        }

        // Knight-promotion is the only one that can give a direct check not
        // already included in the queen-promotion.
        if (Type == QUIET_CHECKS && (StepAttacksBB[W_KNIGHT][to] & ksq))
            (*mlist++).move = make<PROMOTION>(to - Delta, to, KNIGHT);
        else
            (void)ksq; // Silence a warning under MSVC
    }

    return mlist;
  }


  template<Color Us, GenType Type>
  MoveStack* generate_pawn_moves(const Position& pos, MoveStack* mlist, Bitboard target, Square ksq = SQ_NONE) {

    // Compute our parametrized parameters at compile time, named according to
    // the point of view of white side.
    const Color    Them     = (Us == WHITE ? BLACK    : WHITE);
    const Bitboard TRank8BB = (Us == WHITE ? Rank8BB  : Rank1BB);
    const Bitboard TRank7BB = (Us == WHITE ? Rank7BB  : Rank2BB);
    const Bitboard TRank3BB = (Us == WHITE ? Rank3BB  : Rank6BB);
    const Square   UP       = (Us == WHITE ? DELTA_N  : DELTA_S);
    const Square   RIGHT    = (Us == WHITE ? DELTA_NE : DELTA_SW);
    const Square   LEFT     = (Us == WHITE ? DELTA_NW : DELTA_SE);

    Bitboard b1, b2, dc1, dc2, emptySquares;

    Bitboard pawnsOn7    = pos.pieces(Us, PAWN) &  TRank7BB;
    Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~TRank7BB;

    Bitboard enemies = (Type == EVASIONS ? pos.pieces(Them) & target:
                        Type == CAPTURES ? target : pos.pieces(Them));

    // Single and double pawn pushes, no promotions
    if (Type != CAPTURES)
    {
        emptySquares = (Type == QUIETS ? target : ~pos.pieces());

        b1 = move_pawns<UP>(pawnsNotOn7)   & emptySquares;
        b2 = move_pawns<UP>(b1 & TRank3BB) & emptySquares;

        if (Type == EVASIONS) // Consider only blocking squares
        {
            b1 &= target;
            b2 &= target;
        }

        if (Type == QUIET_CHECKS)
        {
            b1 &= pos.attacks_from<PAWN>(ksq, Them);
            b2 &= pos.attacks_from<PAWN>(ksq, Them);

            // Add pawn pushes which give discovered check. This is possible only
            // if the pawn is not on the same file as the enemy king, because we
            // don't generate captures. Note that a possible discovery check
            // promotion has been already generated among captures.
            if (pawnsNotOn7 & target) // Target is dc bitboard
            {
                dc1 = move_pawns<UP>(pawnsNotOn7 & target) & emptySquares & ~file_bb(ksq);
                dc2 = move_pawns<UP>(dc1 & TRank3BB) & emptySquares;

                b1 |= dc1;
                b2 |= dc2;
            }
        }

        SERIALIZE_PAWNS(b1, UP);
        SERIALIZE_PAWNS(b2, UP + UP);
    }

    // Promotions and underpromotions
    if (pawnsOn7 && (Type != EVASIONS || (target & TRank8BB)))
    {
        if (Type == CAPTURES)
            emptySquares = ~pos.pieces();

        if (Type == EVASIONS)
            emptySquares &= target;

        mlist = generate_promotions<Type, RIGHT>(mlist, pawnsOn7, enemies, ksq);
        mlist = generate_promotions<Type, LEFT>(mlist, pawnsOn7, enemies, ksq);
        mlist = generate_promotions<Type, UP>(mlist, pawnsOn7, emptySquares, ksq);
    }

    // Standard and en-passant captures
    if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
    {
        b1 = move_pawns<RIGHT>(pawnsNotOn7) & enemies;
        b2 = move_pawns<LEFT >(pawnsNotOn7) & enemies;

        SERIALIZE_PAWNS(b1, RIGHT);
        SERIALIZE_PAWNS(b2, LEFT);

        if (pos.ep_square() != SQ_NONE)
        {
            assert(rank_of(pos.ep_square()) == relative_rank(Us, RANK_6));

            // An en passant capture can be an evasion only if the checking piece
            // is the double pushed pawn and so is in the target. Otherwise this
            // is a discovery check and we are forced to do otherwise.
            if (Type == EVASIONS && !(target & (pos.ep_square() - UP)))
                return mlist;

            b1 = pawnsNotOn7 & pos.attacks_from<PAWN>(pos.ep_square(), Them);

            assert(b1);

            while (b1)
                (*mlist++).move = make<ENPASSANT>(pop_lsb(&b1), pos.ep_square());
        }
    }

    return mlist;
  }


  template<PieceType Pt, bool OnlyChecks> FORCE_INLINE
  MoveStack* generate_moves(const Position& pos, MoveStack* mlist, Color us,
                            Bitboard target, const CheckInfo* ci = NULL) {

    assert(Pt != KING && Pt != PAWN);

    const Square* pl = pos.piece_list(us, Pt);

    for (Square from = *pl; from != SQ_NONE; from = *++pl)
    {
        if (OnlyChecks)
        {
            if (    (Pt == BISHOP || Pt == ROOK || Pt == QUEEN)
                && !(PseudoAttacks[Pt][from] & target & ci->checkSq[Pt]))
                continue;

            if (ci->dcCandidates && (ci->dcCandidates & from))
                continue;
        }

        Bitboard b = pos.attacks_from<Pt>(from) & target;

        if (OnlyChecks)
            b &= ci->checkSq[Pt];

        SERIALIZE(b);
    }

    return mlist;
  }

  template<> FORCE_INLINE
  MoveStack* generate_moves<KING, false>(const Position& pos, MoveStack* mlist, Color us,
                                         Bitboard target, const CheckInfo*) {
    Square from = pos.king_square(us);
    Bitboard b = pos.attacks_from<KING>(from) & target;
    SERIALIZE(b);
    return mlist;
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
MoveStack* generate(const Position& pos, MoveStack* mlist) {

  assert(Type == CAPTURES || Type == QUIETS || Type == NON_EVASIONS);
  assert(!pos.in_check());

  Color us = pos.side_to_move();
  Bitboard target;

  if (Type == CAPTURES)
      target = pos.pieces(~us);

  else if (Type == QUIETS)
      target = ~pos.pieces();

  else if (Type == NON_EVASIONS)
      target = ~pos.pieces(us);

  mlist = (us == WHITE ? generate_pawn_moves<WHITE, Type>(pos, mlist, target)
                       : generate_pawn_moves<BLACK, Type>(pos, mlist, target));

  mlist = generate_moves<KNIGHT, false>(pos, mlist, us, target);
  mlist = generate_moves<BISHOP, false>(pos, mlist, us, target);
  mlist = generate_moves<ROOK,   false>(pos, mlist, us, target);
  mlist = generate_moves<QUEEN,  false>(pos, mlist, us, target);
  mlist = generate_moves<KING,   false>(pos, mlist, us, target);

  if (Type != CAPTURES && pos.can_castle(us))
  {
      mlist = generate_castle<KING_SIDE,  false>(pos, mlist, us);
      mlist = generate_castle<QUEEN_SIDE, false>(pos, mlist, us);
  }

  return mlist;
}

// Explicit template instantiations
template MoveStack* generate<CAPTURES>(const Position& pos, MoveStack* mlist);
template MoveStack* generate<QUIETS>(const Position& pos, MoveStack* mlist);
template MoveStack* generate<NON_EVASIONS>(const Position& pos, MoveStack* mlist);


/// generate<QUIET_CHECKS> generates all pseudo-legal non-captures and knight
/// underpromotions that give check. Returns a pointer to the end of the move list.
template<>
MoveStack* generate<QUIET_CHECKS>(const Position& pos, MoveStack* mlist) {

  assert(!pos.in_check());

  Color us = pos.side_to_move();
  CheckInfo ci(pos);
  Bitboard empty = ~pos.pieces();
  Bitboard dc = ci.dcCandidates;

  while (dc)
  {
     Square from = pop_lsb(&dc);
     PieceType pt = type_of(pos.piece_on(from));

     if (pt == PAWN)
         continue; // Will be generated togheter with direct checks

     Bitboard b = pos.attacks_from(Piece(pt), from) & ~pos.pieces();

     if (pt == KING)
         b &= ~PseudoAttacks[QUEEN][ci.ksq];

     SERIALIZE(b);
  }

  mlist = (us == WHITE ? generate_pawn_moves<WHITE, QUIET_CHECKS>(pos, mlist, ci.dcCandidates, ci.ksq)
                       : generate_pawn_moves<BLACK, QUIET_CHECKS>(pos, mlist, ci.dcCandidates, ci.ksq));

  mlist = generate_moves<KNIGHT, true>(pos, mlist, us, empty, &ci);
  mlist = generate_moves<BISHOP, true>(pos, mlist, us, empty, &ci);
  mlist = generate_moves<ROOK,   true>(pos, mlist, us, empty, &ci);
  mlist = generate_moves<QUEEN,  true>(pos, mlist, us, empty, &ci);

  if (pos.can_castle(us))
  {
      mlist = generate_castle<KING_SIDE,  true>(pos, mlist, us);
      mlist = generate_castle<QUEEN_SIDE, true>(pos, mlist, us);
  }

  return mlist;
}


/// generate<EVASIONS> generates all pseudo-legal check evasions when the side
/// to move is in check. Returns a pointer to the end of the move list.
template<>
MoveStack* generate<EVASIONS>(const Position& pos, MoveStack* mlist) {

  assert(pos.in_check());

  Bitboard b, target;
  Square from, checksq;
  int checkersCnt = 0;
  Color us = pos.side_to_move();
  Square ksq = pos.king_square(us);
  Bitboard sliderAttacks = 0;
  Bitboard checkers = pos.checkers();

  assert(checkers);

  // Find squares attacked by slider checkers, we will remove them from the king
  // evasions so to skip known illegal moves avoiding useless legality check later.
  b = checkers;
  do
  {
      checkersCnt++;
      checksq = pop_lsb(&b);

      assert(color_of(pos.piece_on(checksq)) == ~us);

      switch (type_of(pos.piece_on(checksq)))
      {
      case BISHOP: sliderAttacks |= PseudoAttacks[BISHOP][checksq]; break;
      case ROOK:   sliderAttacks |= PseudoAttacks[ROOK][checksq];   break;
      case QUEEN:
          // If queen and king are far or not on a diagonal line we can safely
          // remove all the squares attacked in the other direction becuase are
          // not reachable by the king anyway.
          if (between_bb(ksq, checksq) || !(PseudoAttacks[BISHOP][checksq] & ksq))
              sliderAttacks |= PseudoAttacks[QUEEN][checksq];

          // Otherwise we need to use real rook attacks to check if king is safe
          // to move in the other direction. For example: king in B2, queen in A1
          // a knight in B1, and we can safely move to C1.
          else
              sliderAttacks |= PseudoAttacks[BISHOP][checksq] | pos.attacks_from<ROOK>(checksq);

      default:
          break;
      }
  } while (b);

  // Generate evasions for king, capture and non capture moves
  b = pos.attacks_from<KING>(ksq) & ~pos.pieces(us) & ~sliderAttacks;
  from = ksq;
  SERIALIZE(b);

  // Generate evasions for other pieces only if not under a double check
  if (checkersCnt > 1)
      return mlist;

  // Blocking evasions or captures of the checking piece
  target = between_bb(checksq, ksq) | checkers;

  mlist = (us == WHITE ? generate_pawn_moves<WHITE, EVASIONS>(pos, mlist, target)
                       : generate_pawn_moves<BLACK, EVASIONS>(pos, mlist, target));

  mlist = generate_moves<KNIGHT, false>(pos, mlist, us, target);
  mlist = generate_moves<BISHOP, false>(pos, mlist, us, target);
  mlist = generate_moves<ROOK,   false>(pos, mlist, us, target);
  return  generate_moves<QUEEN,  false>(pos, mlist, us, target);
}


/// generate<LEGAL> generates all the legal moves in the given position

template<>
MoveStack* generate<LEGAL>(const Position& pos, MoveStack* mlist) {

  MoveStack *last, *cur = mlist;
  Bitboard pinned = pos.pinned_pieces();

  last = pos.in_check() ? generate<EVASIONS>(pos, mlist)
                        : generate<NON_EVASIONS>(pos, mlist);
  while (cur != last)
      if (!pos.pl_move_is_legal(cur->move, pinned))
          cur->move = (--last)->move;
      else
          cur++;

  return last;
}
