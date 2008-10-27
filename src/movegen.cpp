/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author) Copyright (C) 2008 Marco Costalba

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

#include "movegen.h"

// Simple macro to wrap a very common while loop, no facny, no flexibility,
// hardcoded list name 'mlist' and from square 'from'.
#define SERIALIZE_MOVES(b) while (b) (*mlist++).move = make_move(from, pop_1st_bit(&b))

////
//// Local definitions
////

namespace {

  // Function
  MoveStack* generate_castle_moves(const Position&, MoveStack*);

  // Template generate_pawn_captures() with specializations
  template<Color, Color, Bitboard, SquareDelta, SquareDelta, SquareDelta>
  MoveStack* do_generate_pawn_captures(const Position& pos, MoveStack* mlist);

  template<Color>
  inline MoveStack* generate_pawn_captures(const Position& p, MoveStack* m) {
      return do_generate_pawn_captures<WHITE, BLACK, Rank8BB, DELTA_NE, DELTA_NW, DELTA_N>(p, m);
  }
  template<>
  inline MoveStack* generate_pawn_captures<BLACK>(const Position& p, MoveStack* m) {
      return do_generate_pawn_captures<BLACK, WHITE, Rank1BB, DELTA_SE, DELTA_SW, DELTA_S>(p, m);
  }

  // Template generate_pawn_noncaptures() with specializations
  template<Color, Color, Bitboard, Bitboard, SquareDelta, SquareDelta, SquareDelta>
  MoveStack* do_generate_pawn_noncaptures(const Position& pos, MoveStack* mlist);

  template<Color>
  inline MoveStack* generate_pawn_noncaptures(const Position& p, MoveStack* m) {
      return do_generate_pawn_noncaptures<WHITE, BLACK, Rank8BB, Rank3BB, DELTA_NE, DELTA_NW, DELTA_N>(p, m);
  }
  template<>
  inline MoveStack* generate_pawn_noncaptures<BLACK>(const Position& p, MoveStack* m) {
      return do_generate_pawn_noncaptures<BLACK, WHITE, Rank1BB, Rank6BB, DELTA_SE, DELTA_SW, DELTA_S>(p, m);
  }

  // Template generate_pawn_blocking_evasions() with specializations
  template<Color Us, Rank, Bitboard, SquareDelta>
  MoveStack* do_generate_pawn_blocking_evasions(const Position& pos, Bitboard not_pinned,
                                                Bitboard blockSquares, MoveStack* mlist);
  template<Color>
  inline MoveStack* generate_pawn_blocking_evasions(const Position& p, Bitboard np, Bitboard bs, MoveStack* m) {
      return do_generate_pawn_blocking_evasions<WHITE, RANK_8, Rank3BB, DELTA_N>(p, np, bs, m);
  }
  template<>
  inline MoveStack* generate_pawn_blocking_evasions<BLACK>(const Position& p, Bitboard np, Bitboard bs, MoveStack* m) {
      return do_generate_pawn_blocking_evasions<BLACK, RANK_1, Rank6BB, DELTA_S>(p, np, bs, m);
  }

  // Template generate_pawn_checks() with specializations
  template<Color, Color, Bitboard, Bitboard, SquareDelta>
  MoveStack* do_generate_pawn_checks(const Position&, Bitboard, Square, MoveStack*);

  template<Color>
  inline MoveStack* generate_pawn_checks(const Position& p, Bitboard dc, Square ksq, MoveStack* m) {
      return do_generate_pawn_checks<WHITE, BLACK, Rank8BB, Rank3BB, DELTA_N>(p, dc, ksq, m);
  }
  template<>
  inline MoveStack* generate_pawn_checks<BLACK>(const Position& p, Bitboard dc, Square ksq, MoveStack* m) {
      return do_generate_pawn_checks<BLACK, WHITE, Rank1BB, Rank6BB, DELTA_S>(p, dc, ksq, m);
  }

  // non-pawn templates
  template<PieceType>
  MoveStack* generate_piece_moves(const Position&, MoveStack*, Color us, Bitboard);
  template<>
  MoveStack* generate_piece_moves<KING>(const Position& pos, MoveStack* mlist, Color us, Bitboard target);

  template<PieceType>
  MoveStack* generate_piece_checks(const Position&, Bitboard, Bitboard, Square, MoveStack*);
  MoveStack* generate_piece_checks_king(const Position&, Square, Bitboard, Square, MoveStack*);

  template<PieceType>
  MoveStack* generate_piece_blocking_evasions(const Position&, Bitboard, Bitboard, MoveStack*);
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
  MoveStack* mlist_start = mlist;

  if (us == WHITE)
      mlist = generate_pawn_captures<WHITE>(pos, mlist);
  else
      mlist = generate_pawn_captures<BLACK>(pos, mlist);

  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  mlist = generate_piece_moves<QUEEN>(pos, mlist, us, target);
  mlist = generate_piece_moves<KING>(pos, mlist, us, target);
  return int(mlist - mlist_start);
}


/// generate_noncaptures() generates all pseudo-legal non-captures and
/// underpromotions.  The return value is the number of moves generated.

int generate_noncaptures(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Bitboard target = pos.empty_squares();
  MoveStack* mlist_start = mlist;

  if (us == WHITE)
      mlist = generate_pawn_noncaptures<WHITE>(pos, mlist);
  else
      mlist = generate_pawn_noncaptures<BLACK>(pos, mlist);

  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  mlist = generate_piece_moves<QUEEN>(pos, mlist, us, target);
  mlist = generate_piece_moves<KING>(pos, mlist, us, target);
  mlist = generate_castle_moves(pos, mlist);
  return int(mlist - mlist_start);
}


/// generate_checks() generates all pseudo-legal non-capturing, non-promoting
/// checks, except castling moves (will add this later).  It returns the
/// number of generated moves.

int generate_checks(const Position& pos, MoveStack* mlist, Bitboard dc) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Square ksq = pos.king_square(opposite_color(us));
  MoveStack* mlist_start = mlist;

  assert(pos.piece_on(ksq) == king_of_color(opposite_color(us)));

  dc = pos.discovered_check_candidates(us);

  // Pawn moves
  if (us == WHITE)
     mlist = generate_pawn_checks<WHITE>(pos, dc, ksq, mlist);
  else
     mlist = generate_pawn_checks<BLACK>(pos, dc, ksq, mlist);

  // Pieces moves
  Bitboard b = pos.knights(us);
  if (b)
      mlist = generate_piece_checks<KNIGHT>(pos, b, dc, ksq, mlist);

  b = pos.bishops(us);
  if (b)
      mlist = generate_piece_checks<BISHOP>(pos, b, dc, ksq, mlist);

  b = pos.rooks(us);
  if (b)
      mlist = generate_piece_checks<ROOK>(pos, b, dc, ksq, mlist);

  b = pos.queens(us);
  if (b)
      mlist = generate_piece_checks<QUEEN>(pos, b, dc, ksq, mlist);

  // Hopefully we always have a king ;-)
  mlist = generate_piece_checks_king(pos, pos.king_square(us), dc, ksq, mlist);

  // TODO: Castling moves!

  return int(mlist - mlist_start);
}


/// generate_evasions() generates all check evasions when the side to move is
/// in check.  Unlike the other move generation functions, this one generates
/// only legal moves.  It returns the number of generated moves.  This
/// function is very ugly, and needs cleaning up some time later.  FIXME

int generate_evasions(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(pos.is_check());

  Square from, to;
  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square ksq = pos.king_square(us);
  MoveStack* mlist_start = mlist;

  assert(pos.piece_on(ksq) == king_of_color(us));

  // Generate evasions for king
  Bitboard b1 = pos.piece_attacks<KING>(ksq) & ~pos.pieces_of_color(us);
  Bitboard b2 = pos.occupied_squares();
  clear_bit(&b2, ksq);

  while (b1)
  {
    to = pop_1st_bit(&b1);

    // Make sure 'to' is not attacked by the other side. This is a bit ugly,
    // because we can't use Position::square_is_attacked. Instead we use
    // the low-level bishop_attacks_bb and rook_attacks_bb with the bitboard
    // b2 (the occupied squares with the king removed) in order to test whether
    // the king will remain in check on the destination square.
    if (!(   (pos.piece_attacks<KNIGHT>(to) & pos.knights(them))
          || (pos.pawn_attacks(us, to)      & pos.pawns(them))
          || (bishop_attacks_bb(to, b2)     & pos.bishops_and_queens(them))
          || (rook_attacks_bb(to, b2)       & pos.rooks_and_queens(them))
          || (pos.piece_attacks<KING>(to)   & pos.kings(them))))

        (*mlist++).move = make_move(ksq, to);
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
              (*mlist++).move = make_promotion_move(from, checksq, QUEEN);
              (*mlist++).move = make_promotion_move(from, checksq, ROOK);
              (*mlist++).move = make_promotion_move(from, checksq, BISHOP);
              (*mlist++).move = make_promotion_move(from, checksq, KNIGHT);
          } else
              (*mlist++).move = make_move(from, checksq);
      }

      // Pieces captures
      b1 = (  (pos.piece_attacks<KNIGHT>(checksq) & pos.knights(us))
            | (pos.piece_attacks<BISHOP>(checksq) & pos.bishops_and_queens(us))
            | (pos.piece_attacks<ROOK>(checksq)   & pos.rooks_and_queens(us)) ) & not_pinned;

      while (b1)
      {
          from = pop_1st_bit(&b1);
          (*mlist++).move = make_move(from, checksq);
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
              mlist = generate_pawn_blocking_evasions<WHITE>(pos, not_pinned, blockSquares, mlist);
          else
              mlist = generate_pawn_blocking_evasions<BLACK>(pos, not_pinned, blockSquares, mlist);

          // Pieces moves
          b1 = pos.knights(us) & not_pinned;
          if (b1)
              mlist = generate_piece_blocking_evasions<KNIGHT>(pos, b1, blockSquares, mlist);

          b1 = pos.bishops(us) & not_pinned;
          if (b1)
              mlist = generate_piece_blocking_evasions<BISHOP>(pos, b1, blockSquares, mlist);

          b1 = pos.rooks(us) & not_pinned;
          if (b1)
              mlist = generate_piece_blocking_evasions<ROOK>(pos, b1, blockSquares, mlist);

          b1 = pos.queens(us) & not_pinned;
          if (b1)
              mlist = generate_piece_blocking_evasions<QUEEN>(pos, b1, blockSquares, mlist);
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

                 (*mlist++).move = make_ep_move(from, to);
        }
    }
  }
  return int(mlist - mlist_start);
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

  // Generate pseudo-legal moves
  int n = generate_captures(pos, mlist);
  n += generate_noncaptures(pos, mlist + n);

  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());

  // Remove illegal moves from the list
  for (int i = 0; i < n; i++)
      if (!pos.pl_move_is_legal(mlist[i].move, pinned))
          mlist[i--].move = mlist[--n].move;

  return n;
}


/// move_is_legal() takes a position and a (not necessarily pseudo-legal)
/// move and a pinned pieces bitboard as input, and tests whether
/// the move is legal.  If the move is legal, the move itself is
/// returned. If not, the function returns false.  This function must
/// only be used when the side to move is not in check.

bool move_is_legal(const Position& pos, const Move m, Bitboard pinned) {

  assert(pos.is_ok());
  assert(!pos.is_check());
  assert(move_is_ok(m));
  assert(pinned == pos.pinned_pieces(pos.side_to_move()));

  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Piece pc = pos.piece_on(from);

  // If the from square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if (color_of_piece(pc) != us)
      return false;

  Square to = move_to(m);

  // En passant moves
  if (move_is_ep(m))
  {
      // The piece must be a pawn and destination square must be the
      // en passant square.
      if (   type_of_piece(pc) != PAWN
          || to != pos.ep_square())
          return false;

      assert(pos.square_is_empty(to));
      assert(pos.piece_on(to - pawn_push(us)) == pawn_of_color(them));

      // The move is pseudo-legal, check if it is also legal
      return pos.pl_move_is_legal(m, pinned);
  }

  // Castling moves
  if (move_is_short_castle(m))
  {
      // The piece must be a king and side to move must still have
      // the right to castle kingside.
      if (   type_of_piece(pc) != KING
          ||!pos.can_castle_kingside(us))
          return false;

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

      return !illegal;
  }

  if (move_is_long_castle(m))
  {
      // The piece must be a king and side to move must still have
      // the right to castle kingside.
      if (   type_of_piece(pc) != KING
          ||!pos.can_castle_queenside(us))
          return false;

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

      return !illegal;
  }

  // Normal moves

  // The destination square cannot be occupied by a friendly piece
  if (pos.color_of_piece_on(to) == us)
      return false;

  // Proceed according to the type of the moving piece.
  if (type_of_piece(pc) == PAWN)
  {
      // If the destination square is on the 8/1th rank, the move must
      // be a promotion.
      if (   (  (square_rank(to) == RANK_8 && us == WHITE)
              ||(square_rank(to) == RANK_1 && us != WHITE))
           && !move_promotion(m))
          return false;

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
  return (   pos.piece_attacks_square(from, to)
          && pos.pl_move_is_legal(m, pinned)
          && !move_promotion(m));
}


namespace {

  template<PieceType Piece>
  MoveStack* generate_piece_moves(const Position& pos, MoveStack* mlist, Color us, Bitboard target) {

    Square from;
    Bitboard b;

    for (int i = 0, e = pos.piece_count(us, Piece); i < e; i++)
    {
        from = pos.piece_list(us, Piece, i);
        b = pos.piece_attacks<Piece>(from) & target;
        SERIALIZE_MOVES(b);
    }
    return mlist;
  }

  template<>
  MoveStack* generate_piece_moves<KING>(const Position& pos, MoveStack* mlist, Color us, Bitboard target) {

    Bitboard b;
    Square from = pos.king_square(us);

    b = pos.piece_attacks<KING>(from) & target;
    SERIALIZE_MOVES(b);
    return mlist;
  }

  template<PieceType Piece>
  MoveStack* generate_piece_blocking_evasions(const Position& pos, Bitboard b,
                                              Bitboard blockSquares, MoveStack* mlist) {
    while (b)
    {
        Square from = pop_1st_bit(&b);
        Bitboard bb = pos.piece_attacks<Piece>(from) & blockSquares;
        SERIALIZE_MOVES(bb);
    }
    return mlist;
  }


  template<Color Us, Color Them, Bitboard TRank8BB, SquareDelta TDELTA_NE,
           SquareDelta TDELTA_NW, SquareDelta TDELTA_N
          >
  MoveStack* do_generate_pawn_captures(const Position& pos, MoveStack* mlist) {

    Square to;
    Bitboard pawns = pos.pawns(Us);
    Bitboard enemyPieces = pos.pieces_of_color(Them);

    // Captures in the a1-h8 (a8-h1 for black) direction
    Bitboard b1 = (Us == WHITE ? pawns << 9 : pawns >> 7) & ~FileABB & enemyPieces;

    // Capturing promotions
    Bitboard b2 = b1 & TRank8BB;
    while (b2)
    {
        to = pop_1st_bit(&b2);
        (*mlist++).move = make_promotion_move(to - TDELTA_NE, to, QUEEN);
    }

    // Capturing non-promotions
    b2 = b1 & ~TRank8BB;
    while (b2)
    {
        to = pop_1st_bit(&b2);
        (*mlist++).move = make_move(to - TDELTA_NE, to);
    }

    // Captures in the h1-a8 (h8-a1 for black) direction
    b1 = (Us == WHITE ? pawns << 7 : pawns >> 9) & ~FileHBB & enemyPieces;

    // Capturing promotions
    b2 = b1 & TRank8BB;
    while (b2)
    {
        to = pop_1st_bit(&b2);
        (*mlist++).move = make_promotion_move(to - TDELTA_NW, to, QUEEN);
    }

    // Capturing non-promotions
    b2 = b1 & ~TRank8BB;
    while (b2)
    {
        to = pop_1st_bit(&b2);
        (*mlist++).move = make_move(to - TDELTA_NW, to);
    }

    // Non-capturing promotions
    b1 = (Us == WHITE ? pawns << 8 : pawns >> 8) & pos.empty_squares() & TRank8BB;
    while (b1)
    {
        to = pop_1st_bit(&b1);
        (*mlist++).move = make_promotion_move(to - TDELTA_N, to, QUEEN);
    }

    // En passant captures
    if (pos.ep_square() != SQ_NONE)
    {
        assert(Us != WHITE || square_rank(pos.ep_square()) == RANK_6);
        assert(Us != BLACK || square_rank(pos.ep_square()) == RANK_3);

        b1 = pawns & pos.pawn_attacks(Them, pos.ep_square());
        assert(b1 != EmptyBoardBB);

        while (b1)
        {
            to = pop_1st_bit(&b1);
            (*mlist++).move = make_ep_move(to, pos.ep_square());
        }
    }
    return mlist;
  }

  template<Color Us, Color Them, Bitboard TRank8BB, Bitboard TRank3BB,
           SquareDelta TDELTA_NE, SquareDelta TDELTA_NW, SquareDelta TDELTA_N
          >
  MoveStack* do_generate_pawn_noncaptures(const Position& pos, MoveStack* mlist) {

    Bitboard pawns = pos.pawns(Us);
    Bitboard enemyPieces = pos.pieces_of_color(Them);
    Bitboard emptySquares = pos.empty_squares();
    Bitboard b1, b2;
    Square to;

    // Underpromotion captures in the a1-h8 (a8-h1 for black) direction
    b1 = (Us == WHITE ? pawns << 9 : pawns >> 7) & ~FileABB & enemyPieces & TRank8BB;
    while (b1)
    {
        to = pop_1st_bit(&b1);
        (*mlist++).move = make_promotion_move(to - TDELTA_NE, to, ROOK);
        (*mlist++).move = make_promotion_move(to - TDELTA_NE, to, BISHOP);
        (*mlist++).move = make_promotion_move(to - TDELTA_NE, to, KNIGHT);
    }

    // Underpromotion captures in the h1-a8 (h8-a1 for black) direction
    b1 = (Us == WHITE ? pawns << 7 : pawns >> 9) & ~FileHBB & enemyPieces & TRank8BB;
    while (b1)
    {
        to = pop_1st_bit(&b1);
        (*mlist++).move = make_promotion_move(to - TDELTA_NW, to, ROOK);
        (*mlist++).move = make_promotion_move(to - TDELTA_NW, to, BISHOP);
        (*mlist++).move = make_promotion_move(to - TDELTA_NW, to, KNIGHT);
    }

    // Single pawn pushes
    b1 = (Us == WHITE ? pawns << 8 : pawns >> 8) & emptySquares;
    b2 = b1 & TRank8BB;
    while (b2)
    {
      to = pop_1st_bit(&b2);
      (*mlist++).move = make_promotion_move(to - TDELTA_N, to, ROOK);
      (*mlist++).move = make_promotion_move(to - TDELTA_N, to, BISHOP);
      (*mlist++).move = make_promotion_move(to - TDELTA_N, to, KNIGHT);
    }
    b2 = b1 & ~TRank8BB;
    while (b2)
    {
        to = pop_1st_bit(&b2);
        (*mlist++).move = make_move(to - TDELTA_N, to);
    }

    // Double pawn pushes
    b2 = (Us == WHITE ? (b1 & TRank3BB) << 8 : (b1 & TRank3BB) >> 8) & emptySquares;
    while (b2)
    {
      to = pop_1st_bit(&b2);
      (*mlist++).move = make_move(to - TDELTA_N - TDELTA_N, to);
    }
    return mlist;
  }


  template<Color Us, Color Them, Bitboard TRank8BB, Bitboard TRank3BB, SquareDelta TDELTA_N>
  MoveStack* do_generate_pawn_checks(const Position& pos, Bitboard dc, Square ksq, MoveStack* mlist)
  {
    // Pawn moves which give discovered check. This is possible only if the
    // pawn is not on the same file as the enemy king, because we don't
    // generate captures.
    Bitboard empty = pos.empty_squares();

    // Find all friendly pawns not on the enemy king's file
    Bitboard b1 = pos.pawns(Us) & ~file_bb(ksq), b2, b3;

    // Discovered checks, single pawn pushes
    b2 = b3 = (Us == WHITE ? (b1 & dc) << 8 : (b1 & dc) >> 8) & ~TRank8BB & empty;
    while (b3)
    {
        Square to = pop_1st_bit(&b3);
        (*mlist++).move = make_move(to - TDELTA_N, to);
    }

    // Discovered checks, double pawn pushes
    b3 = (Us == WHITE ? (b2 & TRank3BB) << 8 : (b2 & TRank3BB) >> 8) & empty;
    while (b3)
    {
        Square to = pop_1st_bit(&b3);
        (*mlist++).move = make_move(to - TDELTA_N - TDELTA_N, to);
    }

    // Direct checks. These are possible only for pawns on neighboring files
    // of the enemy king

    b1 &= (~dc & neighboring_files_bb(ksq)); // FIXME why ~dc ??

    // Direct checks, single pawn pushes
    b2 = (Us == WHITE ? b1 << 8 : b1 >> 8) & empty;
    b3 = b2 & pos.pawn_attacks(Them, ksq);
    while (b3)
    {
        Square to = pop_1st_bit(&b3);
        (*mlist++).move = make_move(to - TDELTA_N, to);
    }

    // Direct checks, double pawn pushes
    b3 =  (Us == WHITE ? (b2 & TRank3BB) << 8 : (b2 & TRank3BB) >> 8)
        & empty
        & pos.pawn_attacks(Them, ksq);

    while (b3)
    {
        Square to = pop_1st_bit(&b3);
        (*mlist++).move = make_move(to - TDELTA_N - TDELTA_N, to);
    }
    return mlist;
  }

  template<PieceType Piece>
  MoveStack* generate_piece_checks(const Position& pos, Bitboard target, Bitboard dc,
                                   Square ksq, MoveStack* mlist) {
    // Discovered checks
    Bitboard b = target & dc;
    while (b)
    {
        Square from = pop_1st_bit(&b);
        Bitboard bb = pos.piece_attacks<Piece>(from) & pos.empty_squares();
        SERIALIZE_MOVES(bb);
    }
    // Direct checks
    b = target & ~dc;
    Bitboard checkSqs = pos.piece_attacks<Piece>(ksq) & pos.empty_squares();
    while (b)
    {
        Square from = pop_1st_bit(&b);
        Bitboard bb = pos.piece_attacks<Piece>(from) & checkSqs;
        SERIALIZE_MOVES(bb);
    }
    return mlist;
  }

  MoveStack* generate_piece_checks_king(const Position& pos, Square from, Bitboard dc,
                                        Square ksq, MoveStack* mlist) {
    if (bit_is_set(dc, from))
    {
        Bitboard b =   pos.piece_attacks<KING>(from)
                     & pos.empty_squares()
                     & ~QueenPseudoAttacks[ksq];
        SERIALIZE_MOVES(b);
    }
    return mlist;
  }


  template<Color Us, Rank TRANK_8, Bitboard TRank3BB, SquareDelta TDELTA_N>
  MoveStack* do_generate_pawn_blocking_evasions(const Position& pos, Bitboard not_pinned,
                                                Bitboard blockSquares, MoveStack* mlist) {
    Square to;

    // Find non-pinned pawns
    Bitboard b1 = pos.pawns(Us) & not_pinned;

    // Single pawn pushes. We don't have to AND with empty squares here,
    // because the blocking squares will always be empty.
    Bitboard b2 = (Us == WHITE ? b1 << 8 : b1 >> 8) & blockSquares;
    while (b2)
    {
        to = pop_1st_bit(&b2);

        assert(pos.piece_on(to) == EMPTY);

        if (square_rank(to) == TRANK_8)
        {
            (*mlist++).move = make_promotion_move(to - TDELTA_N, to, QUEEN);
            (*mlist++).move = make_promotion_move(to - TDELTA_N, to, ROOK);
            (*mlist++).move = make_promotion_move(to - TDELTA_N, to, BISHOP);
            (*mlist++).move = make_promotion_move(to - TDELTA_N, to, KNIGHT);
        } else
            (*mlist++).move = make_move(to - TDELTA_N, to);
    }

    // Double pawn pushes
    b2 = (Us == WHITE ? b1 << 8 : b1 >> 8) & pos.empty_squares() & TRank3BB;
    b2 = (Us == WHITE ? b2 << 8 : b2 >> 8) & blockSquares;;
    while (b2)
    {
        to = pop_1st_bit(&b2);

        assert(pos.piece_on(to) == EMPTY);
        assert(Us != WHITE || square_rank(to) == RANK_4);
        assert(Us != BLACK || square_rank(to) == RANK_5);

        (*mlist++).move = make_move(to - TDELTA_N - TDELTA_N, to);
    }
    return mlist;
  }


  MoveStack* generate_castle_moves(const Position& pos, MoveStack* mlist) {

    Color us = pos.side_to_move();

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
                (*mlist++).move = make_castle_move(ksq, rsq);
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
            (*mlist++).move = make_castle_move(ksq, rsq);
      }
    }
    return mlist;
  }

}
