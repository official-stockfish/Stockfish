/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include "bitcount.h"
#include "movegen.h"

// Simple macro to wrap a very common while loop, no facny, no flexibility,
// hardcoded list name 'mlist' and from square 'from'.
#define SERIALIZE_MOVES(b) while (b) (*mlist++).move = make_move(from, pop_1st_bit(&b))

// Version used for pawns, where the 'from' square is given as a delta from the 'to' square
#define SERIALIZE_MOVES_D(b, d) while (b) { to = pop_1st_bit(&b); (*mlist++).move = make_move(to + (d), to); }

namespace {

  enum CastlingSide {
    KING_SIDE,
    QUEEN_SIDE
  };

  template<CastlingSide>
  MoveStack* generate_castle_moves(const Position&, MoveStack*, Color us);

  template<Color, MoveType>
  MoveStack* generate_pawn_moves(const Position&, MoveStack*, Bitboard, Square);

  template<PieceType Pt>
  inline MoveStack* generate_discovered_checks(const Position& pos, MoveStack* mlist, Square from) {

    assert(Pt != QUEEN);

    Bitboard b = pos.attacks_from<Pt>(from) & pos.empty_squares();
    if (Pt == KING)
    {
        Square ksq = pos.king_square(opposite_color(pos.side_to_move()));
        b &= ~QueenPseudoAttacks[ksq];
    }
    SERIALIZE_MOVES(b);
    return mlist;
  }

  template<PieceType Pt>
  inline MoveStack* generate_direct_checks(const Position& pos, MoveStack* mlist, Color us,
                                           Bitboard dc, Square ksq) {
    assert(Pt != KING);

    Bitboard checkSqs, b;
    Square from;
    const Square* ptr = pos.piece_list_begin(us, Pt);

    if ((from = *ptr++) == SQ_NONE)
        return mlist;

    checkSqs = pos.attacks_from<Pt>(ksq) & pos.empty_squares();

    do
    {
        if (   (Pt == QUEEN  && !(QueenPseudoAttacks[from]  & checkSqs))
            || (Pt == ROOK   && !(RookPseudoAttacks[from]   & checkSqs))
            || (Pt == BISHOP && !(BishopPseudoAttacks[from] & checkSqs)))
            continue;

        if (dc && bit_is_set(dc, from))
            continue;

        b = pos.attacks_from<Pt>(from) & checkSqs;
        SERIALIZE_MOVES(b);

    } while ((from = *ptr++) != SQ_NONE);

    return mlist;
  }

  template<>
  FORCE_INLINE MoveStack* generate_direct_checks<PAWN>(const Position& p, MoveStack* m, Color us, Bitboard dc, Square ksq) {

    return (us == WHITE ? generate_pawn_moves<WHITE, MV_CHECK>(p, m, dc, ksq)
                        : generate_pawn_moves<BLACK, MV_CHECK>(p, m, dc, ksq));
  }

  template<PieceType Pt, MoveType Type>
  FORCE_INLINE MoveStack* generate_piece_moves(const Position& p, MoveStack* m, Color us, Bitboard t) {

    assert(Pt == PAWN);
    assert(Type == MV_CAPTURE || Type == MV_NON_CAPTURE || Type == MV_EVASION);

    return (us == WHITE ? generate_pawn_moves<WHITE, Type>(p, m, t, SQ_NONE)
                        : generate_pawn_moves<BLACK, Type>(p, m, t, SQ_NONE));
  }

  template<PieceType Pt>
  FORCE_INLINE MoveStack* generate_piece_moves(const Position& pos, MoveStack* mlist, Color us, Bitboard target) {

    Bitboard b;
    Square from;
    const Square* ptr = pos.piece_list_begin(us, Pt);

    if (*ptr != SQ_NONE)
    {
        do {
            from = *ptr;
            b = pos.attacks_from<Pt>(from) & target;
            SERIALIZE_MOVES(b);
        } while (*++ptr != SQ_NONE);
    }
    return mlist;
  }

  template<>
  FORCE_INLINE MoveStack* generate_piece_moves<KING>(const Position& pos, MoveStack* mlist, Color us, Bitboard target) {

    Bitboard b;
    Square from = pos.king_square(us);

    b = pos.attacks_from<KING>(from) & target;
    SERIALIZE_MOVES(b);
    return mlist;
  }

}


/// generate<MV_CAPTURE> generates all pseudo-legal captures and queen
/// promotions. Returns a pointer to the end of the move list.
///
/// generate<MV_NON_CAPTURE> generates all pseudo-legal non-captures and
/// underpromotions. Returns a pointer to the end of the move list.
///
/// generate<MV_NON_EVASION> generates all pseudo-legal captures and
/// non-captures. Returns a pointer to the end of the move list.

template<MoveType Type>
MoveStack* generate(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.in_check());

  Color us = pos.side_to_move();
  Bitboard target;

  if (Type == MV_CAPTURE || Type == MV_NON_EVASION)
      target = pos.pieces_of_color(opposite_color(us));
  else if (Type == MV_NON_CAPTURE)
      target = pos.empty_squares();
  else
      assert(false);

  if (Type == MV_NON_EVASION)
  {
      mlist = generate_piece_moves<PAWN, MV_CAPTURE>(pos, mlist, us, target);
      mlist = generate_piece_moves<PAWN, MV_NON_CAPTURE>(pos, mlist, us, pos.empty_squares());
      target |= pos.empty_squares();
  }
  else
      mlist = generate_piece_moves<PAWN, Type>(pos, mlist, us, target);

  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  mlist = generate_piece_moves<QUEEN>(pos, mlist, us, target);
  mlist = generate_piece_moves<KING>(pos, mlist, us, target);

  if (Type != MV_CAPTURE)
  {
      if (pos.can_castle_kingside(us))
          mlist = generate_castle_moves<KING_SIDE>(pos, mlist, us);

      if (pos.can_castle_queenside(us))
          mlist = generate_castle_moves<QUEEN_SIDE>(pos, mlist, us);
  }

  return mlist;
}

// Explicit template instantiations
template MoveStack* generate<MV_CAPTURE>(const Position& pos, MoveStack* mlist);
template MoveStack* generate<MV_NON_CAPTURE>(const Position& pos, MoveStack* mlist);
template MoveStack* generate<MV_NON_EVASION>(const Position& pos, MoveStack* mlist);


/// generate_non_capture_checks() generates all pseudo-legal non-captures and knight
/// underpromotions that give check. Returns a pointer to the end of the move list.
template<>
MoveStack* generate<MV_NON_CAPTURE_CHECK>(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.in_check());

  Bitboard b, dc;
  Square from;
  Color us = pos.side_to_move();
  Square ksq = pos.king_square(opposite_color(us));

  assert(pos.piece_on(ksq) == make_piece(opposite_color(us), KING));

  // Discovered non-capture checks
  b = dc = pos.discovered_check_candidates(us);

  while (b)
  {
     from = pop_1st_bit(&b);
     switch (pos.type_of_piece_on(from))
     {
      case PAWN:   /* Will be generated togheter with pawns direct checks */     break;
      case KNIGHT: mlist = generate_discovered_checks<KNIGHT>(pos, mlist, from); break;
      case BISHOP: mlist = generate_discovered_checks<BISHOP>(pos, mlist, from); break;
      case ROOK:   mlist = generate_discovered_checks<ROOK>(pos, mlist, from);   break;
      case KING:   mlist = generate_discovered_checks<KING>(pos, mlist, from);   break;
      default: assert(false); break;
     }
  }

  // Direct non-capture checks
  mlist = generate_direct_checks<PAWN>(pos, mlist, us, dc, ksq);
  mlist = generate_direct_checks<KNIGHT>(pos, mlist, us, dc, ksq);
  mlist = generate_direct_checks<BISHOP>(pos, mlist, us, dc, ksq);
  mlist = generate_direct_checks<ROOK>(pos, mlist, us, dc, ksq);
  return  generate_direct_checks<QUEEN>(pos, mlist, us, dc, ksq);
}


/// generate_evasions() generates all pseudo-legal check evasions when
/// the side to move is in check. Returns a pointer to the end of the move list.
template<>
MoveStack* generate<MV_EVASION>(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(pos.in_check());

  Bitboard b, target;
  Square from, checksq;
  int checkersCnt = 0;
  Color us = pos.side_to_move();
  Square ksq = pos.king_square(us);
  Bitboard checkers = pos.checkers();
  Bitboard sliderAttacks = EmptyBoardBB;

  assert(pos.piece_on(ksq) == make_piece(us, KING));
  assert(checkers);

  // Find squares attacked by slider checkers, we will remove
  // them from the king evasions set so to early skip known
  // illegal moves and avoid an useless legality check later.
  b = checkers;
  do
  {
      checkersCnt++;
      checksq = pop_1st_bit(&b);

      assert(pos.color_of_piece_on(checksq) == opposite_color(us));

      switch (pos.type_of_piece_on(checksq))
      {
      case BISHOP: sliderAttacks |= BishopPseudoAttacks[checksq]; break;
      case ROOK:   sliderAttacks |= RookPseudoAttacks[checksq];   break;
      case QUEEN:
          // In case of a queen remove also squares attacked in the other direction to
          // avoid possible illegal moves when queen and king are on adjacent squares.
          if (RookPseudoAttacks[checksq] & (1ULL << ksq))
              sliderAttacks |= RookPseudoAttacks[checksq] | pos.attacks_from<BISHOP>(checksq);
          else
              sliderAttacks |= BishopPseudoAttacks[checksq] | pos.attacks_from<ROOK>(checksq);
      default:
          break;
      }
  } while (b);

  // Generate evasions for king, capture and non capture moves
  b = pos.attacks_from<KING>(ksq) & ~pos.pieces_of_color(us) & ~sliderAttacks;
  from = ksq;
  SERIALIZE_MOVES(b);

  // Generate evasions for other pieces only if not double check
  if (checkersCnt > 1)
      return mlist;

  // Find squares where a blocking evasion or a capture of the
  // checker piece is possible.
  target = squares_between(checksq, ksq) | checkers;

  mlist = generate_piece_moves<PAWN, MV_EVASION>(pos, mlist, us, target);
  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  return  generate_piece_moves<QUEEN>(pos, mlist, us, target);
}


/// generate<MV_LEGAL / MV_PSEUDO_LEGAL> computes a complete list of legal
/// or pseudo-legal moves in the current position.
template<>
MoveStack* generate<MV_PSEUDO_LEGAL>(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());

  return pos.in_check() ? generate<MV_EVASION>(pos, mlist)
                        : generate<MV_NON_EVASION>(pos, mlist);
}

template<>
MoveStack* generate<MV_LEGAL>(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());

  MoveStack *last, *cur = mlist;
  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());

  last = generate<MV_PSEUDO_LEGAL>(pos, mlist);

  // Remove illegal moves from the list
  while (cur != last)
      if (!pos.pl_move_is_legal(cur->move, pinned))
          cur->move = (--last)->move;
      else
          cur++;

  return last;
}


namespace {

  template<Square Delta>
  inline Bitboard move_pawns(Bitboard p) {

    return Delta == DELTA_N  ? p << 8 : Delta == DELTA_S  ? p >> 8 :
           Delta == DELTA_NE ? p << 9 : Delta == DELTA_SE ? p >> 7 :
           Delta == DELTA_NW ? p << 7 : Delta == DELTA_SW ? p >> 9 : p;
  }

  template<MoveType Type, Square Delta>
  inline MoveStack* generate_pawn_captures(MoveStack* mlist, Bitboard pawns, Bitboard target) {

    const Bitboard TFileABB = (Delta == DELTA_NE || Delta == DELTA_SE ? FileABB : FileHBB);

    Bitboard b;
    Square to;

    // Captures in the a1-h8 (a8-h1 for black) diagonal or in the h1-a8 (h8-a1 for black)
    b = move_pawns<Delta>(pawns) & target & ~TFileABB;
    SERIALIZE_MOVES_D(b, -Delta);
    return mlist;
  }

  template<Color Us, MoveType Type, Square Delta>
  inline MoveStack* generate_promotions(const Position& pos, MoveStack* mlist, Bitboard pawnsOn7, Bitboard target) {

    const Bitboard TFileABB = (Delta == DELTA_NE || Delta == DELTA_SE ? FileABB : FileHBB);

    Bitboard b;
    Square to;

    // Promotions and under-promotions, both captures and non-captures
    b = move_pawns<Delta>(pawnsOn7) & target;

    if (Delta != DELTA_N && Delta != DELTA_S)
        b &= ~TFileABB;

    while (b)
    {
        to = pop_1st_bit(&b);

        if (Type == MV_CAPTURE || Type == MV_EVASION)
            (*mlist++).move = make_promotion_move(to - Delta, to, QUEEN);

        if (Type == MV_NON_CAPTURE || Type == MV_EVASION)
        {
            (*mlist++).move = make_promotion_move(to - Delta, to, ROOK);
            (*mlist++).move = make_promotion_move(to - Delta, to, BISHOP);
            (*mlist++).move = make_promotion_move(to - Delta, to, KNIGHT);
        }

        // This is the only possible under promotion that can give a check
        // not already included in the queen-promotion.
        if (   Type == MV_CHECK
            && bit_is_set(pos.attacks_from<KNIGHT>(to), pos.king_square(opposite_color(Us))))
            (*mlist++).move = make_promotion_move(to - Delta, to, KNIGHT);
        else (void)pos; // Silence a warning under MSVC
    }
    return mlist;
  }

  template<Color Us, MoveType Type>
  MoveStack* generate_pawn_moves(const Position& pos, MoveStack* mlist, Bitboard target, Square ksq) {

    // Calculate our parametrized parameters at compile time, named
    // according to the point of view of white side.
    const Color    Them      = (Us == WHITE ? BLACK    : WHITE);
    const Bitboard TRank7BB  = (Us == WHITE ? Rank7BB  : Rank2BB);
    const Bitboard TRank3BB  = (Us == WHITE ? Rank3BB  : Rank6BB);
    const Square   TDELTA_N  = (Us == WHITE ? DELTA_N  : DELTA_S);
    const Square   TDELTA_NE = (Us == WHITE ? DELTA_NE : DELTA_SE);
    const Square   TDELTA_NW = (Us == WHITE ? DELTA_NW : DELTA_SW);

    Square to;
    Bitboard b1, b2, dc1, dc2, pawnPushes, emptySquares;
    Bitboard pawns = pos.pieces(PAWN, Us);
    Bitboard pawnsOn7 = pawns & TRank7BB;
    Bitboard enemyPieces = (Type == MV_CAPTURE ? target : pos.pieces_of_color(Them));

    // Pre-calculate pawn pushes before changing emptySquares definition
    if (Type != MV_CAPTURE)
    {
        emptySquares = (Type == MV_NON_CAPTURE ? target : pos.empty_squares());
        pawnPushes = move_pawns<TDELTA_N>(pawns & ~TRank7BB) & emptySquares;
    }

    if (Type == MV_EVASION)
    {
        emptySquares &= target; // Only blocking squares
        enemyPieces  &= target; // Capture only the checker piece
    }

    // Promotions and underpromotions
    if (pawnsOn7)
    {
        if (Type == MV_CAPTURE)
            emptySquares = pos.empty_squares();

        pawns &= ~TRank7BB;
        mlist = generate_promotions<Us, Type, TDELTA_NE>(pos, mlist, pawnsOn7, enemyPieces);
        mlist = generate_promotions<Us, Type, TDELTA_NW>(pos, mlist, pawnsOn7, enemyPieces);
        mlist = generate_promotions<Us, Type, TDELTA_N >(pos, mlist, pawnsOn7, emptySquares);
    }

    // Standard captures
    if (Type == MV_CAPTURE || Type == MV_EVASION)
    {
        mlist = generate_pawn_captures<Type, TDELTA_NE>(mlist, pawns, enemyPieces);
        mlist = generate_pawn_captures<Type, TDELTA_NW>(mlist, pawns, enemyPieces);
    }

    // Single and double pawn pushes
    if (Type != MV_CAPTURE)
    {
        b1 = pawnPushes & emptySquares;
        b2 = move_pawns<TDELTA_N>(pawnPushes & TRank3BB) & emptySquares;

        if (Type == MV_CHECK)
        {
            // Consider only pawn moves which give direct checks
            b1 &= pos.attacks_from<PAWN>(ksq, Them);
            b2 &= pos.attacks_from<PAWN>(ksq, Them);

            // Add pawn moves which gives discovered check. This is possible only
            // if the pawn is not on the same file as the enemy king, because we
            // don't generate captures.
            if (pawns & target) // For CHECK type target is dc bitboard
            {
                dc1 = move_pawns<TDELTA_N>(pawns & target & ~file_bb(ksq)) & emptySquares;
                dc2 = move_pawns<TDELTA_N>(dc1 & TRank3BB) & emptySquares;

                b1 |= dc1;
                b2 |= dc2;
            }
        }
        SERIALIZE_MOVES_D(b1, -TDELTA_N);
        SERIALIZE_MOVES_D(b2, -TDELTA_N -TDELTA_N);
    }

    // En passant captures
    if ((Type == MV_CAPTURE || Type == MV_EVASION) && pos.ep_square() != SQ_NONE)
    {
        assert(Us != WHITE || square_rank(pos.ep_square()) == RANK_6);
        assert(Us != BLACK || square_rank(pos.ep_square()) == RANK_3);

        // An en passant capture can be an evasion only if the checking piece
        // is the double pushed pawn and so is in the target. Otherwise this
        // is a discovery check and we are forced to do otherwise.
        if (Type == MV_EVASION && !bit_is_set(target, pos.ep_square() - TDELTA_N))
            return mlist;

        b1 = pawns & pos.attacks_from<PAWN>(pos.ep_square(), Them);

        assert(b1 != EmptyBoardBB);

        while (b1)
        {
            to = pop_1st_bit(&b1);
            (*mlist++).move = make_ep_move(to, pos.ep_square());
        }
    }
    return mlist;
  }

  template<CastlingSide Side>
  MoveStack* generate_castle_moves(const Position& pos, MoveStack* mlist, Color us) {

    Color them = opposite_color(us);
    Square ksq = pos.king_square(us);

    assert(pos.piece_on(ksq) == make_piece(us, KING));

    Square rsq = (Side == KING_SIDE ? pos.initial_kr_square(us) : pos.initial_qr_square(us));
    Square s1 = relative_square(us, Side == KING_SIDE ? SQ_G1 : SQ_C1);
    Square s2 = relative_square(us, Side == KING_SIDE ? SQ_F1 : SQ_D1);
    Square s;
    bool illegal = false;

    assert(pos.piece_on(rsq) == make_piece(us, ROOK));

    // It is a bit complicated to correctly handle Chess960
    for (s = Min(ksq, s1); s <= Max(ksq, s1); s++)
        if (  (s != ksq && s != rsq && pos.square_is_occupied(s))
            ||(pos.attackers_to(s) & pos.pieces_of_color(them)))
            illegal = true;

    for (s = Min(rsq, s2); s <= Max(rsq, s2); s++)
        if (s != ksq && s != rsq && pos.square_is_occupied(s))
            illegal = true;

    if (   Side == QUEEN_SIDE
        && square_file(rsq) == FILE_B
        && (   pos.piece_on(relative_square(us, SQ_A1)) == make_piece(them, ROOK)
            || pos.piece_on(relative_square(us, SQ_A1)) == make_piece(them, QUEEN)))
        illegal = true;

    if (!illegal)
        (*mlist++).move = make_castle_move(ksq, rsq);

    return mlist;
  }

} // namespace
