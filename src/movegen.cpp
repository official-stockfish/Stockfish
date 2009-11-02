/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2009 Marco Costalba

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


////
//// Includes
////

#include <cassert>

#include "bitcount.h"
#include "movegen.h"

// Simple macro to wrap a very common while loop, no facny, no flexibility,
// hardcoded list name 'mlist' and from square 'from'.
#define SERIALIZE_MOVES(b) while (b) (*mlist++).move = make_move(from, pop_1st_bit(&b))

// Version used for pawns, where the 'from' square is given as a delta from the 'to' square
#define SERIALIZE_MOVES_D(b, d) while (b) { to = pop_1st_bit(&b); (*mlist++).move = make_move(to + (d), to); }

////
//// Local definitions
////

namespace {

  enum CastlingSide {
    KING_SIDE,
    QUEEN_SIDE
  };

  enum MoveType {
    CAPTURE,
    NON_CAPTURE,
    CHECK,
    EVASION
  };

  // Helper templates
  template<CastlingSide Side>
  MoveStack* generate_castle_moves(const Position&, MoveStack*);

  template<Color Us, MoveType Type>
  MoveStack* generate_pawn_moves(const Position&, MoveStack*, Bitboard = EmptyBoardBB,
                                 Square = SQ_NONE, Bitboard = EmptyBoardBB);

  // Template generate_piece_moves (captures and non-captures) with specializations and overloads
  template<PieceType>
  MoveStack* generate_piece_moves(const Position&, MoveStack*, Color, Bitboard);

  template<>
  MoveStack* generate_piece_moves<KING>(const Position&, MoveStack*, Color, Bitboard);

  template<PieceType Piece, MoveType Type>
  inline MoveStack* generate_piece_moves(const Position& p, MoveStack* m, Color us) {

    assert(Piece == PAWN);
    assert(Type == CAPTURE || Type == NON_CAPTURE);

    return (us == WHITE ? generate_pawn_moves<WHITE, Type>(p, m)
                        : generate_pawn_moves<BLACK, Type>(p, m));
  }

  // Templates for non-capture checks generation

  template<PieceType Piece>
  MoveStack* generate_discovered_checks(const Position& pos, Square from, MoveStack* mlist);

  template<PieceType>
  MoveStack* generate_direct_checks(const Position&, MoveStack*, Color, Bitboard, Square);

  template<>
  inline MoveStack* generate_direct_checks<PAWN>(const Position& p, MoveStack* m, Color us, Bitboard dc, Square ksq) {

    return (us == WHITE ? generate_pawn_moves<WHITE, CHECK>(p, m, dc, ksq)
                        : generate_pawn_moves<BLACK, CHECK>(p, m, dc, ksq));
  }

  // Template generate_piece_evasions with specializations
  template<PieceType>
  MoveStack* generate_piece_evasions(const Position&, MoveStack*, Color, Bitboard, Bitboard);

  template<>
  inline MoveStack* generate_piece_evasions<PAWN>(const Position& p, MoveStack* m,
                                                  Color us, Bitboard t, Bitboard pnd) {

    return (us == WHITE ? generate_pawn_moves<WHITE, EVASION>(p, m, pnd, SQ_NONE, t)
                        : generate_pawn_moves<BLACK, EVASION>(p, m, pnd, SQ_NONE, t));
  }
}


////
//// Functions
////


/// generate_captures() generates all pseudo-legal captures and queen
/// promotions. Returns a pointer to the end of the move list.

MoveStack* generate_captures(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Bitboard target = pos.pieces_of_color(opposite_color(us));

  mlist = generate_piece_moves<QUEEN>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<PAWN, CAPTURE>(pos, mlist, us);
  return  generate_piece_moves<KING>(pos, mlist, us, target);
}


/// generate_noncaptures() generates all pseudo-legal non-captures and
/// underpromotions. Returns a pointer to the end of the move list.

MoveStack* generate_noncaptures(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Bitboard target = pos.empty_squares();

  mlist = generate_piece_moves<PAWN, NON_CAPTURE>(pos, mlist, us);
  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  mlist = generate_piece_moves<QUEEN>(pos, mlist, us, target);
  mlist = generate_piece_moves<KING>(pos, mlist, us, target);
  mlist = generate_castle_moves<KING_SIDE>(pos, mlist);
  return  generate_castle_moves<QUEEN_SIDE>(pos, mlist);
}


/// generate_non_capture_checks() generates all pseudo-legal non-captures and
/// underpromotions that give check. Returns a pointer to the end of the move list.

MoveStack* generate_non_capture_checks(const Position& pos, MoveStack* mlist, Bitboard dc) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Square ksq = pos.king_square(opposite_color(us));

  assert(pos.piece_on(ksq) == piece_of_color_and_type(opposite_color(us), KING));

  // Discovered non-capture checks
  Bitboard b = dc;
  while (b)
  {
     Square from = pop_1st_bit(&b);
     switch (pos.type_of_piece_on(from))
     {
      case PAWN:   /* Will be generated togheter with pawns direct checks */     break;
      case KNIGHT: mlist = generate_discovered_checks<KNIGHT>(pos, from, mlist); break;
      case BISHOP: mlist = generate_discovered_checks<BISHOP>(pos, from, mlist); break;
      case ROOK:   mlist = generate_discovered_checks<ROOK>(pos, from, mlist);   break;
      case KING:   mlist = generate_discovered_checks<KING>(pos, from, mlist);   break;
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


/// generate_evasions() generates all check evasions when the side to move is
/// in check. Unlike the other move generation functions, this one generates
/// only legal moves. Returns a pointer to the end of the move list.

MoveStack* generate_evasions(const Position& pos, MoveStack* mlist, Bitboard pinned) {

  assert(pos.is_ok());
  assert(pos.is_check());

  Square from, to;
  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square ksq = pos.king_square(us);
  Bitboard sliderAttacks = EmptyBoardBB;
  Bitboard checkers = pos.checkers();

  assert(pos.piece_on(ksq) == piece_of_color_and_type(us, KING));

  // The bitboard of occupied pieces without our king
  Bitboard b_noKing = pos.occupied_squares();
  clear_bit(&b_noKing, ksq);

  // Find squares attacked by slider checkers, we will remove
  // them from the king evasions set so to avoid a couple
  // of cycles in the slow king evasions legality check loop
  // and to be able to use attackers_to().
  Bitboard b = checkers & pos.pieces(BISHOP, QUEEN);
  while (b)
  {
      from = pop_1st_bit(&b);
      sliderAttacks |= bishop_attacks_bb(from, b_noKing);
  }

  b = checkers & pos.pieces(ROOK, QUEEN);
  while (b)
  {
      from = pop_1st_bit(&b);
      sliderAttacks |= rook_attacks_bb(from, b_noKing);
  }

  // Generate evasions for king, capture and non capture moves
  Bitboard enemy = pos.pieces_of_color(them);
  Bitboard b1 = pos.attacks_from<KING>(ksq) & ~pos.pieces_of_color(us) & ~sliderAttacks;
  while (b1)
  {
      // Note that we can use attackers_to() only because we have already
      // removed from b1 the squares attacked by slider checkers.
      to = pop_1st_bit(&b1);
      if (!(pos.attackers_to(to) & enemy))
          (*mlist++).move = make_move(ksq, to);
  }

  // Generate evasions for other pieces only if not double check. We use a
  // simple bit twiddling hack here rather than calling count_1s in order to
  // save some time (we know that pos.checkers() has at most two nonzero bits).
  if (checkers & (checkers - 1)) // Two bits set?
      return mlist;

  Square checksq = first_1(checkers);
  Bitboard target = squares_between(checksq, ksq);

  assert(pos.color_of_piece_on(checksq) == them);

  // Pawn captures
  b1 = pos.attacks_from<PAWN>(checksq, them) & pos.pieces(PAWN, us) & ~pinned;
  while (b1)
  {
      from = pop_1st_bit(&b1);
      if (relative_rank(us, checksq) == RANK_8)
      {
          (*mlist++).move = make_promotion_move(from, checksq, QUEEN);
          (*mlist++).move = make_promotion_move(from, checksq, ROOK);
          (*mlist++).move = make_promotion_move(from, checksq, BISHOP);
          (*mlist++).move = make_promotion_move(from, checksq, KNIGHT);
      } else
          (*mlist++).move = make_move(from, checksq);
  }

  // Pawn blocking evasions (possible only if the checking piece is a slider)
  if (sliderAttacks)
      mlist = generate_piece_evasions<PAWN>(pos, mlist, us, target, pinned);

  // Add the checking piece to the target squares
  target |= checkers;

  // Captures and blocking evasions for the other pieces
  mlist = generate_piece_evasions<KNIGHT>(pos, mlist, us, target, pinned);
  mlist = generate_piece_evasions<BISHOP>(pos, mlist, us, target, pinned);
  mlist = generate_piece_evasions<ROOK>(pos, mlist, us, target, pinned);
  mlist = generate_piece_evasions<QUEEN>(pos, mlist, us, target, pinned);

  // Finally, the special case of en passant captures. An en passant
  // capture can only be a check evasion if the check is not a discovered
  // check. If pos.ep_square() is set, the last move made must have been
  // a double pawn push. If, furthermore, the checking piece is a pawn,
  // an en passant check evasion may be possible.
  if (pos.ep_square() != SQ_NONE && (checkers & pos.pieces(PAWN, them)))
  {
      to = pos.ep_square();
      b1 = pos.attacks_from<PAWN>(to, them) & pos.pieces(PAWN, us);

      // The checking pawn cannot be a discovered (bishop) check candidate
      // otherwise we were in check also before last double push move.
      assert(!bit_is_set(pos.discovered_check_candidates(them), checksq));
      assert(count_1s(b1) == 1 || count_1s(b1) == 2);

      b1 &= ~pinned;
      while (b1)
      {
          from = pop_1st_bit(&b1);
          // Move is always legal because checking pawn is not a discovered
          // check candidate and our capturing pawn has been already tested
          // against pinned pieces.
          (*mlist++).move = make_ep_move(from, to);
      }
  }
  return mlist;
}


/// generate_moves() computes a complete list of legal or pseudo-legal moves in
/// the current position. This function is not very fast, and should be used
/// only in non time-critical paths.

MoveStack* generate_moves(const Position& pos, MoveStack* mlist, bool pseudoLegal) {

  assert(pos.is_ok());

  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());

  if (pos.is_check())
      return generate_evasions(pos, mlist, pinned);

  // Generate pseudo-legal moves
  MoveStack* last = generate_captures(pos, mlist);
  last = generate_noncaptures(pos, last);
  if (pseudoLegal)
      return last;

  // Remove illegal moves from the list
  for (MoveStack* cur = mlist; cur != last; cur++)
      if (!pos.pl_move_is_legal(cur->move, pinned))
      {
          cur->move = (--last)->move;
          cur--;
      }
  return last;
}


/// move_is_legal() takes a position and a (not necessarily pseudo-legal)
/// move and tests whether the move is legal. This version is not very fast
/// and should be used only in non time-critical paths.

bool move_is_legal(const Position& pos, const Move m) {

  MoveStack mlist[256];
  MoveStack* last = generate_moves(pos, mlist, true);
  for (MoveStack* cur = mlist; cur != last; cur++)
      if (cur->move == m)
          return pos.pl_move_is_legal(m);

  return false;
}


/// Fast version of move_is_legal() that takes a position a move and a
/// bitboard of pinned pieces as input, and tests whether the move is legal.
/// This version must only be used when the side to move is not in check.

bool move_is_legal(const Position& pos, const Move m, Bitboard pinned) {

  assert(pos.is_ok());
  assert(!pos.is_check());
  assert(move_is_ok(m));
  assert(pinned == pos.pinned_pieces(pos.side_to_move()));

  // Use a slower but simpler function for uncommon cases
  if (move_is_ep(m) || move_is_castle(m))
      return move_is_legal(pos, m);

  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Square to = move_to(m);
  Piece pc = pos.piece_on(from);

  // If the from square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if (color_of_piece(pc) != us)
      return false;

  // The destination square cannot be occupied by a friendly piece
  if (pos.color_of_piece_on(to) == us)
      return false;

  // Handle the special case of a pawn move
  if (type_of_piece(pc) == PAWN)
  {
      // Move direction must be compatible with pawn color
      int direction = to - from;
      if ((us == WHITE) != (direction > 0))
          return false;

      // A pawn move is a promotion iff the destination square is
      // on the 8/1th rank.
      if ((  (square_rank(to) == RANK_8 && us == WHITE)
           ||(square_rank(to) == RANK_1 && us != WHITE)) != bool(move_is_promotion(m)))
          return false;

      // Proceed according to the square delta between the origin and
      // destination squares.
      switch (direction)
      {
      case DELTA_NW:
      case DELTA_NE:
      case DELTA_SW:
      case DELTA_SE:
      // Capture. The destination square must be occupied by an enemy
      // piece (en passant captures was handled earlier).
          if (pos.color_of_piece_on(to) != them)
              return false;
          break;

      case DELTA_N:
      case DELTA_S:
      // Pawn push. The destination square must be empty.
          if (!pos.square_is_empty(to))
              return false;
          break;

      case DELTA_NN:
      // Double white pawn push. The destination square must be on the fourth
      // rank, and both the destination square and the square between the
      // source and destination squares must be empty.
      if (   square_rank(to) != RANK_4
          || !pos.square_is_empty(to)
          || !pos.square_is_empty(from + DELTA_N))
          return false;
          break;

      case DELTA_SS:
      // Double black pawn push. The destination square must be on the fifth
      // rank, and both the destination square and the square between the
      // source and destination squares must be empty.
          if (   square_rank(to) != RANK_5
              || !pos.square_is_empty(to)
              || !pos.square_is_empty(from + DELTA_S))
              return false;
          break;

      default:
          return false;
      }
      // The move is pseudo-legal, check if it is also legal
      return pos.pl_move_is_legal(m, pinned);
  }

  // Luckly we can handle all the other pieces in one go
  return (   bit_is_set(pos.attacks_from(pc, from), to)
          && pos.pl_move_is_legal(m, pinned)
          && !move_is_promotion(m));
}


namespace {

  template<PieceType Piece>
  MoveStack* generate_piece_moves(const Position& pos, MoveStack* mlist, Color us, Bitboard target) {

    Square from;
    Bitboard b;
    const Square* ptr = pos.piece_list_begin(us, Piece);

    while ((from = *ptr++) != SQ_NONE)
    {
        b = pos.attacks_from<Piece>(from) & target;
        SERIALIZE_MOVES(b);
    }
    return mlist;
  }

  template<>
  MoveStack* generate_piece_moves<KING>(const Position& pos, MoveStack* mlist, Color us, Bitboard target) {

    Bitboard b;
    Square from = pos.king_square(us);

    b = pos.attacks_from<KING>(from) & target;
    SERIALIZE_MOVES(b);
    return mlist;
  }

  template<PieceType Piece>
  MoveStack* generate_piece_evasions(const Position& pos, MoveStack* mlist,
                                     Color us, Bitboard target, Bitboard pinned) {
    Square from;
    Bitboard b;
    const Square* ptr = pos.piece_list_begin(us, Piece);

    while ((from = *ptr++) != SQ_NONE)
    {
        if (pinned && bit_is_set(pinned, from))
            continue;

        b = pos.attacks_from<Piece>(from) & target;
        SERIALIZE_MOVES(b);
    }
    return mlist;
  }

  template<Color Us, SquareDelta Direction>
  inline Bitboard move_pawns(Bitboard p) {

    if (Direction == DELTA_N)
        return Us == WHITE ? p << 8 : p >> 8;
    else if (Direction == DELTA_NE)
        return Us == WHITE ? p << 9 : p >> 7;
    else if (Direction == DELTA_NW)
        return Us == WHITE ? p << 7 : p >> 9;
    else
        return p;
  }

  template<Color Us, SquareDelta Diagonal>
  MoveStack* generate_pawn_diagonal_captures(MoveStack* mlist, Bitboard pawns, Bitboard enemyPieces, bool promotion) {

    // Calculate our parametrized parameters at compile time
    const Bitboard TRank8BB = (Us == WHITE ? Rank8BB : Rank1BB);
    const Bitboard TFileABB = (Diagonal == DELTA_NE ? FileABB : FileHBB);
    const SquareDelta TDELTA_NE = (Us == WHITE ? DELTA_NE : DELTA_SE);
    const SquareDelta TDELTA_NW = (Us == WHITE ? DELTA_NW : DELTA_SW);
    const SquareDelta TTDELTA_NE = (Diagonal == DELTA_NE ? TDELTA_NE : TDELTA_NW);

    Square to;

    // Captures in the a1-h8 (a8-h1 for black) diagonal or in the h1-a8 (h8-a1 for black)
    Bitboard b1 = move_pawns<Us, Diagonal>(pawns) & ~TFileABB & enemyPieces;

    // Capturing promotions
    if (promotion)
    {
        Bitboard b2 = b1 & TRank8BB;
        b1 &= ~TRank8BB;
        while (b2)
        {
            to = pop_1st_bit(&b2);
            (*mlist++).move = make_promotion_move(to - TTDELTA_NE, to, QUEEN);
        }
    }

    // Capturing non-promotions
    SERIALIZE_MOVES_D(b1, -TTDELTA_NE);
    return mlist;
  }

  template<Color Us, MoveType Type>
  MoveStack* generate_pawn_moves(const Position& pos, MoveStack* mlist, Bitboard dcp,
                                 Square ksq, Bitboard blockSquares) {

    // Calculate our parametrized parameters at compile time
    const Color Them = (Us == WHITE ? BLACK : WHITE);
    const Bitboard TRank8BB = (Us == WHITE ? Rank8BB : Rank1BB);
    const Bitboard TRank7BB = (Us == WHITE ? Rank7BB : Rank2BB);
    const Bitboard TRank3BB = (Us == WHITE ? Rank3BB : Rank6BB);
    const SquareDelta TDELTA_NE = (Us == WHITE ? DELTA_NE : DELTA_SE);
    const SquareDelta TDELTA_NW = (Us == WHITE ? DELTA_NW : DELTA_SW);
    const SquareDelta TDELTA_N = (Us == WHITE ? DELTA_N : DELTA_S);

    Bitboard b1, b2, dcPawns1, dcPawns2;
    Square to;
    Bitboard pawns = (Type == EVASION ? pos.pieces(PAWN, Us) & ~dcp : pos.pieces(PAWN, Us));
    bool possiblePromotion = pawns & TRank7BB;

    if (Type == CAPTURE)
    {
        // Standard captures and capturing promotions in both directions
        Bitboard enemyPieces = pos.pieces_of_color(opposite_color(Us));
        mlist = generate_pawn_diagonal_captures<Us, DELTA_NE>(mlist, pawns, enemyPieces, possiblePromotion);
        mlist = generate_pawn_diagonal_captures<Us, DELTA_NW>(mlist, pawns, enemyPieces, possiblePromotion);
    }

    if (possiblePromotion)
    {
        // When generating checks consider under-promotion moves (both captures
        // and non captures) only if can give a discovery check. Note that dcp
        // is dc bitboard or pinned bitboard when Type == EVASION.
        Bitboard pp = (Type == CHECK ? pawns & dcp : pawns);

        if (Type != EVASION && Type != CAPTURE)
        {
            Bitboard enemyPieces = pos.pieces_of_color(opposite_color(Us));

            // Underpromotion captures in the a1-h8 (a8-h1 for black) direction
            b1 = move_pawns<Us, DELTA_NE>(pp) & ~FileABB & enemyPieces & TRank8BB;
            while (b1)
            {
                to = pop_1st_bit(&b1);
                (*mlist++).move = make_promotion_move(to - TDELTA_NE, to, ROOK);
                (*mlist++).move = make_promotion_move(to - TDELTA_NE, to, BISHOP);
                (*mlist++).move = make_promotion_move(to - TDELTA_NE, to, KNIGHT);
            }

            // Underpromotion captures in the h1-a8 (h8-a1 for black) direction
            b1 = move_pawns<Us, DELTA_NW>(pp) & ~FileHBB & enemyPieces & TRank8BB;
            while (b1)
            {
                to = pop_1st_bit(&b1);
                (*mlist++).move = make_promotion_move(to - TDELTA_NW, to, ROOK);
                (*mlist++).move = make_promotion_move(to - TDELTA_NW, to, BISHOP);
                (*mlist++).move = make_promotion_move(to - TDELTA_NW, to, KNIGHT);
            }
        }

        // Underpromotion pawn pushes. Also queen promotions for evasions and captures.
        b1 = move_pawns<Us, DELTA_N>(pp) & TRank8BB;
        b1 &= (Type == EVASION ? blockSquares : pos.empty_squares());

        while (b1)
        {
            to = pop_1st_bit(&b1);
            if (Type == EVASION || Type == CAPTURE)
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, QUEEN);

            if (Type != CAPTURE)
            {
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, ROOK);
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, BISHOP);
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, KNIGHT);
            }
        }
    }

    if (Type != CAPTURE)
    {
        Bitboard emptySquares = pos.empty_squares();
        dcPawns1 = dcPawns2 = EmptyBoardBB;
        if (Type == CHECK && (pawns & dcp))
        {
            // Pawn moves which gives discovered check. This is possible only if the
            // pawn is not on the same file as the enemy king, because we don't
            // generate captures.
            dcPawns1 = move_pawns<Us, DELTA_N>(pawns & dcp & ~file_bb(ksq)) & emptySquares & ~TRank8BB;
            dcPawns2 = move_pawns<Us, DELTA_N>(dcPawns1 & TRank3BB) & emptySquares;
        }

        // Single pawn pushes
        b1 = move_pawns<Us, DELTA_N>(pawns) & emptySquares & ~TRank8BB;
        b2 = (Type == CHECK ? (b1 & pos.attacks_from<PAWN>(ksq, Them)) | dcPawns1 :
             (Type == EVASION ? b1 & blockSquares : b1));
        SERIALIZE_MOVES_D(b2, -TDELTA_N);

        // Double pawn pushes
        b1 = move_pawns<Us, DELTA_N>(b1 & TRank3BB) & emptySquares;
        b2 = (Type == CHECK ? (b1 & pos.attacks_from<PAWN>(ksq, Them)) | dcPawns2 :
             (Type == EVASION ? b1 & blockSquares : b1));
        SERIALIZE_MOVES_D(b2, -TDELTA_N -TDELTA_N);
    }
    else if (pos.ep_square() != SQ_NONE) // En passant captures
    {
        assert(Us != WHITE || square_rank(pos.ep_square()) == RANK_6);
        assert(Us != BLACK || square_rank(pos.ep_square()) == RANK_3);

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

  template<PieceType Piece>
  MoveStack* generate_discovered_checks(const Position& pos, Square from, MoveStack* mlist) {

    assert(Piece != QUEEN);

    Bitboard b = pos.attacks_from<Piece>(from) & pos.empty_squares();
    if (Piece == KING)
    {
        Square ksq = pos.king_square(opposite_color(pos.side_to_move()));
        b &= ~QueenPseudoAttacks[ksq];
    }
    SERIALIZE_MOVES(b);
    return mlist;
  }

  template<PieceType Piece>
  MoveStack* generate_direct_checks(const Position& pos, MoveStack* mlist, Color us,
                                   Bitboard dc, Square ksq) {
    assert(Piece != KING);

    Square from;
    Bitboard checkSqs;
    const Square* ptr = pos.piece_list_begin(us, Piece);

    if ((from = *ptr++) == SQ_NONE)
        return mlist;

    checkSqs = pos.attacks_from<Piece>(ksq) & pos.empty_squares();

    do
    {
        if (   (Piece == QUEEN  && !(QueenPseudoAttacks[from]  & checkSqs))
            || (Piece == ROOK   && !(RookPseudoAttacks[from]   & checkSqs))
            || (Piece == BISHOP && !(BishopPseudoAttacks[from] & checkSqs)))
            continue;

        if (dc && bit_is_set(dc, from))
            continue;

        Bitboard bb = pos.attacks_from<Piece>(from) & checkSqs;
        SERIALIZE_MOVES(bb);

    } while ((from = *ptr++) != SQ_NONE);

    return mlist;
  }

  template<CastlingSide Side>
  MoveStack* generate_castle_moves(const Position& pos, MoveStack* mlist) {

    Color us = pos.side_to_move();

    if (  (Side == KING_SIDE && pos.can_castle_kingside(us))
        ||(Side == QUEEN_SIDE && pos.can_castle_queenside(us)))
    {
        Color them = opposite_color(us);
        Square ksq = pos.king_square(us);

        assert(pos.piece_on(ksq) == piece_of_color_and_type(us, KING));

        Square rsq = (Side == KING_SIDE ? pos.initial_kr_square(us) : pos.initial_qr_square(us));
        Square s1 = relative_square(us, Side == KING_SIDE ? SQ_G1 : SQ_C1);
        Square s2 = relative_square(us, Side == KING_SIDE ? SQ_F1 : SQ_D1);
        Square s;
        bool illegal = false;

        assert(pos.piece_on(rsq) == piece_of_color_and_type(us, ROOK));

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
            && (   pos.piece_on(relative_square(us, SQ_A1)) == piece_of_color_and_type(them, ROOK)
                || pos.piece_on(relative_square(us, SQ_A1)) == piece_of_color_and_type(them, QUEEN)))
            illegal = true;

        if (!illegal)
            (*mlist++).move = make_castle_move(ksq, rsq);
    }
    return mlist;
  }
}
