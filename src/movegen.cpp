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

  enum CastlingSide {
    KING_SIDE,
    QUEEN_SIDE
  };

  enum MoveType {
    CAPTURE,
    NON_CAPTURE
  };

  // Functions
  bool castling_is_check(const Position&, CastlingSide);

  // Helper templates
  template<CastlingSide Side>
  MoveStack* generate_castle_moves(const Position& pos, MoveStack* mlist);

  template<Color Us, Rank, Bitboard, SquareDelta>
  MoveStack* generate_pawn_blocking_evasions(const Position&, Bitboard, Bitboard, MoveStack*);

  template<Color, Color, Bitboard, SquareDelta, SquareDelta, SquareDelta>
  MoveStack* generate_pawn_captures(const Position& pos, MoveStack* mlist);

  template<Color, Color, Bitboard, Bitboard, SquareDelta, SquareDelta, SquareDelta>
  MoveStack* generate_pawn_noncaptures(const Position& pos, MoveStack* mlist);

  template<Color, Color, Bitboard, Bitboard, SquareDelta>
  MoveStack* generate_pawn_checks(const Position&, Bitboard, Square, MoveStack*);

  // Template generate_piece_checks() with specializations
  template<PieceType>
  MoveStack* generate_piece_checks(const Position&, MoveStack*, Color, Bitboard, Square);

  template<>
  inline MoveStack* generate_piece_checks<PAWN>(const Position& p, MoveStack* m, Color us, Bitboard dc, Square ksq) {

    if (us == WHITE)
        return generate_pawn_checks<WHITE, BLACK, Rank8BB, Rank3BB, DELTA_N>(p, dc, ksq, m);
    else
        return generate_pawn_checks<BLACK, WHITE, Rank1BB, Rank6BB, DELTA_S>(p, dc, ksq, m);

  }

  // Template generate_piece_moves() with specializations and overloads
  template<PieceType>
  MoveStack* generate_piece_moves(const Position&, MoveStack*, Color us, Bitboard);

  template<>
  MoveStack* generate_piece_moves<KING>(const Position&, MoveStack*, Color, Bitboard);

  template<PieceType Piece, MoveType Type>
  inline MoveStack* generate_piece_moves(const Position& p, MoveStack* m, Color us) {

      assert(Piece == PAWN);

      if (Type == CAPTURE)
          return (us == WHITE ? generate_pawn_captures<WHITE, BLACK, Rank8BB, DELTA_NE, DELTA_NW, DELTA_N>(p, m)
                              : generate_pawn_captures<BLACK, WHITE, Rank1BB, DELTA_SE, DELTA_SW, DELTA_S>(p, m));
      else
          return (us == WHITE ? generate_pawn_noncaptures<WHITE, BLACK, Rank8BB, Rank3BB, DELTA_NE, DELTA_NW, DELTA_N>(p, m)
                              : generate_pawn_noncaptures<BLACK, WHITE, Rank1BB, Rank6BB, DELTA_SE, DELTA_SW, DELTA_S>(p, m));
  }

  template<PieceType>
  MoveStack* generate_piece_moves(const Position&, MoveStack*, Color us, Bitboard, Bitboard);

  template<>
  inline MoveStack* generate_piece_moves<PAWN>(const Position& p, MoveStack* m,
                                               Color us, Bitboard t, Bitboard pnd) {
    if (us == WHITE)
        return generate_pawn_blocking_evasions<WHITE, RANK_8, Rank3BB, DELTA_N>(p, pnd, t, m);
    else
        return generate_pawn_blocking_evasions<BLACK, RANK_1, Rank6BB, DELTA_S>(p, pnd, t, m);
  }
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

  mlist = generate_piece_moves<QUEEN>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<PAWN, CAPTURE>(pos, mlist, us);
  mlist = generate_piece_moves<KING>(pos, mlist, us, target);
  return int(mlist - mlist_start);
}


/// generate_noncaptures() generates all pseudo-legal non-captures and
/// underpromotions. The return value is the number of moves generated.

int generate_noncaptures(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Bitboard target = pos.empty_squares();
  MoveStack* mlist_start = mlist;

  mlist = generate_piece_moves<PAWN, NON_CAPTURE>(pos, mlist, us);
  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  mlist = generate_piece_moves<QUEEN>(pos, mlist, us, target);
  mlist = generate_piece_moves<KING>(pos, mlist, us, target);
  mlist = generate_castle_moves<KING_SIDE>(pos, mlist);
  mlist = generate_castle_moves<QUEEN_SIDE>(pos, mlist);
  return int(mlist - mlist_start);
}


/// generate_checks() generates all pseudo-legal non-capturing, non-promoting
/// checks. It returns the number of generated moves.

int generate_checks(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Square ksq = pos.king_square(opposite_color(us));
  Bitboard dc = pos.discovered_check_candidates(us);
  MoveStack* mlist_start = mlist;

  assert(pos.piece_on(ksq) == piece_of_color_and_type(opposite_color(us), KING));

  // Pieces moves
  mlist = generate_piece_checks<PAWN>(pos, mlist, us, dc, ksq);
  mlist = generate_piece_checks<KNIGHT>(pos, mlist, us, dc, ksq);
  mlist = generate_piece_checks<BISHOP>(pos, mlist, us, dc, ksq);
  mlist = generate_piece_checks<ROOK>(pos, mlist, us, dc, ksq);
  mlist = generate_piece_checks<QUEEN>(pos, mlist, us, dc, ksq);
  mlist = generate_piece_checks<KING>(pos, mlist, us, dc, ksq);

  // Castling moves that give check. Very rare but nice to have!
  if (   pos.can_castle_queenside(us)
      && (square_rank(ksq) == square_rank(pos.king_square(us)) || square_file(ksq) == FILE_D)
      && castling_is_check(pos, QUEEN_SIDE))
      mlist = generate_castle_moves<QUEEN_SIDE>(pos, mlist);

  if (   pos.can_castle_kingside(us)
      && (square_rank(ksq) == square_rank(pos.king_square(us)) || square_file(ksq) == FILE_F)
      && castling_is_check(pos, KING_SIDE))
      mlist = generate_castle_moves<KING_SIDE>(pos, mlist);

  return int(mlist - mlist_start);
}


/// generate_evasions() generates all check evasions when the side to move is
/// in check. Unlike the other move generation functions, this one generates
/// only legal moves. It returns the number of generated moves.

int generate_evasions(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(pos.is_check());

  Square from, to;
  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square ksq = pos.king_square(us);
  MoveStack* mlist_start = mlist;

  assert(pos.piece_on(ksq) == piece_of_color_and_type(us, KING));

  // The bitboard of occupied pieces without our king
  Bitboard b_noKing = pos.occupied_squares();
  clear_bit(&b_noKing, ksq);

  // Find squares attacked by slider checkers, we will
  // remove them from king evasions set so to avoid a couple
  // of cycles in the slow king evasions legality check loop
  // and to be able to use square_is_attacked().
  Bitboard checkers = pos.checkers();
  Bitboard checkersAttacks = EmptyBoardBB;
  Bitboard b = checkers & (pos.queens() | pos.bishops());
  while (b)
  {
      from = pop_1st_bit(&b);
      checkersAttacks |= bishop_attacks_bb(from, b_noKing);
  }

  b = checkers & (pos.queens() | pos.rooks());
  while (b)
  {
      from = pop_1st_bit(&b);
      checkersAttacks |= rook_attacks_bb(from, b_noKing);
  }

  // Generate evasions for king
  Bitboard b1 = pos.piece_attacks<KING>(ksq) & ~pos.pieces_of_color(us) & ~checkersAttacks;
  while (b1)
  {
      to = pop_1st_bit(&b1);
      // Note that we can use square_is_attacked() only because we
      // have already removed slider checkers.
      if (!pos.square_is_attacked(to, them))
          (*mlist++).move = make_move(ksq, to);
  }

  // Generate evasions for other pieces only if not double check. We use a
  // simple bit twiddling hack here rather than calling count_1s in order to
  // save some time (we know that pos.checkers() has at most two nonzero bits).
  if (!(checkers & (checkers - 1))) // Only one bit set?
  {
      Square checksq = first_1(checkers);
      Bitboard pinned = pos.pinned_pieces(us);

      assert(pos.color_of_piece_on(checksq) == them);

      // Generate captures of the checking piece

      // Pawn captures
      b1 = pos.pawn_attacks(them, checksq) & pos.pawns(us) & ~pinned;
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
            | (pos.piece_attacks<ROOK>(checksq)   & pos.rooks_and_queens(us)) ) & ~pinned;

      while (b1)
      {
          from = pop_1st_bit(&b1);
          (*mlist++).move = make_move(from, checksq);
      }

      // Blocking check evasions are possible only if the checking piece is
      // a slider.
      if (checkers & pos.sliders())
      {
          Bitboard blockSquares = squares_between(checksq, ksq);

          assert((pos.occupied_squares() & blockSquares) == EmptyBoardBB);

          if (blockSquares != EmptyBoardBB)
          {
              mlist = generate_piece_moves<PAWN>(pos, mlist, us, blockSquares, pinned);
              mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, blockSquares, pinned);
              mlist = generate_piece_moves<BISHOP>(pos, mlist, us, blockSquares, pinned);
              mlist = generate_piece_moves<ROOK>(pos, mlist, us, blockSquares, pinned);
              mlist = generate_piece_moves<QUEEN>(pos, mlist, us, blockSquares, pinned);
          }
      }

      // Finally, the special case of en passant captures. An en passant
      // capture can only be a check evasion if the check is not a discovered
      // check. If pos.ep_square() is set, the last move made must have been
      // a double pawn push. If, furthermore, the checking piece is a pawn,
      // an en passant check evasion may be possible.
      if (pos.ep_square() != SQ_NONE && (checkers & pos.pawns(them)))
      {
          to = pos.ep_square();
          b1 = pos.pawn_attacks(them, to) & pos.pawns(us);

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
  }
  return int(mlist - mlist_start);
}


/// generate_legal_moves() computes a complete list of legal moves in the
/// current position. This function is not very fast, and should be used
/// only in situations where performance is unimportant. It wouldn't be
/// very hard to write an efficient legal move generator, but for the moment
/// we don't need it.

int generate_legal_moves(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());

  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());

  if (pos.is_check())
      return generate_evasions(pos, mlist);

  // Generate pseudo-legal moves
  int n = generate_captures(pos, mlist);
  n += generate_noncaptures(pos, mlist + n);

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

bool move_is_legal(const Position& pos, const Move m) {

  assert(pos.is_ok());
  assert(!pos.is_check());
  assert(move_is_ok(m));

  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Piece pc = pos.piece_on(from);
  Bitboard pinned = pos.pinned_pieces(us);

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
      assert(pos.piece_on(to - pawn_push(us)) == piece_of_color_and_type(them, PAWN));

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
      assert(pos.piece_on(to) == piece_of_color_and_type(us, ROOK));

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
      assert(pos.piece_on(to) == piece_of_color_and_type(us, ROOK));

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
          && (   pos.piece_on(to + DELTA_W) == piece_of_color_and_type(them, ROOK)
              || pos.piece_on(to + DELTA_W) == piece_of_color_and_type(them, QUEEN)))
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
  return (   pos.piece_attacks_square(pos.piece_on(from), from, to)
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

  template<PieceType Piece>
  MoveStack* generate_piece_moves(const Position& pos, MoveStack* mlist,
                                  Color us, Bitboard target, Bitboard pinned) {
    Square from;
    Bitboard b;

    for (int i = 0, e = pos.piece_count(us, Piece); i < e; i++)
    {
        from = pos.piece_list(us, Piece, i);
        if (pinned && bit_is_set(pinned, from))
            continue;

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

  template<Color Us, Color Them, Bitboard TRank8BB, SquareDelta TDELTA_NE,
           SquareDelta TDELTA_NW, SquareDelta TDELTA_N
          >
  MoveStack* generate_pawn_captures(const Position& pos, MoveStack* mlist) {

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
  MoveStack* generate_pawn_noncaptures(const Position& pos, MoveStack* mlist) {

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
  MoveStack* generate_pawn_checks(const Position& pos, Bitboard dc, Square ksq, MoveStack* mlist)
  {
    // Find all friendly pawns not on the enemy king's file
    Bitboard b1, b2, b3;
    Bitboard empty = pos.empty_squares();

    if (dc != EmptyBoardBB)
    {
        // Pawn moves which gives discovered check. This is possible only if the
        // pawn is not on the same file as the enemy king, because we don't
        // generate captures.
        b1 = pos.pawns(Us) & ~file_bb(ksq);

        // Discovered checks, single pawn pushes, no promotions
        b2 = b3 = (Us == WHITE ? (b1 & dc) << 8 : (b1 & dc) >> 8) & empty & ~TRank8BB;
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
    }

    // Direct checks. These are possible only for pawns on neighboring files
    // of the enemy king.
    b1 = pos.pawns(Us) & neighboring_files_bb(ksq) & ~dc;

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
  MoveStack* generate_piece_checks(const Position& pos, MoveStack* mlist, Color us,
                                   Bitboard dc, Square ksq) {

    Bitboard target = pos.pieces_of_color_and_type(us, Piece);

    // Discovered checks
    Bitboard b = target & dc;
    while (b)
    {
        Square from = pop_1st_bit(&b);
        Bitboard bb = pos.piece_attacks<Piece>(from) & pos.empty_squares();
        if (Piece == KING)
            bb &= ~QueenPseudoAttacks[ksq];

        SERIALIZE_MOVES(bb);
    }

    // Direct checks
    b = target & ~dc;
    if (Piece == KING || !b)
        return mlist;

    Bitboard checkSqs = pos.piece_attacks<Piece>(ksq) & pos.empty_squares();
    while (b)
    {
        Square from = pop_1st_bit(&b);
        Bitboard bb = pos.piece_attacks<Piece>(from) & checkSqs;
        SERIALIZE_MOVES(bb);
    }
    return mlist;
  }

  template<Color Us, Rank TRANK_8, Bitboard TRank3BB, SquareDelta TDELTA_N>
  MoveStack* generate_pawn_blocking_evasions(const Position& pos, Bitboard pinned,
                                             Bitboard blockSquares, MoveStack* mlist) {
    Square to;

    // Find non-pinned pawns
    Bitboard b1 = pos.pawns(Us) & ~pinned;

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
    b2 = (Us == WHITE ? b2 << 8 : b2 >> 8) & blockSquares;
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
                || pos.square_is_attacked(s, them))
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

  bool castling_is_check(const Position& pos, CastlingSide side) {

    // After castling opponent king is attacked by the castled rook?
    File rookFile = (side == QUEEN_SIDE ? FILE_D : FILE_F);
    Color us = pos.side_to_move();
    Square ksq = pos.king_square(us);
    Bitboard occ = pos.occupied_squares();

    clear_bit(&occ, ksq); // Remove our king from the board
    Square rsq = make_square(rookFile, square_rank(ksq));
    return bit_is_set(rook_attacks_bb(rsq, occ), pos.king_square(opposite_color(us)));
  }
}
