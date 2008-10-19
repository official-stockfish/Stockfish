/*
  Glaurung, a UCI chess playing engine.
  Copyright (C) 2004-2008 Tord Romstad

  Glaurung is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  Glaurung is distributed in the hope that it will be useful,
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

#include "movegen.h"


////
//// Local definitions
////

namespace {

  inline Bitboard forward_white(Bitboard b) { return b << 8; }
  inline Bitboard forward_right_white(Bitboard b) { return b << 9; }
  inline Bitboard forward_left_white(Bitboard b) { return b << 7; }

  inline Bitboard forward_black(Bitboard b) { return b >> 8; }
  inline Bitboard forward_right_black(Bitboard b) { return b >> 7; }
  inline Bitboard forward_left_black(Bitboard b) { return b >> 9; }

  struct PawnOffsets {

      Bitboard Rank3BB, Rank8BB;
      Rank RANK_8;
      SquareDelta DELTA_N, DELTA_NE, DELTA_NW;
      Color us, them;
      typedef Bitboard (*Shift_fn)(Bitboard b);
      Shift_fn forward, forward_left, forward_right;
  };

  const PawnOffsets WhitePawnOffsets = { Rank3BB, Rank8BB, RANK_8, DELTA_N, DELTA_NE, DELTA_NW, WHITE, BLACK,
                                         &forward_white, forward_left_white, forward_right_white };

  const PawnOffsets BlackPawnOffsets = { Rank6BB, Rank1BB, RANK_1, DELTA_S, DELTA_SE, DELTA_SW, BLACK, WHITE,
                                         &forward_black, &forward_left_black, &forward_right_black };
  
  int generate_pawn_captures(const PawnOffsets&, const Position&, MoveStack*);  
  int generate_pawn_noncaptures(const PawnOffsets&, const Position&, MoveStack*);
  int generate_pawn_checks(const PawnOffsets&, const Position&, Bitboard, Square, MoveStack*, int);
  int generate_piece_checks(PieceType, const Position&, Bitboard, Bitboard, Square, MoveStack*, int);
  int generate_piece_moves(PieceType, const Position&, MoveStack*, Color, Bitboard);
  int generate_castle_moves(const Position&, MoveStack*, Color);
  int generate_piece_blocking_evasions(PieceType, const Position&, Bitboard, Bitboard, MoveStack*, int);
  int generate_pawn_blocking_evasions(const PawnOffsets&, const Position&, Bitboard, Bitboard, MoveStack*, int);
}


////
//// Functions
////


/// generate_captures generates() all pseudo-legal captures and queen
/// promotions.  The return value is the number of moves generated.

int generate_captures(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Bitboard target = pos.pieces_of_color(opposite_color(us));
  int n;

  if (us == WHITE)
      n = generate_pawn_captures(WhitePawnOffsets, pos, mlist);
  else
      n = generate_pawn_captures(BlackPawnOffsets, pos, mlist);

  for (PieceType pce = KNIGHT; pce <= KING; pce++)
      n += generate_piece_moves(pce, pos, mlist+n, us, target);

  return n;
}


/// generate_noncaptures() generates all pseudo-legal non-captures and 
/// underpromotions.  The return value is the number of moves generated.

int generate_noncaptures(const Position& pos, MoveStack *mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Bitboard target = pos.empty_squares();
  int n;

  if (us == WHITE)
      n = generate_pawn_noncaptures(WhitePawnOffsets, pos, mlist);
  else
      n = generate_pawn_noncaptures(BlackPawnOffsets, pos, mlist);

  for (PieceType pce = KNIGHT; pce <= KING; pce++)
      n += generate_piece_moves(pce, pos, mlist+n, us, target);

  n += generate_castle_moves(pos, mlist+n, us);
  return n;
}


/// generate_checks() generates all pseudo-legal non-capturing, non-promoting
/// checks, except castling moves (will add this later).  It returns the
/// number of generated moves.  

int generate_checks(const Position& pos, MoveStack* mlist, Bitboard dc) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  int n;
  Color us = pos.side_to_move();
  Square ksq = pos.king_square(opposite_color(us));

  assert(pos.piece_on(ksq) == king_of_color(opposite_color(us)));

  dc = pos.discovered_check_candidates(us);

  // Pawn moves
  if (us == WHITE)    
     n = generate_pawn_checks(WhitePawnOffsets, pos, dc, ksq, mlist, 0);
  else
     n = generate_pawn_checks(BlackPawnOffsets, pos, dc, ksq, mlist, 0);

  // Pieces moves
  Bitboard b = pos.knights(us);
  if (b)
      n = generate_piece_checks(KNIGHT, pos, b, dc, ksq, mlist, n);

  b = pos.bishops(us);
  if (b)
      n = generate_piece_checks(BISHOP, pos, b, dc, ksq, mlist, n);

  b = pos.rooks(us);
  if (b)
      n = generate_piece_checks(ROOK, pos, b, dc, ksq, mlist, n);

  b = pos.queens(us);
  if (b)
      n = generate_piece_checks(QUEEN, pos, b, dc, ksq, mlist, n);

  // King moves
  Square from = pos.king_square(us);
  if (bit_is_set(dc, from))
  {
      b = pos.king_attacks(from) & pos.empty_squares() & ~QueenPseudoAttacks[ksq];
      while (b)
      {
          Square to = pop_1st_bit(&b);
          mlist[n++].move = make_move(from, to);
      }
  }

  // TODO: Castling moves!
  
  return n;
}


/// generate_evasions() generates all check evasions when the side to move is
/// in check.  Unlike the other move generation functions, this one generates
/// only legal moves.  It returns the number of generated moves.  This
/// function is very ugly, and needs cleaning up some time later.  FIXME

int generate_evasions(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(pos.is_check());

  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square ksq = pos.king_square(us);
  Square from, to;
  int n = 0;

  assert(pos.piece_on(ksq) == king_of_color(us));
  
  // Generate evasions for king
  Bitboard b1 = pos.king_attacks(ksq) & ~pos.pieces_of_color(us);
  Bitboard b2 = pos.occupied_squares();
  clear_bit(&b2, ksq);

  while (b1)
  {
    Square to = pop_1st_bit(&b1);

    // Make sure to is not attacked by the other side. This is a bit ugly,
    // because we can't use Position::square_is_attacked. Instead we use
    // the low-level bishop_attacks_bb and rook_attacks_bb with the bitboard
    // b2 (the occupied squares with the king removed) in order to test whether
    // the king will remain in check on the destination square.
    if (!(   (bishop_attacks_bb(to, b2)  & pos.bishops_and_queens(them))
          || (rook_attacks_bb(to, b2)    & pos.rooks_and_queens(them))
          || (pos.knight_attacks(to)     & pos.knights(them))
          || (pos.pawn_attacks(us, to)   & pos.pawns(them))
          || (pos.king_attacks(to)       & pos.kings(them))))

        mlist[n++].move = make_move(ksq, to);
  }

  // Generate evasions for other pieces only if not double check. We use a
  // simple bit twiddling hack here rather than calling count_1s in order to
  // save some time (we know that pos.checkers() has at most two nonzero bits).
  Bitboard checkers = pos.checkers();

  if (!(checkers & (checkers - 1))) // Only one bit set?
  {
      Square checksq = first_1(checkers);

      assert(pos.color_of_piece_on(checksq) == them);

      // Find pinned pieces
      Bitboard not_pinned = ~pos.pinned_pieces(us);

      // Generate captures of the checking piece

      // Pawn captures
      b1 = pos.pawn_attacks(them, checksq) & pos.pawns(us) & not_pinned;
      while (b1)
      {
          from = pop_1st_bit(&b1);
          if (relative_rank(us, checksq) == RANK_8)
          {
              mlist[n++].move = make_promotion_move(from, checksq, QUEEN);
              mlist[n++].move = make_promotion_move(from, checksq, ROOK);
              mlist[n++].move = make_promotion_move(from, checksq, BISHOP);
              mlist[n++].move = make_promotion_move(from, checksq, KNIGHT);
          } else
              mlist[n++].move = make_move(from, checksq);
      }

      // Pieces captures
      b1 =  (pos.knight_attacks(checksq) & pos.knights(us))
          | (pos.bishop_attacks(checksq) & pos.bishops_and_queens(us))
          | (pos.rook_attacks(checksq)   & pos.rooks_and_queens(us))
          & not_pinned;

      while (b1)
      {
          from = pop_1st_bit(&b1);
          mlist[n++].move = make_move(from, checksq);
      }

      // Blocking check evasions are possible only if the checking piece is
      // a slider
      if (checkers & pos.sliders())
      {
          Bitboard blockSquares = squares_between(checksq, ksq);

          assert((pos.occupied_squares() & blockSquares) == EmptyBoardBB);

          // Pawn moves. Because a blocking evasion can never be a capture, we
          // only generate pawn pushes.
          if (us == WHITE)
              n = generate_pawn_blocking_evasions(WhitePawnOffsets, pos, not_pinned, blockSquares, mlist, n);
          else
              n = generate_pawn_blocking_evasions(BlackPawnOffsets, pos, not_pinned, blockSquares, mlist, n);

          // Pieces moves
          b1 = pos.knights(us) & not_pinned;
          if (b1)
              n = generate_piece_blocking_evasions(KNIGHT, pos, b1, blockSquares, mlist, n);

          b1 = pos.bishops(us) & not_pinned;
          if (b1)
              n = generate_piece_blocking_evasions(BISHOP, pos, b1, blockSquares, mlist, n);

          b1 = pos.rooks(us) & not_pinned;
          if (b1)
              n = generate_piece_blocking_evasions(ROOK, pos, b1, blockSquares, mlist, n);

          b1 = pos.queens(us) & not_pinned;
          if (b1)
              n = generate_piece_blocking_evasions(QUEEN, pos, b1, blockSquares, mlist, n);
    }

    // Finally, the ugly special case of en passant captures. An en passant
    // capture can only be a check evasion if the check is not a discovered
    // check. If pos.ep_square() is set, the last move made must have been
    // a double pawn push. If, furthermore, the checking piece is a pawn,
    // an en passant check evasion may be possible.
    if (pos.ep_square() != SQ_NONE && (checkers & pos.pawns(them)))
    {
        to = pos.ep_square();
        b1 = pos.pawn_attacks(them, to) & pos.pawns(us);

        assert(b1 != EmptyBoardBB);
  
        b1 &= not_pinned;
        while (b1)
        {
            from = pop_1st_bit(&b1);

            // Before generating the move, we have to make sure it is legal.
            // This is somewhat tricky, because the two disappearing pawns may
            // cause new "discovered checks".  We test this by removing the
            // two relevant bits from the occupied squares bitboard, and using
            // the low-level bitboard functions for bishop and rook attacks.
            b2 = pos.occupied_squares();
            clear_bit(&b2, from);
            clear_bit(&b2, checksq);
            if (!(  (bishop_attacks_bb(ksq, b2) & pos.bishops_and_queens(them))
                  ||(rook_attacks_bb(ksq, b2)   & pos.rooks_and_queens(them))))
                
                 mlist[n++].move = make_ep_move(from, to);
        }
    }
  }
  return n;
}


/// generate_legal_moves() computes a complete list of legal moves in the
/// current position.  This function is not very fast, and should be used
/// only in situations where performance is unimportant.  It wouldn't be
/// very hard to write an efficient legal move generator, but for the moment
/// we don't need it.

int generate_legal_moves(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());

  if (pos.is_check())
      return generate_evasions(pos, mlist);

  // Generate pseudo-legal moves:
  int n = generate_captures(pos, mlist);
  n += generate_noncaptures(pos, mlist + n);

  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());

  // Remove illegal moves from the list:
  for (int i = 0; i < n; i++)
      if (!pos.move_is_legal(mlist[i].move, pinned))
          mlist[i--].move = mlist[--n].move;

  return n;
}


/// generate_move_if_legal() takes a position and a (not necessarily
/// pseudo-legal) move and a pinned pieces bitboard as input, and tests
/// whether the move is legal.  If the move is legal, the move itself is
/// returned.  If not, the function returns MOVE_NONE.  This function must
/// only be used when the side to move is not in check.

Move generate_move_if_legal(const Position &pos, Move m, Bitboard pinned) {

  assert(pos.is_ok());
  assert(!pos.is_check());
  assert(move_is_ok(m));

  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Piece pc = pos.piece_on(from);

  // If the from square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if (color_of_piece(pc) != us)
      return MOVE_NONE;

  Square to = move_to(m);

  // En passant moves
  if (move_is_ep(m))
  {
      // The piece must be a pawn and destination square must be the
      // en passant square.
      if (   type_of_piece(pc) != PAWN
          || to != pos.ep_square())
          return MOVE_NONE;

      assert(pos.square_is_empty(to));
      assert(pos.piece_on(to - pawn_push(us)) == pawn_of_color(them));

      // The move is pseudo-legal.  If it is legal, return it.
      return (pos.move_is_legal(m) ? m : MOVE_NONE);
  }

  // Castling moves
  if (move_is_short_castle(m))
  {
      // The piece must be a king and side to move must still have
      // the right to castle kingside.
      if (   type_of_piece(pc) != KING
          ||!pos.can_castle_kingside(us))
          return MOVE_NONE;

      assert(from == pos.king_square(us));
      assert(to == pos.initial_kr_square(us));
      assert(pos.piece_on(to) == rook_of_color(us));

      Square g1 = relative_square(us, SQ_G1);
      Square f1 = relative_square(us, SQ_F1);
      Square s;
      bool illegal = false;

      // Check if any of the squares between king and rook
      // is occupied or under attack.
      for (s = Min(from, g1); s <= Max(from, g1); s++)
          if (  (s != from && s != to && !pos.square_is_empty(s))
              || pos.square_is_attacked(s, them))
              illegal = true;

      // Check if any of the squares between king and rook
      // is occupied.
      for (s = Min(to, f1); s <= Max(to, f1); s++)
          if (s != from && s != to && !pos.square_is_empty(s))
              illegal = true;

      return (!illegal ? m : MOVE_NONE);
  }

  if (move_is_long_castle(m))
  {
      // The piece must be a king and side to move must still have
      // the right to castle kingside.
      if (   type_of_piece(pc) != KING
          ||!pos.can_castle_queenside(us))
          return MOVE_NONE;

      assert(from == pos.king_square(us));
      assert(to == pos.initial_qr_square(us));
      assert(pos.piece_on(to) == rook_of_color(us));

      Square c1 = relative_square(us, SQ_C1);
      Square d1 = relative_square(us, SQ_D1);
      Square s;
      bool illegal = false;

      for (s = Min(from, c1); s <= Max(from, c1); s++)
          if(  (s != from && s != to && !pos.square_is_empty(s))
             || pos.square_is_attacked(s, them))
              illegal = true;

      for (s = Min(to, d1); s <= Max(to, d1); s++)
          if(s != from && s != to && !pos.square_is_empty(s))
              illegal = true;

      if (   square_file(to) == FILE_B
          && (   pos.piece_on(to + DELTA_W) == rook_of_color(them)
              || pos.piece_on(to + DELTA_W) == queen_of_color(them)))
          illegal = true;

      return (!illegal ? m : MOVE_NONE);
  }

  // Normal moves

  // The destination square cannot be occupied by a friendly piece
  if (pos.color_of_piece_on(to) == us)
      return MOVE_NONE;

  // Proceed according to the type of the moving piece.
  if (type_of_piece(pc) == PAWN)
  {  
      // If the destination square is on the 8/1th rank, the move must
      // be a promotion.
      if (   (  (square_rank(to) == RANK_8 && us == WHITE)
              ||(square_rank(to) == RANK_1 && us != WHITE))
           && !move_promotion(m))
          return MOVE_NONE;

      // Proceed according to the square delta between the source and
      // destionation squares.
      switch (to - from)
      {
      case DELTA_NW:
      case DELTA_NE:
      case DELTA_SW:
      case DELTA_SE:
      // Capture. The destination square must be occupied by an enemy
      // piece (en passant captures was handled earlier).
          if (pos.color_of_piece_on(to) != them)
              return MOVE_NONE;
          break;

      case DELTA_N:
      case DELTA_S:
      // Pawn push. The destination square must be empty.
          if (!pos.square_is_empty(to))
              return MOVE_NONE;
          break;

      case DELTA_NN:
      // Double white pawn push. The destination square must be on the fourth
      // rank, and both the destination square and the square between the
      // source and destination squares must be empty.
      if (   square_rank(to) != RANK_4
          || !pos.square_is_empty(to)
          || !pos.square_is_empty(from + DELTA_N))
          return MOVE_NONE;
          break;

      case DELTA_SS:
      // Double black pawn push. The destination square must be on the fifth
      // rank, and both the destination square and the square between the
      // source and destination squares must be empty.
          if (   square_rank(to) != RANK_5
              || !pos.square_is_empty(to)
              || !pos.square_is_empty(from + DELTA_S))
              return MOVE_NONE;
          break;

      default:
          return MOVE_NONE;
      }
      // The move is pseudo-legal.  Return it if it is legal.
      return (pos.move_is_legal(m) ? m : MOVE_NONE);
  }

  // Luckly we can handle all the other pieces in one go
  return (   pos.piece_attacks_square(from, to)
          && pos.move_is_legal(m)
          && !move_promotion(m) ? m : MOVE_NONE);
}


namespace {

  int generate_pawn_captures(const PawnOffsets& ofs, const Position& pos, MoveStack* mlist) {

    Bitboard pawns = pos.pawns(ofs.us);
    Bitboard enemyPieces = pos.pieces_of_color(ofs.them);
    Square sq;
    int n = 0;

    // Captures in the a1-h8 (a8-h1 for black) direction
    Bitboard b1 = (ofs.forward_right)(pawns) & ~FileABB & enemyPieces;

    // Capturing promotions
    Bitboard b2 = b1 & ofs.Rank8BB;
    while (b2)
    {
        sq = pop_1st_bit(&b2);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_NE, sq, QUEEN);
    }

    // Capturing non-promotions
    b2 = b1 & ~ofs.Rank8BB;
    while (b2)
    {
        sq = pop_1st_bit(&b2);
        mlist[n++].move = make_move(sq - ofs.DELTA_NE, sq);
    }

    // Captures in the h1-a8 (h8-a1 for black) direction
    b1 = (ofs.forward_left)(pawns) & ~FileHBB & enemyPieces;

    // Capturing promotions
    b2 = b1 & ofs.Rank8BB;
    while (b2)
    {
        sq = pop_1st_bit(&b2);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_NW, sq, QUEEN);
    }

    // Capturing non-promotions
    b2 = b1 & ~ofs.Rank8BB;
    while (b2)
    {
        sq = pop_1st_bit(&b2);
        mlist[n++].move = make_move(sq - ofs.DELTA_NW, sq);
    }

    // Non-capturing promotions
    b1 = (ofs.forward)(pawns) & pos.empty_squares() & Rank8BB;
    while (b1)
    {
        sq = pop_1st_bit(&b1);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_N, sq, QUEEN);
    }

    // En passant captures
    if (pos.ep_square() != SQ_NONE)
    {
        assert(ofs.us != WHITE || square_rank(pos.ep_square()) == RANK_6);
        assert(ofs.us != BLACK || square_rank(pos.ep_square()) == RANK_3);

        b1 = pawns & pos.pawn_attacks(ofs.them, pos.ep_square());
        assert(b1 != EmptyBoardBB);

        while (b1)
        {
            sq = pop_1st_bit(&b1);
            mlist[n++].move = make_ep_move(sq, pos.ep_square());
        }
    }
    return n;
  }


  int generate_pawn_noncaptures(const PawnOffsets& ofs, const Position& pos, MoveStack* mlist) {

    Bitboard pawns = pos.pawns(ofs.us);
    Bitboard enemyPieces = pos.pieces_of_color(ofs.them);
    Bitboard emptySquares = pos.empty_squares();
    Bitboard b1, b2;
    Square sq;
    int n = 0;

    // Underpromotion captures in the a1-h8 (a8-h1 for black) direction
    b1 = ofs.forward_right(pawns) & ~FileABB & enemyPieces & ofs.Rank8BB;
    while (b1)
    {
        sq = pop_1st_bit(&b1);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_NE, sq, ROOK);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_NE, sq, BISHOP);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_NE, sq, KNIGHT);
    }

    // Underpromotion captures in the h1-a8 (h8-a1 for black) direction
    b1 = ofs.forward_left(pawns) & ~FileHBB & enemyPieces & ofs.Rank8BB;
    while (b1)
    {
        sq = pop_1st_bit(&b1);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_NW, sq, ROOK);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_NW, sq, BISHOP);
        mlist[n++].move = make_promotion_move(sq - ofs.DELTA_NW, sq, KNIGHT);
    }

    // Single pawn pushes
    b1 = ofs.forward(pawns) & emptySquares;
    b2 = b1 & ofs.Rank8BB;
    while (b2)
    {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_promotion_move(sq - ofs.DELTA_N, sq, ROOK);
      mlist[n++].move = make_promotion_move(sq - ofs.DELTA_N, sq, BISHOP);
      mlist[n++].move = make_promotion_move(sq - ofs.DELTA_N, sq, KNIGHT);
    }
    b2 = b1 & ~ofs.Rank8BB;
    while (b2)
    {
        sq = pop_1st_bit(&b2);
        mlist[n++].move = make_move(sq - ofs.DELTA_N, sq);
    }

    // Double pawn pushes
    b2 = (ofs.forward(b1 & ofs.Rank3BB)) & emptySquares;
    while (b2)
    {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - ofs.DELTA_N - ofs.DELTA_N, sq);
    }
    return n;
  }


  int generate_piece_moves(PieceType piece, const Position &pos, MoveStack *mlist, 
                           Color side, Bitboard target) {

    const Piece_attacks_fn mem_fn = piece_attacks_fn[piece];
    Square from, to;
    Bitboard b;
    int n = 0;

    for (int i = 0; i < pos.piece_count(side, piece); i++)
    {
        from = pos.piece_list(side, piece, i);
        b = (pos.*mem_fn)(from) & target;
        while (b)
        {
            to = pop_1st_bit(&b);
            mlist[n++].move = make_move(from, to);
        }
    }
    return n;
  }


  int generate_castle_moves(const Position &pos, MoveStack *mlist, Color us) {

    int n = 0;

    if (pos.can_castle(us))
    {
        Color them = opposite_color(us);
        Square ksq = pos.king_square(us);        

        assert(pos.piece_on(ksq) == king_of_color(us));

        if (pos.can_castle_kingside(us))
        {
            Square rsq = pos.initial_kr_square(us);
            Square g1 = relative_square(us, SQ_G1);
            Square f1 = relative_square(us, SQ_F1);
            Square s;
            bool illegal = false;

            assert(pos.piece_on(rsq) == rook_of_color(us));

            for (s = Min(ksq, g1); s <= Max(ksq, g1); s++)
                if (  (s != ksq && s != rsq && pos.square_is_occupied(s))
                    || pos.square_is_attacked(s, them))
                    illegal = true;

            for (s = Min(rsq, f1); s <= Max(rsq, f1); s++)
                if (s != ksq && s != rsq && pos.square_is_occupied(s))
                    illegal = true;

            if (!illegal)
                mlist[n++].move = make_castle_move(ksq, rsq);
      }

      if (pos.can_castle_queenside(us))
      {
          Square rsq = pos.initial_qr_square(us);
          Square c1 = relative_square(us, SQ_C1);
          Square d1 = relative_square(us, SQ_D1);
          Square s;
          bool illegal = false;        

          assert(pos.piece_on(rsq) == rook_of_color(us));

          for (s = Min(ksq, c1); s <= Max(ksq, c1); s++)
              if (  (s != ksq && s != rsq && pos.square_is_occupied(s))
                  || pos.square_is_attacked(s, them))
                  illegal = true;

          for (s = Min(rsq, d1); s <= Max(rsq, d1); s++)
              if (s != ksq && s != rsq && pos.square_is_occupied(s))
                  illegal = true;

        if (   square_file(rsq) == FILE_B
            && (   pos.piece_on(relative_square(us, SQ_A1)) == rook_of_color(them)
                || pos.piece_on(relative_square(us, SQ_A1)) == queen_of_color(them)))
            illegal = true;
                         
        if (!illegal)
            mlist[n++].move = make_castle_move(ksq, rsq);
      }
    }
    return n;
  }

  int generate_piece_checks(PieceType pce, const Position& pos, Bitboard target,
                          Bitboard dc, Square ksq, MoveStack* mlist, int n) {

    const Piece_attacks_fn mem_fn = piece_attacks_fn[pce];

    // Discovered checks
    Bitboard b = target & dc;
    while (b)
    {
        Square from = pop_1st_bit(&b);
        Bitboard bb = (pos.*mem_fn)(from) & pos.empty_squares();
        while (bb)
        {
            Square to = pop_1st_bit(&bb);
            mlist[n++].move = make_move(from, to);
        }
    }

    // Direct checks
    b = target & ~dc;
    Bitboard checkSqs = (pos.*mem_fn)(ksq) & pos.empty_squares();
    while (b)
    {
        Square from = pop_1st_bit(&b);
        Bitboard bb = (pos.*mem_fn)(from) & checkSqs;
        while (bb)
        {
            Square to = pop_1st_bit(&bb);
            mlist[n++].move = make_move(from, to);
        }
    }
    return n;
  }

  int generate_pawn_checks(const PawnOffsets& ofs, const Position& pos, Bitboard dc, Square ksq, MoveStack* mlist, int n)
  {
    // Pawn moves which give discovered check. This is possible only if the 
    // pawn is not on the same file as the enemy king, because we don't 
    // generate captures.
    Bitboard empty = pos.empty_squares();

    // Find all friendly pawns not on the enemy king's file
    Bitboard b1 = pos.pawns(pos.side_to_move()) & ~file_bb(ksq), b2, b3;

    // Discovered checks, single pawn pushes
    b2 = b3 = (ofs.forward)(b1 & dc) & ~ofs.Rank8BB & empty;
    while (b3)
    {
        Square to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(to - ofs.DELTA_N, to);
    }

    // Discovered checks, double pawn pushes
    b3 = (ofs.forward)(b2 & ofs.Rank3BB) & empty;
    while (b3)
    {
        Square to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(to - ofs.DELTA_N - ofs.DELTA_N, to);
    }

    // Direct checks. These are possible only for pawns on neighboring files
    // of the enemy king

    b1 &= (~dc & neighboring_files_bb(ksq)); // FIXME why ~dc ??

    // Direct checks, single pawn pushes
    b2 = (ofs.forward)(b1) & empty;
    b3 = b2 & pos.pawn_attacks(ofs.them, ksq);
    while (b3)
    {
        Square to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(to - ofs.DELTA_N, to);
    }

    // Direct checks, double pawn pushes
    b3 = (ofs.forward)(b2 & ofs.Rank3BB) & empty & pos.pawn_attacks(ofs.them, ksq);
    while (b3)
    {
        Square to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(to - ofs.DELTA_N - ofs.DELTA_N, to);
    }
    return n;
  }


   int generate_piece_blocking_evasions(PieceType pce, const Position& pos, Bitboard b,
                                        Bitboard blockSquares, MoveStack* mlist, int n) {

    const Piece_attacks_fn mem_fn = piece_attacks_fn[pce];

    while (b)
    {
        Square from = pop_1st_bit(&b);
        Bitboard bb = (pos.*mem_fn)(from) & blockSquares;
        while (bb)
        {
            Square to = pop_1st_bit(&bb);
            mlist[n++].move = make_move(from, to);
        }
    }
    return n;
  }


  int generate_pawn_blocking_evasions(const PawnOffsets& ofs, const Position& pos, Bitboard not_pinned,
                                      Bitboard blockSquares, MoveStack* mlist, int n) {
    // Find non-pinned pawns
    Bitboard b1 = pos.pawns(ofs.us) & not_pinned;

    // Single pawn pushes. We don't have to AND with empty squares here,
    // because the blocking squares will always be empty.
    Bitboard b2 = (ofs.forward)(b1) & blockSquares;
    while (b2)
    {
        Square to = pop_1st_bit(&b2);

        assert(pos.piece_on(to) == EMPTY);

        if (square_rank(to) == ofs.RANK_8)
        {
            mlist[n++].move = make_promotion_move(to - ofs.DELTA_N, to, QUEEN);
            mlist[n++].move = make_promotion_move(to - ofs.DELTA_N, to, ROOK);
            mlist[n++].move = make_promotion_move(to - ofs.DELTA_N, to, BISHOP);
            mlist[n++].move = make_promotion_move(to - ofs.DELTA_N, to, KNIGHT);
        } else
            mlist[n++].move = make_move(to - ofs.DELTA_N, to);
    }

    // Double pawn pushes
    b2 = (ofs.forward)((ofs.forward)(b1) & pos.empty_squares() & ofs.Rank3BB) & blockSquares;
    while (b2)
    {
        Square to = pop_1st_bit(&b2);

        assert(pos.piece_on(to) == EMPTY);
        assert(ofs.us != WHITE || square_rank(to) == RANK_4);
        assert(ofs.us != BLACK || square_rank(to) == RANK_5);

        mlist[n++].move = make_move(to - ofs.DELTA_N - ofs.DELTA_N, to);
    }
    return n;
  }
}
