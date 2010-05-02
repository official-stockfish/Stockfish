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
  MoveStack* generate_pawn_moves(const Position&, MoveStack*, Bitboard, Square);

  // Template generate_piece_moves (captures and non-captures) with specializations and overloads
  template<PieceType>
  MoveStack* generate_piece_moves(const Position&, MoveStack*, Color, Bitboard);

  template<>
  MoveStack* generate_piece_moves<KING>(const Position&, MoveStack*, Color, Bitboard);

  template<PieceType Piece, MoveType Type>
  inline MoveStack* generate_piece_moves(const Position& p, MoveStack* m, Color us, Bitboard t) {

    assert(Piece == PAWN);
    assert(Type == CAPTURE || Type == NON_CAPTURE || Type == EVASION);

    return (us == WHITE ? generate_pawn_moves<WHITE, Type>(p, m, t, SQ_NONE)
                        : generate_pawn_moves<BLACK, Type>(p, m, t, SQ_NONE));
  }

  // Templates for non-capture checks generation

  template<PieceType Piece>
  MoveStack* generate_discovered_checks(const Position&, MoveStack*, Square);

  template<PieceType>
  MoveStack* generate_direct_checks(const Position&, MoveStack*, Color, Bitboard, Square);

  template<>
  inline MoveStack* generate_direct_checks<PAWN>(const Position& p, MoveStack* m, Color us, Bitboard dc, Square ksq) {

    return (us == WHITE ? generate_pawn_moves<WHITE, CHECK>(p, m, dc, ksq)
                        : generate_pawn_moves<BLACK, CHECK>(p, m, dc, ksq));
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
  mlist = generate_piece_moves<PAWN, CAPTURE>(pos, mlist, us, target);
  return  generate_piece_moves<KING>(pos, mlist, us, target);
}


/// generate_noncaptures() generates all pseudo-legal non-captures and
/// underpromotions. Returns a pointer to the end of the move list.

MoveStack* generate_noncaptures(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Color us = pos.side_to_move();
  Bitboard target = pos.empty_squares();

  mlist = generate_piece_moves<PAWN, NON_CAPTURE>(pos, mlist, us, target);
  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  mlist = generate_piece_moves<QUEEN>(pos, mlist, us, target);
  mlist = generate_piece_moves<KING>(pos, mlist, us, target);
  mlist = generate_castle_moves<KING_SIDE>(pos, mlist);
  return  generate_castle_moves<QUEEN_SIDE>(pos, mlist);
}


/// generate_non_capture_checks() generates all pseudo-legal non-captures and knight
/// underpromotions that give check. Returns a pointer to the end of the move list.

MoveStack* generate_non_capture_checks(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(!pos.is_check());

  Bitboard b, dc;
  Square from;
  Color us = pos.side_to_move();
  Square ksq = pos.king_square(opposite_color(us));

  assert(pos.piece_on(ksq) == piece_of_color_and_type(opposite_color(us), KING));

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

MoveStack* generate_evasions(const Position& pos, MoveStack* mlist) {

  assert(pos.is_ok());
  assert(pos.is_check());

  Bitboard b, target;
  Square from, checksq;
  int checkersCnt = 0;
  Color us = pos.side_to_move();
  Square ksq = pos.king_square(us);
  Bitboard checkers = pos.checkers();
  Bitboard sliderAttacks = EmptyBoardBB;

  assert(pos.piece_on(ksq) == piece_of_color_and_type(us, KING));
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
          if (direction_is_straight(checksq, ksq))
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

  mlist = generate_piece_moves<PAWN, EVASION>(pos, mlist, us, target);
  mlist = generate_piece_moves<KNIGHT>(pos, mlist, us, target);
  mlist = generate_piece_moves<BISHOP>(pos, mlist, us, target);
  mlist = generate_piece_moves<ROOK>(pos, mlist, us, target);
  return  generate_piece_moves<QUEEN>(pos, mlist, us, target);
}


/// generate_moves() computes a complete list of legal or pseudo-legal moves in
/// the current position. This function is not very fast, and should be used
/// only in non time-critical paths.

MoveStack* generate_moves(const Position& pos, MoveStack* mlist, bool pseudoLegal) {

  assert(pos.is_ok());

  MoveStack *last, *cur = mlist;
  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());

  // Generate pseudo-legal moves
  if (pos.is_check())
      last = generate_evasions(pos, mlist);
  else
      last = generate_noncaptures(pos, generate_captures(pos, mlist));

  if (pseudoLegal)
      return last;

  // Remove illegal moves from the list
  while (cur != last)
      if (pos.pl_move_is_legal(cur->move, pinned))
          cur++;
      else
          cur->move = (--last)->move;

  return last;
}


/// move_is_legal() takes a position and a (not necessarily pseudo-legal)
/// move and tests whether the move is legal. This version is not very fast
/// and should be used only in non time-critical paths.

bool move_is_legal(const Position& pos, const Move m) {

  MoveStack mlist[256];
  MoveStack *cur, *last = generate_moves(pos, mlist, true);

   for (cur = mlist; cur != last; cur++)
      if (cur->move == m)
          return pos.pl_move_is_legal(m, pos.pinned_pieces(pos.side_to_move()));

  return false;
}


/// Fast version of move_is_legal() that takes a position a move and a
/// bitboard of pinned pieces as input, and tests whether the move is legal.

bool move_is_legal(const Position& pos, const Move m, Bitboard pinned) {

  assert(pos.is_ok());
  assert(move_is_ok(m));
  assert(pinned == pos.pinned_pieces(pos.side_to_move()));

  Color us = pos.side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Square to = move_to(m);
  Piece pc = pos.piece_on(from);

  // Use a slower but simpler function for uncommon cases
  if (move_is_ep(m) || move_is_castle(m))
      return move_is_legal(pos, m);

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

      // The promotion piece, if any, must be valid
      if (move_promotion_piece(m) > QUEEN || move_promotion_piece(m) == PAWN)
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
      return pos.is_check() ? pos.pl_move_is_evasion(m, pinned) : pos.pl_move_is_legal(m, pinned);
  }

  // Luckly we can handle all the other pieces in one go
  return    bit_is_set(pos.attacks_from(pc, from), to)
        && (pos.is_check() ? pos.pl_move_is_evasion(m, pinned) : pos.pl_move_is_legal(m, pinned))
        && !move_is_promotion(m);
}


namespace {

  template<PieceType Piece>
  MoveStack* generate_piece_moves(const Position& pos, MoveStack* mlist, Color us, Bitboard target) {

    Bitboard b;
    Square from;
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

  template<Color Us, MoveType Type, SquareDelta Diagonal>
  inline MoveStack* generate_pawn_captures(MoveStack* mlist, Bitboard pawns, Bitboard enemyPieces) {

    // Calculate our parametrized parameters at compile time
    const Bitboard TRank8BB = (Us == WHITE ? Rank8BB : Rank1BB);
    const Bitboard TFileABB = (Diagonal == DELTA_NE ? FileABB : FileHBB);
    const SquareDelta TDELTA_NE = (Us == WHITE ? DELTA_NE : DELTA_SE);
    const SquareDelta TDELTA_NW = (Us == WHITE ? DELTA_NW : DELTA_SW);
    const SquareDelta TTDELTA_NE = (Diagonal == DELTA_NE ? TDELTA_NE : TDELTA_NW);

    Bitboard b1, b2;
    Square to;

    // Captures in the a1-h8 (a8-h1 for black) diagonal or in the h1-a8 (h8-a1 for black)
    b1 = move_pawns<Us, Diagonal>(pawns) & ~TFileABB & enemyPieces;

    // Capturing promotions and under-promotions
    if (b1 & TRank8BB)
    {
        b2 = b1 & TRank8BB;
        b1 &= ~TRank8BB;
        while (b2)
        {
            to = pop_1st_bit(&b2);

            if (Type == CAPTURE || Type == EVASION)
                (*mlist++).move = make_promotion_move(to - TTDELTA_NE, to, QUEEN);

            if (Type == NON_CAPTURE || Type == EVASION)
            {
                (*mlist++).move = make_promotion_move(to - TTDELTA_NE, to, ROOK);
                (*mlist++).move = make_promotion_move(to - TTDELTA_NE, to, BISHOP);
                (*mlist++).move = make_promotion_move(to - TTDELTA_NE, to, KNIGHT);
            }

            // This is the only possible under promotion that can give a check
            // not already included in the queen-promotion. It is not sure that
            // the promoted knight will give check, but it doesn't worth to verify.
            if (Type == CHECK)
                (*mlist++).move = make_promotion_move(to - TTDELTA_NE, to, KNIGHT);
        }
    }

    // Serialize standard captures
    if (Type == CAPTURE || Type == EVASION)
        SERIALIZE_MOVES_D(b1, -TTDELTA_NE);

    return mlist;
  }

  template<Color Us, MoveType Type>
  MoveStack* generate_pawn_moves(const Position& pos, MoveStack* mlist, Bitboard target, Square ksq) {

    // Calculate our parametrized parameters at compile time
    const Color Them = (Us == WHITE ? BLACK : WHITE);
    const Bitboard TRank8BB = (Us == WHITE ? Rank8BB : Rank1BB);
    const Bitboard TRank7BB = (Us == WHITE ? Rank7BB : Rank2BB);
    const Bitboard TRank3BB = (Us == WHITE ? Rank3BB : Rank6BB);
    const SquareDelta TDELTA_N = (Us == WHITE ? DELTA_N : DELTA_S);

    Square to;
    Bitboard b1, b2, enemyPieces, emptySquares;
    Bitboard pawns = pos.pieces(PAWN, Us);

    // Standard captures and capturing promotions and underpromotions
    if (Type == CAPTURE || Type == EVASION || (pawns & TRank7BB))
    {
        enemyPieces = (Type == CAPTURE ? target : pos.pieces_of_color(opposite_color(Us)));

        if (Type == EVASION)
            enemyPieces &= target; // Capture only the checker piece

        mlist = generate_pawn_captures<Us, Type, DELTA_NE>(mlist, pawns, enemyPieces);
        mlist = generate_pawn_captures<Us, Type, DELTA_NW>(mlist, pawns, enemyPieces);
    }

    // Non-capturing promotions and underpromotions
    if (pawns & TRank7BB)
    {
        b1 = move_pawns<Us, DELTA_N>(pawns) & TRank8BB & pos.empty_squares();

        if (Type == EVASION)
            b1 &= target; // Only blocking promotion pushes

        while (b1)
        {
            to = pop_1st_bit(&b1);

            if (Type == CAPTURE || Type == EVASION)
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, QUEEN);

            if (Type == NON_CAPTURE || Type == EVASION)
            {
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, ROOK);
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, BISHOP);
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, KNIGHT);
            }

            // This is the only possible under promotion that can give a check
            // not already included in the queen-promotion.
            if (Type == CHECK && bit_is_set(pos.attacks_from<KNIGHT>(to), pos.king_square(Them)))
                (*mlist++).move = make_promotion_move(to - TDELTA_N, to, KNIGHT);
        }
    }

    // Standard pawn pushes and double pushes
    if (Type != CAPTURE)
    {
        emptySquares = (Type == NON_CAPTURE ? target : pos.empty_squares());

        // Single and double pawn pushes
        b1 = move_pawns<Us, DELTA_N>(pawns) & emptySquares & ~TRank8BB;
        b2 = move_pawns<Us, DELTA_N>(b1 & TRank3BB) & emptySquares;

        // Filter out unwanted pushes according to the move type
        if (Type == EVASION)
        {
            b1 &= target;
            b2 &= target;
        }
        else if (Type == CHECK)
        {
            // Pawn moves which give direct cheks
            b1 &= pos.attacks_from<PAWN>(ksq, Them);
            b2 &= pos.attacks_from<PAWN>(ksq, Them);

            // Pawn moves which gives discovered check. This is possible only if
            // the pawn is not on the same file as the enemy king, because we
            //  don't generate captures.
            if (pawns & target) // For CHECK type target is dc bitboard
            {
                Bitboard dc1 = move_pawns<Us, DELTA_N>(pawns & target & ~file_bb(ksq)) & emptySquares & ~TRank8BB;
                Bitboard dc2 = move_pawns<Us, DELTA_N>(dc1 & TRank3BB) & emptySquares;

                b1 |= dc1;
                b2 |= dc2;
            }
        }
        SERIALIZE_MOVES_D(b1, -TDELTA_N);
        SERIALIZE_MOVES_D(b2, -TDELTA_N -TDELTA_N);
    }

    // En passant captures
    if ((Type == CAPTURE || Type == EVASION) && pos.ep_square() != SQ_NONE)
    {
        assert(Us != WHITE || square_rank(pos.ep_square()) == RANK_6);
        assert(Us != BLACK || square_rank(pos.ep_square()) == RANK_3);

        // An en passant capture can be an evasion only if the checking piece
        // is the double pushed pawn and so is in the target. Otherwise this
        // is a discovery check and we are forced to do otherwise.
        if (Type == EVASION && !bit_is_set(target, pos.ep_square() - TDELTA_N))
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

  template<PieceType Piece>
  MoveStack* generate_discovered_checks(const Position& pos, MoveStack* mlist, Square from) {

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

    Bitboard checkSqs, b;
    Square from;
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

        b = pos.attacks_from<Piece>(from) & checkSqs;
        SERIALIZE_MOVES(b);

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
