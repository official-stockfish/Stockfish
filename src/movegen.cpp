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
  
  int generate_white_pawn_captures(const Position &pos, MoveStack *mlist);
  int generate_black_pawn_captures(const Position &pos, MoveStack *mlist);
  int generate_white_pawn_noncaptures(const Position &pos, MoveStack *mlist);
  int generate_black_pawn_noncaptures(const Position &pos, MoveStack *mlist);
  int generate_knight_moves(const Position &pos, MoveStack *mlist, 
                            Color side, Bitboard target);
  int generate_bishop_moves(const Position &pos, MoveStack *mlist, 
                            Color side, Bitboard target);
  int generate_rook_moves(const Position &pos, MoveStack *mlist, 
                          Color side, Bitboard target);
  int generate_queen_moves(const Position &pos, MoveStack *mlist, 
                           Color side, Bitboard target);
  int generate_king_moves(const Position &pos, MoveStack *mlist, 
                          Square from, Bitboard target);
  int generate_castle_moves(const Position &pos, MoveStack *mlist, Color us);

}


////
//// Functions
////


/// generate_captures generates() all pseudo-legal captures and queen
/// promotions.  The return value is the number of moves generated.

int generate_captures(const Position &pos, MoveStack *mlist) {
  Color us = pos.side_to_move();
  Bitboard target = pos.pieces_of_color(opposite_color(us));
  int n = 0;

  assert(pos.is_ok());
  assert(!pos.is_check());

  if(us == WHITE)
    n += generate_white_pawn_captures(pos, mlist);
  else
    n += generate_black_pawn_captures(pos, mlist);
  n += generate_knight_moves(pos, mlist+n, us, target);
  n += generate_bishop_moves(pos, mlist+n, us, target);
  n += generate_rook_moves(pos, mlist+n, us, target);
  n += generate_queen_moves(pos, mlist+n, us, target);
  n += generate_king_moves(pos, mlist+n, pos.king_square(us), target);

  return n;
}


/// generate_noncaptures() generates all pseudo-legal non-captures and 
/// underpromotions.  The return value is the number of moves generated.

int generate_noncaptures(const Position &pos, MoveStack *mlist) {
  Color us = pos.side_to_move();
  Bitboard target = pos.empty_squares();
  int n = 0;

  assert(pos.is_ok());
  assert(!pos.is_check());

  if(us == WHITE)
    n += generate_white_pawn_noncaptures(pos, mlist);
  else
    n += generate_black_pawn_noncaptures(pos, mlist);
  n += generate_knight_moves(pos, mlist+n, us, target);
  n += generate_bishop_moves(pos, mlist+n, us, target);
  n += generate_rook_moves(pos, mlist+n, us, target);
  n += generate_queen_moves(pos, mlist+n, us, target);
  n += generate_king_moves(pos, mlist+n, pos.king_square(us), target);
  n += generate_castle_moves(pos, mlist+n, us);

  return n;
}


/// generate_checks() generates all pseudo-legal non-capturing, non-promoting
/// checks, except castling moves (will add this later).  It returns the
/// number of generated moves.  

int generate_checks(const Position &pos, MoveStack *mlist, Bitboard dc) {
  Color us, them;
  Square ksq, from, to;
  Bitboard empty, checkSqs, b1, b2, b3;
  int n = 0;

  assert(pos.is_ok());
  assert(!pos.is_check());

  us = pos.side_to_move();
  them = opposite_color(us);

  ksq = pos.king_square(them);
  assert(pos.piece_on(ksq) == king_of_color(them));

  dc = pos.discovered_check_candidates(us);
  empty = pos.empty_squares();
  
  // Pawn moves.  This is somewhat messy, and we use separate code for white
  // and black, because we can't shift by negative numbers in C/C++.  :-(

  if(us == WHITE) {
    // Pawn moves which give discovered check.  This is possible only if the 
    // pawn is not on the same file as the enemy king, because we don't 
    // generate captures.

    // Find all friendly pawns not on the enemy king's file:
    b1 = pos.pawns(us) & ~file_bb(ksq);

    // Discovered checks, single pawn pushes:
    b2 = b3 = ((b1 & dc) << 8) & ~Rank8BB & empty;
    while(b3) {
      to = pop_1st_bit(&b3);
      mlist[n++].move = make_move(to - DELTA_N, to);
    }

    // Discovered checks, double pawn pushes:
    b3 = ((b2 & Rank3BB) << 8) & empty;
    while(b3) {
      to = pop_1st_bit(&b3);
      mlist[n++].move = make_move(to - DELTA_N - DELTA_N, to);
    }

    // Direct checks.  These are possible only for pawns on neighboring files
    // of the enemy king:

    b1 &= (~dc & neighboring_files_bb(ksq));

    // Direct checks, single pawn pushes:
    b2 = (b1 << 8) & empty;
    b3 = b2 & pos.black_pawn_attacks(ksq);
    while(b3) {
      to = pop_1st_bit(&b3);
      mlist[n++].move = make_move(to - DELTA_N, to);
    }

    // Direct checks, double pawn pushes:
    b3 = ((b2 & Rank3BB) << 8) & empty & pos.black_pawn_attacks(ksq);
    while(b3) {
      to = pop_1st_bit(&b3);
      mlist[n++].move = make_move(to - DELTA_N - DELTA_N, to);
    }
  }
  else { // (us == BLACK)

    // Pawn moves which give discovered check.  This is possible only if the 
    // pawn is not on the same file as the enemy king, because we don't 
    // generate captures.

    // Find all friendly pawns not on the enemy king's file:
    b1 = pos.pawns(us) & ~file_bb(ksq);

    // Discovered checks, single pawn pushes:
    b2 = b3 = ((b1 & dc) >> 8) & ~Rank1BB & empty;
    while(b3) {
      to = pop_1st_bit(&b3);
      mlist[n++].move = make_move(to - DELTA_S, to);
    }

    // Discovered checks, double pawn pushes:
    b3 = ((b2 & Rank6BB) >> 8) & empty;
    while(b3) {
      to = pop_1st_bit(&b3);
      mlist[n++].move = make_move(to - DELTA_S - DELTA_S, to);
    }

    // Direct checks.  These are possible only for pawns on neighboring files
    // of the enemy king:

    b1 &= (~dc & neighboring_files_bb(ksq));

    // Direct checks, single pawn pushes:
    b2 = (b1 >> 8) & empty;
    b3 = b2 & pos.white_pawn_attacks(ksq);
    while(b3) {
      to = pop_1st_bit(&b3);
      mlist[n++].move = make_move(to - DELTA_S, to);
    }

    // Direct checks, double pawn pushes:
    b3 = ((b2 & Rank6BB) >> 8) & empty & pos.black_pawn_attacks(ksq);
    while(b3) {
      to = pop_1st_bit(&b3);
      mlist[n++].move = make_move(to - DELTA_S - DELTA_S, to);
    }
  }

  // Knight moves
  b1 = pos.knights(us);
  if(b1) {
    // Discovered knight checks:
    b2 = b1 & dc;
    while(b2) {
      from = pop_1st_bit(&b2);
      b3 = pos.knight_attacks(from) & empty;
      while(b3) {
        to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(from, to);
      }
    }
    
    // Direct knight checks:
    b2 = b1 & ~dc;
    checkSqs = pos.knight_attacks(ksq) & empty;
    while(b2) {
      from = pop_1st_bit(&b2);
      b3 = pos.knight_attacks(from) & checkSqs;
      while(b3) {
        to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(from, to);
      }
    }
  }

  // Bishop moves
  b1 = pos.bishops(us);
  if(b1) {
    // Discovered bishop checks:
    b2 = b1 & dc;
    while(b2) {
      from = pop_1st_bit(&b2);
      b3 = pos.bishop_attacks(from) & empty;
      while(b3) {
        to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(from, to);
      }
    }
    
    // Direct bishop checks:
    b2 = b1 & ~dc;
    checkSqs = pos.bishop_attacks(ksq) & empty;
    while(b2) {
      from = pop_1st_bit(&b2);
      b3 = pos.bishop_attacks(from) & checkSqs;
      while(b3) {
        to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(from, to);
      }
    }
  }

  // Rook moves
  b1 = pos.rooks(us);
  if(b1) {
    // Discovered rook checks:
    b2 = b1 & dc;
    while(b2) {
      from = pop_1st_bit(&b2);
      b3 = pos.rook_attacks(from) & empty;
      while(b3) {
        to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(from, to);
      }
    }
    
    // Direct rook checks:
    b2 = b1 & ~dc;
    checkSqs = pos.rook_attacks(ksq) & empty;
    while(b2) {
      from = pop_1st_bit(&b2);
      b3 = pos.rook_attacks(from) & checkSqs;
      while(b3) {
        to = pop_1st_bit(&b3);
        mlist[n++].move = make_move(from, to);
      }
    }
  }

  // Queen moves
  b1 = pos.queens(us);
  if(b1) {
    // Discovered queen checks are impossible!

    // Direct queen checks:
    checkSqs = pos.queen_attacks(ksq) & empty;
    while(b1) {
      from = pop_1st_bit(&b1);
      b2 = pos.queen_attacks(from) & checkSqs;
      while(b2) {
        to = pop_1st_bit(&b2);
        mlist[n++].move = make_move(from, to);
      }
    }
  }

  // King moves
  from = pos.king_square(us);
  if(bit_is_set(dc, from)) {
    b1 = pos.king_attacks(from) & empty & ~QueenPseudoAttacks[ksq];
    while(b1) {
      to = pop_1st_bit(&b1);
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

int generate_evasions(const Position &pos, MoveStack *mlist) {
  Color us, them;
  Bitboard checkers = pos.checkers();
  Bitboard pinned, b1, b2;
  Square ksq, from, to;
  int n = 0;

  assert(pos.is_ok());
  assert(pos.is_check());

  us = pos.side_to_move();
  them = opposite_color(us);

  ksq = pos.king_square(us);
  assert(pos.piece_on(ksq) == king_of_color(us));
  
  // Generate evasions for king:
  b1 = pos.king_attacks(ksq) & ~pos.pieces_of_color(us);
  b2 = pos.occupied_squares();
  clear_bit(&b2, ksq);
  while(b1) {
    to = pop_1st_bit(&b1);

    // Make sure to is not attacked by the other side.  This is a bit ugly,
    // because we can't use Position::square_is_attacked.  Instead we use
    // the low-level bishop_attacks_bb and rook_attacks_bb with the bitboard
    // b2 (the occupied squares with the king removed) in order to test whether
    // the king will remain in check on the destination square.
    if(((pos.pawn_attacks(us, to) & pos.pawns(them)) == EmptyBoardBB) &&
       ((pos.knight_attacks(to) & pos.knights(them)) == EmptyBoardBB) &&
       ((pos.king_attacks(to) & pos.kings(them)) == EmptyBoardBB) &&
       ((bishop_attacks_bb(to, b2) & pos.bishops_and_queens(them))
        == EmptyBoardBB) &&
       ((rook_attacks_bb(to, b2) & pos.rooks_and_queens(them)) == EmptyBoardBB))
      mlist[n++].move = make_move(ksq, to);
  }


  // Generate evasions for other pieces only if not double check.  We use a
  // simple bit twiddling hack here rather than calling count_1s in order to
  // save some time (we know that pos.checkers() has at most two nonzero bits).
  if(!(checkers & (checkers - 1))) {
    Square checksq = first_1(checkers);
    assert(pos.color_of_piece_on(checksq) == them);

    // Find pinned pieces:
    pinned = pos.pinned_pieces(us);

    // Generate captures of the checking piece:

    // Pawn captures:
    b1 = pos.pawn_attacks(them, checksq) & pos.pawns(us) & ~pinned;
    while(b1) {
      from = pop_1st_bit(&b1);
      if(pawn_rank(us, checksq) == RANK_8) {
        mlist[n++].move = make_promotion_move(from, checksq, QUEEN);
        mlist[n++].move = make_promotion_move(from, checksq, ROOK);
        mlist[n++].move = make_promotion_move(from, checksq, BISHOP);
        mlist[n++].move = make_promotion_move(from, checksq, KNIGHT);
      }
      else
        mlist[n++].move = make_move(from, checksq);
    }

    // Knight captures:
    b1 = pos.knight_attacks(checksq) & pos.knights(us) & ~pinned;
    while(b1) {
      from = pop_1st_bit(&b1);
      mlist[n++].move = make_move(from, checksq);
    }

    // Bishop and queen captures:
    b1 = pos.bishop_attacks(checksq) & pos.bishops_and_queens(us)
      & ~pinned;
    while(b1) {
      from = pop_1st_bit(&b1);
      mlist[n++].move = make_move(from, checksq);
    }

    // Rook and queen captures:
    b1 = pos.rook_attacks(checksq) & pos.rooks_and_queens(us)
      & ~pinned;
    while(b1) {
      from = pop_1st_bit(&b1);
      mlist[n++].move = make_move(from, checksq);
    }

    // Blocking check evasions are possible only if the checking piece is
    // a slider:
    if(checkers & pos.sliders()) {
      Bitboard blockSquares = squares_between(checksq, ksq);
      assert((pos.occupied_squares() & blockSquares) == EmptyBoardBB);

      // Pawn moves.  Because a blocking evasion can never be a capture, we
      // only generate pawn pushes.  As so often, the code for pawns is a bit
      // ugly, and uses separate clauses for white and black pawns. :-(
      if(us == WHITE) {
        // Find non-pinned pawns:
        b1 = pos.pawns(WHITE) & ~pinned;

        // Single pawn pushes.  We don't have to AND with empty squares here,
        // because the blocking squares will always be empty.
        b2 = (b1 << 8) & blockSquares;
        while(b2) {
          to = pop_1st_bit(&b2);
          assert(pos.piece_on(to) == EMPTY);
          if(square_rank(to) == RANK_8) {
            mlist[n++].move = make_promotion_move(to - DELTA_N, to, QUEEN);
            mlist[n++].move = make_promotion_move(to - DELTA_N, to, ROOK);
            mlist[n++].move = make_promotion_move(to - DELTA_N, to, BISHOP);
            mlist[n++].move = make_promotion_move(to - DELTA_N, to, KNIGHT);
          }
          else
            mlist[n++].move = make_move(to - DELTA_N, to);
        }
        // Double pawn pushes.
        b2 = (((b1 << 8) & pos.empty_squares() & Rank3BB) << 8) & blockSquares;
        while(b2) {
          to = pop_1st_bit(&b2);
          assert(pos.piece_on(to) == EMPTY);
          assert(square_rank(to) == RANK_4);
          mlist[n++].move = make_move(to - DELTA_N - DELTA_N, to);
        }
      }
      else { // (us == BLACK)
        // Find non-pinned pawns:
        b1 = pos.pawns(BLACK) & ~pinned;

        // Single pawn pushes.  We don't have to AND with empty squares here,
        // because the blocking squares will always be empty.
        b2 = (b1 >> 8) & blockSquares;
        while(b2) {
          to = pop_1st_bit(&b2);
          assert(pos.piece_on(to) == EMPTY);
          if(square_rank(to) == RANK_1) {
            mlist[n++].move = make_promotion_move(to - DELTA_S, to, QUEEN);
            mlist[n++].move = make_promotion_move(to - DELTA_S, to, ROOK);
            mlist[n++].move = make_promotion_move(to - DELTA_S, to, BISHOP);
            mlist[n++].move = make_promotion_move(to - DELTA_S, to, KNIGHT);
          }
          else
            mlist[n++].move = make_move(to - DELTA_S, to);
        }
        // Double pawn pushes.
        b2 = (((b1 >> 8) & pos.empty_squares() & Rank6BB) >> 8) & blockSquares;
        while(b2) {
          to = pop_1st_bit(&b2);
          assert(pos.piece_on(to) == EMPTY);
          assert(square_rank(to) == RANK_5);
          mlist[n++].move = make_move(to - DELTA_S - DELTA_S, to);
        }
      }

      // Knight moves
      b1 = pos.knights(us) & ~pinned;
      while(b1) {
        from = pop_1st_bit(&b1);
        b2 = pos.knight_attacks(from) & blockSquares;
        while(b2) {
          to = pop_1st_bit(&b2);
          mlist[n++].move = make_move(from, to);
        }
      }
      
      // Bishop moves
      b1 = pos.bishops(us) & ~pinned;
      while(b1) {
        from = pop_1st_bit(&b1);
        b2 = pos.bishop_attacks(from) & blockSquares;
        while(b2) {
          to = pop_1st_bit(&b2);
          mlist[n++].move = make_move(from, to);
        }
      }
      
      // Rook moves
      b1 = pos.rooks(us) & ~pinned;
      while(b1) {
        from = pop_1st_bit(&b1);
        b2 = pos.rook_attacks(from) & blockSquares;
        while(b2) {
          to = pop_1st_bit(&b2);
          mlist[n++].move = make_move(from, to);
        }
      }
      
      // Queen moves
      b1 = pos.queens(us) & ~pinned;
      while(b1) {
        from = pop_1st_bit(&b1);
        b2 = pos.queen_attacks(from) & blockSquares;
        while(b2) {
          to = pop_1st_bit(&b2);
          mlist[n++].move = make_move(from, to);
        }
      }
    }

    // Finally, the ugly special case of en passant captures.  An en passant
    // capture can only be a check evasion if the check is not a discovered
    // check.  If pos.ep_square() is set, the last move made must have been
    // a double pawn push.  If, furthermore, the checking piece is a pawn,
    // an en passant check evasion may be possible.
    if(pos.ep_square() != SQ_NONE && (checkers & pos.pawns(them))) {
      to = pos.ep_square();
      b1 = pos.pawn_attacks(them, to) & pos.pawns(us);
      assert(b1 != EmptyBoardBB);
      b1 &= ~pinned;
      while(b1) {
        from = pop_1st_bit(&b1);

        // Before generating the move, we have to make sure it is legal.
        // This is somewhat tricky, because the two disappearing pawns may
        // cause new "discovered checks".  We test this by removing the
        // two relevant bits from the occupied squares bitboard, and using
        // the low-level bitboard functions for bishop and rook attacks.
        b2 = pos.occupied_squares();
        clear_bit(&b2, from);
        clear_bit(&b2, checksq);
        if(((bishop_attacks_bb(ksq, b2) & pos.bishops_and_queens(them))
            == EmptyBoardBB) &&
           ((rook_attacks_bb(ksq, b2) & pos.rooks_and_queens(them))
            == EmptyBoardBB))
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

int generate_legal_moves(const Position &pos, MoveStack *mlist) {
  assert(pos.is_ok());

  if(pos.is_check())
    return generate_evasions(pos, mlist);
  else {
    int i, n;
    Bitboard pinned = pos.pinned_pieces(pos.side_to_move());

    // Generate pseudo-legal moves:
    n = generate_captures(pos, mlist);
    n += generate_noncaptures(pos, mlist + n);

    // Remove illegal moves from the list:
    for(i = 0; i < n; i++) {
      if(!pos.move_is_legal(mlist[i].move, pinned))
        mlist[i--].move = mlist[--n].move;
    }

    return n;
  }
}


/// generate_move_if_legal() takes a position and a (not necessarily
/// pseudo-legal) move and a pinned pieces bitboard as input, and tests
/// whether the move is legal.  If the move is legal, the move itself is
/// returned.  If not, the function returns MOVE_NONE.  This function must
/// only be used when the side to move is not in check.

Move generate_move_if_legal(const Position &pos, Move m, Bitboard pinned) {
  Color us, them;
  Square from, to;
  Piece pc;

  assert(pos.is_ok());
  assert(!pos.is_check());
  assert(move_is_ok(m));

  us = pos.side_to_move();
  them = opposite_color(us);
  from = move_from(m);
  pc = pos.piece_on(from);

  // If the from square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if(color_of_piece(pc) != us )
    return MOVE_NONE;

  to = move_to(m);

  // En passant moves:
  if(move_is_ep(m)) {

    // The piece must be a pawn:
    if(type_of_piece(pc) != PAWN)
      return MOVE_NONE;

    // The destination square must be the en passant square:
    if(to != pos.ep_square())
      return MOVE_NONE;

    assert(pos.square_is_empty(to));
    assert(pos.piece_on(to - pawn_push(us)) == pawn_of_color(them));

    // The move is pseudo-legal.  If it is legal, return it.
    if(pos.move_is_legal(m))
      return m;
    else
      return MOVE_NONE;
  }

  // Castling moves:
  else if(move_is_short_castle(m)) {

    // The piece must be a king:
    if(type_of_piece(pc) != KING)
      return MOVE_NONE;

    // The side to move must still have the right to castle kingside:
    if(!pos.can_castle_kingside(us))
      return MOVE_NONE;

    assert(from == pos.king_square(us));
    assert(to == pos.initial_kr_square(us));
    assert(pos.piece_on(to) == rook_of_color(us));

    Square g1 = relative_square(us, SQ_G1);
    Square f1 = relative_square(us, SQ_F1);
    Square s;
    bool illegal = false;

    for(s = Min(from, g1); s <= Max(from, g1); s++)
      if((s != from && s != to && !pos.square_is_empty(s)) ||
         pos.square_is_attacked(s, them))
        illegal = true;
    for(s = Min(to, f1); s <= Max(to, f1); s++)
      if(s != from && s != to && !pos.square_is_empty(s))
        illegal = true;

    if(!illegal)
      return m;
    else
      return MOVE_NONE;
  }
  else if(move_is_long_castle(m)) {

    // The piece must be a king:
    if(type_of_piece(pc) != KING)
      return MOVE_NONE;

    // The side to move must still have the right to castle kingside:
    if(!pos.can_castle_queenside(us))
      return MOVE_NONE;

    assert(from == pos.king_square(us));
    assert(to == pos.initial_qr_square(us));
    assert(pos.piece_on(to) == rook_of_color(us));

    Square c1 = relative_square(us, SQ_C1);
    Square d1 = relative_square(us, SQ_D1);
    Square s;
    bool illegal = false;

    for(s = Min(from, c1); s <= Max(from, c1); s++)
      if((s != from && s != to && !pos.square_is_empty(s)) ||
         pos.square_is_attacked(s, them))
        illegal = true;
    for(s = Min(to, d1); s <= Max(to, d1); s++)
      if(s != from && s != to && !pos.square_is_empty(s))
        illegal = true;
    if(square_file(to) == FILE_B &&
       (pos.piece_on(to + DELTA_W) == rook_of_color(them) ||
        pos.piece_on(to + DELTA_W) == queen_of_color(them)))
      illegal = true;

    if(!illegal)
      return m;
    else
      return MOVE_NONE;
  }

  // Normal moves
  else {

    // The destination square cannot be occupied by a friendly piece:
    if(pos.color_of_piece_on(to) == us)
      return MOVE_NONE;

    // Proceed according to the type of the moving piece.
    switch(type_of_piece(pc)) {

    case PAWN:
      // Pawn moves, as usual, are somewhat messy.  
      if(us == WHITE) {
        // If the destination square is on the 8th rank, the move must be a
        // promotion.
        if(square_rank(to) == RANK_8 && !move_promotion(m))
          return MOVE_NONE;
        
        // Proceed according to the square delta between the source and
        // destionation squares.
        switch(to - from) {
          
        case DELTA_NW: case DELTA_NE:
          // Capture.  The destination square must be occupied by an enemy piece
          // (en passant captures was handled earlier).
          if(pos.color_of_piece_on(to) != them)
            return MOVE_NONE;
          break;

        case DELTA_N:
          // Pawn push.  The destination square must be empty.
          if(!pos.square_is_empty(to))
            return MOVE_NONE;
          break;

        case DELTA_NN:
          // Double pawn push.  The destination square must be on the fourth
          // rank, and both the destination square and the square between the
          // source and destination squares must be empty.
          if(square_rank(to) != RANK_4 || !pos.square_is_empty(to) ||
             !pos.square_is_empty(from + DELTA_N))
            return MOVE_NONE;
          break;
          
        default:
          return MOVE_NONE;
        }
      }
      else { // (us == BLACK)
        // If the destination square is on the 1st rank, the move must be a
        // promotion.
        if(square_rank(to) == RANK_1 && !move_promotion(m))
          return MOVE_NONE;
        
        // Proceed according to the square delta between the source and
        // destionation squares.
        switch(to - from) {
          
        case DELTA_SW: case DELTA_SE:
          // Capture.  The destination square must be occupied by an enemy piece
          // (en passant captures was handled earlier).
          if(pos.color_of_piece_on(to) != them)
            return MOVE_NONE;
          break;
          
        case DELTA_S:
          // Pawn push.  The destination square must be empty.
          if(!pos.square_is_empty(to))
            return MOVE_NONE;
          break;
          
        case DELTA_SS:
          // Double pawn push.  The destination square must be on the fifth
          // rank, and both the destination square and the square between the
          // source and destination squares must be empty.
          if(square_rank(to) != RANK_5 || !pos.square_is_empty(to) ||
             !pos.square_is_empty(from + DELTA_S))
            return MOVE_NONE;
          break;
          
        default:
          return MOVE_NONE;
        }
      }
      // The move is pseudo-legal.  Return it if it is legal.
      if(pos.move_is_legal(m))
        return m;
      else
        return MOVE_NONE;
      break;

    case KNIGHT:
      if(pos.knight_attacks_square(from, to) && pos.move_is_legal(m) &&
         !move_promotion(m))
        return m;
      else
        return MOVE_NONE;
      break;
      
    case BISHOP:
      if(pos.bishop_attacks_square(from, to) && pos.move_is_legal(m) &&
         !move_promotion(m))
        return m;
      else
        return MOVE_NONE;
      break;
      
    case ROOK:
      if(pos.rook_attacks_square(from, to) && pos.move_is_legal(m) &&
         !move_promotion(m))
        return m;
      else
        return MOVE_NONE;
      break;
    
    case QUEEN:
      if(pos.queen_attacks_square(from, to) && pos.move_is_legal(m) &&
         !move_promotion(m))
        return m;
      else
        return MOVE_NONE;
      break;

    case KING:
      if(pos.king_attacks_square(from, to) && pos.move_is_legal(m) &&
         !move_promotion(m))
        return m;
      else
        return MOVE_NONE;
      break;

    default:
      assert(false);
    }
  }

  assert(false);
  return MOVE_NONE;
}


namespace {

  int generate_white_pawn_captures(const Position &pos, MoveStack *mlist) {
    Bitboard pawns = pos.pawns(WHITE);
    Bitboard enemyPieces = pos.pieces_of_color(BLACK);
    Bitboard b1, b2;
    Square sq;
    int n = 0;

    // Captures in the a1-h8 direction:
    b1 = (pawns << 9) & ~FileABB & enemyPieces;

    // Promotions:
    b2 = b1 & Rank8BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_promotion_move(sq - DELTA_NE, sq, QUEEN);
    }

    // Non-promotions:
    b2 = b1 & ~Rank8BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - DELTA_NE, sq);
    }

    // Captures in the h1-a8 direction:
    b1 = (pawns << 7) & ~FileHBB & enemyPieces;

    // Promotions:
    b2 = b1 & Rank8BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_promotion_move(sq - DELTA_NW, sq, QUEEN);
    }

    // Non-promotions:
    b2 = b1 & ~Rank8BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - DELTA_NW, sq);
    }

    // Non-capturing promotions:
    b1 = (pawns << 8) & pos.empty_squares() & Rank8BB;
    while(b1)  {
      sq = pop_1st_bit(&b1);
      mlist[n++].move = make_promotion_move(sq - DELTA_N, sq, QUEEN);
    }

    // En passant captures:
    if(pos.ep_square() != SQ_NONE) {
      assert(square_rank(pos.ep_square()) == RANK_6);
      b1 = pawns & pos.black_pawn_attacks(pos.ep_square());
      assert(b1 != EmptyBoardBB);
      while(b1) {
        sq = pop_1st_bit(&b1);
        mlist[n++].move = make_ep_move(sq, pos.ep_square());
      }
    }

    return n;
  }


  int generate_black_pawn_captures(const Position &pos, MoveStack *mlist) {
    Bitboard pawns = pos.pawns(BLACK);
    Bitboard enemyPieces = pos.pieces_of_color(WHITE);
    Bitboard b1, b2;
    Square sq;
    int n = 0;

    // Captures in the a8-h1 direction:
    b1 = (pawns >> 7) & ~FileABB & enemyPieces;

    // Promotions:
    b2 = b1 & Rank1BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_promotion_move(sq - DELTA_SE, sq, QUEEN);
    }

    // Non-promotions:
    b2 = b1 & ~Rank1BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - DELTA_SE, sq);
    }

    // Captures in the h8-a1 direction:
    b1 = (pawns >> 9) & ~FileHBB & enemyPieces;

    // Promotions:
    b2 = b1 & Rank1BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_promotion_move(sq - DELTA_SW, sq, QUEEN);
    }

    // Non-promotions:
    b2 = b1 & ~Rank1BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - DELTA_SW, sq);
    }

    // Non-capturing promotions:
    b1 = (pawns >> 8) & pos.empty_squares() & Rank1BB;
    while(b1)  {
      sq = pop_1st_bit(&b1);
      mlist[n++].move = make_promotion_move(sq - DELTA_S, sq, QUEEN);
    }

    // En passant captures:
    if(pos.ep_square() != SQ_NONE) {
      assert(square_rank(pos.ep_square()) == RANK_3);
      b1 = pawns & pos.white_pawn_attacks(pos.ep_square());
      assert(b1 != EmptyBoardBB);
      while(b1) {
        sq = pop_1st_bit(&b1);
        mlist[n++].move = make_ep_move(sq, pos.ep_square());
      }
    }

    return n;
  }
    

  int generate_white_pawn_noncaptures(const Position &pos, MoveStack *mlist) {
    Bitboard pawns = pos.pawns(WHITE);
    Bitboard enemyPieces = pos.pieces_of_color(BLACK);
    Bitboard emptySquares = pos.empty_squares();
    Bitboard b1, b2;
    Square sq;
    int n = 0;

    // Underpromotion captures in the a1-h8 direction:
    b1 = (pawns << 9) & ~FileABB & enemyPieces & Rank8BB;
    while(b1) {
      sq = pop_1st_bit(&b1);
      mlist[n++].move = make_promotion_move(sq - DELTA_NE, sq, ROOK);
      mlist[n++].move = make_promotion_move(sq - DELTA_NE, sq, BISHOP);
      mlist[n++].move = make_promotion_move(sq - DELTA_NE, sq, KNIGHT);
    }

    // Underpromotion captures in the h1-a8 direction:
    b1 = (pawns << 7) & ~FileHBB & enemyPieces & Rank8BB;
    while(b1) {
      sq = pop_1st_bit(&b1);
      mlist[n++].move = make_promotion_move(sq - DELTA_NW, sq, ROOK);
      mlist[n++].move = make_promotion_move(sq - DELTA_NW, sq, BISHOP);
      mlist[n++].move = make_promotion_move(sq - DELTA_NW, sq, KNIGHT);
    }

    // Single pawn pushes:
    b1 = (pawns << 8) & emptySquares;
    b2 = b1 & Rank8BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_promotion_move(sq - DELTA_N, sq, ROOK);
      mlist[n++].move = make_promotion_move(sq - DELTA_N, sq, BISHOP);
      mlist[n++].move = make_promotion_move(sq - DELTA_N, sq, KNIGHT);
    }
    b2 = b1 & ~Rank8BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - DELTA_N, sq);
    }

    // Double pawn pushes:
    b2 = ((b1 & Rank3BB) << 8) & emptySquares;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - DELTA_N - DELTA_N, sq);
    }

    return n;
  }


  int generate_black_pawn_noncaptures(const Position &pos, MoveStack *mlist) {
    Bitboard pawns = pos.pawns(BLACK);
    Bitboard enemyPieces = pos.pieces_of_color(WHITE);
    Bitboard emptySquares = pos.empty_squares();
    Bitboard b1, b2;
    Square sq;
    int n = 0;

    // Underpromotion captures in the a8-h1 direction:
    b1 = (pawns >> 7) & ~FileABB & enemyPieces & Rank1BB;
    while(b1) {
      sq = pop_1st_bit(&b1);
      mlist[n++].move = make_promotion_move(sq - DELTA_SE, sq, ROOK);
      mlist[n++].move = make_promotion_move(sq - DELTA_SE, sq, BISHOP);
      mlist[n++].move = make_promotion_move(sq - DELTA_SE, sq, KNIGHT);
    }

    // Underpromotion captures in the h8-a1 direction:
    b1 = (pawns >> 9) & ~FileHBB & enemyPieces & Rank1BB;
    while(b1) {
      sq = pop_1st_bit(&b1);
      mlist[n++].move = make_promotion_move(sq - DELTA_SW, sq, ROOK);
      mlist[n++].move = make_promotion_move(sq - DELTA_SW, sq, BISHOP);
      mlist[n++].move = make_promotion_move(sq - DELTA_SW, sq, KNIGHT);
    }

    // Single pawn pushes:
    b1 = (pawns >> 8) & emptySquares;
    b2 = b1 & Rank1BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_promotion_move(sq - DELTA_S, sq, ROOK);
      mlist[n++].move = make_promotion_move(sq - DELTA_S, sq, BISHOP);
      mlist[n++].move = make_promotion_move(sq - DELTA_S, sq, KNIGHT);
    }
    b2 = b1 & ~Rank1BB;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - DELTA_S, sq);
    }
    
    // Double pawn pushes:
    b2 = ((b1 & Rank6BB) >> 8) & emptySquares;
    while(b2) {
      sq = pop_1st_bit(&b2);
      mlist[n++].move = make_move(sq - DELTA_S - DELTA_S, sq);
    }

    return n;
  }

  
  int generate_knight_moves(const Position &pos, MoveStack *mlist, 
                            Color side, Bitboard target) {
    Square from, to;
    Bitboard b;
    int i, n = 0;

    for(i = 0; i < pos.knight_count(side); i++) {
      from = pos.knight_list(side, i);
      b = pos.knight_attacks(from) & target;
      while(b) {
        to = pop_1st_bit(&b);
        mlist[n++].move = make_move(from, to);
      }
    }
    return n;
  }


  int generate_bishop_moves(const Position &pos, MoveStack *mlist, 
                            Color side, Bitboard target) {
    Square from, to;
    Bitboard b;
    int i, n = 0;

    for(i = 0; i < pos.bishop_count(side); i++) {
      from = pos.bishop_list(side, i);
      b = pos.bishop_attacks(from) & target;
      while(b) {
        to = pop_1st_bit(&b);
        mlist[n++].move = make_move(from, to);
      }
    }
    return n;
  }


  int generate_rook_moves(const Position &pos, MoveStack *mlist, 
                          Color side, Bitboard target) {
    Square from, to;
    Bitboard b;
    int i, n = 0;

    for(i = 0; i < pos.rook_count(side); i++) {
      from = pos.rook_list(side, i);
      b = pos.rook_attacks(from) & target;
      while(b) {
        to = pop_1st_bit(&b);
        mlist[n++].move = make_move(from, to);
      }
    }
    return n;
  }


  int generate_queen_moves(const Position &pos, MoveStack *mlist, 
                           Color side, Bitboard target) {
    Square from, to;
    Bitboard b;
    int i, n = 0;

    for(i = 0; i < pos.queen_count(side); i++) {
      from = pos.queen_list(side, i);
      b = pos.queen_attacks(from) & target;
      while(b) {
        to = pop_1st_bit(&b);
        mlist[n++].move = make_move(from, to);
      }
    }
    return n;
  }


  int generate_king_moves(const Position &pos, MoveStack *mlist, 
                          Square from, Bitboard target) {
    Square to;
    Bitboard b;
    int n = 0;
    
    b = pos.king_attacks(from) & target;
    while(b) {
      to = pop_1st_bit(&b);
      mlist[n++].move = make_move(from, to);
    }
    return n;
  }


  int generate_castle_moves(const Position &pos, MoveStack *mlist, Color us) {
    int n = 0;

    if(pos.can_castle(us)) {
      Color them = opposite_color(us);
      Square ksq = pos.king_square(us);
      assert(pos.piece_on(ksq) == king_of_color(us));
      
      if(pos.can_castle_kingside(us)) {
        Square rsq = pos.initial_kr_square(us);
        Square g1 = relative_square(us, SQ_G1);
        Square f1 = relative_square(us, SQ_F1);
        Square s;
        bool illegal = false;

        assert(pos.piece_on(rsq) == rook_of_color(us));

        for(s = Min(ksq, g1); s <= Max(ksq, g1); s++)
          if((s != ksq && s != rsq && pos.square_is_occupied(s))
             || pos.square_is_attacked(s, them))
            illegal = true;
        for(s = Min(rsq, f1); s <= Max(rsq, f1); s++)
          if(s != ksq && s != rsq && pos.square_is_occupied(s))
            illegal = true;

        if(!illegal)
          mlist[n++].move = make_castle_move(ksq, rsq);
      }

      if(pos.can_castle_queenside(us)) {
        Square rsq = pos.initial_qr_square(us);
        Square c1 = relative_square(us, SQ_C1);
        Square d1 = relative_square(us, SQ_D1);
        Square s;
        bool illegal = false;

        assert(pos.piece_on(rsq) == rook_of_color(us));

        for(s = Min(ksq, c1); s <= Max(ksq, c1); s++)
          if((s != ksq && s != rsq && pos.square_is_occupied(s))
             || pos.square_is_attacked(s, them))
            illegal = true;
        for(s = Min(rsq, d1); s <= Max(rsq, d1); s++)
          if(s != ksq && s != rsq && pos.square_is_occupied(s))
            illegal = true;
        if(square_file(rsq) == FILE_B &&
           (pos.piece_on(relative_square(us, SQ_A1)) == rook_of_color(them) ||
            pos.piece_on(relative_square(us, SQ_A1)) == queen_of_color(them)))
           illegal = true;
                         
        if(!illegal)
          mlist[n++].move = make_castle_move(ksq, rsq);
      }
    }

    return n;
  }
    
}
