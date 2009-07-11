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
#include <cstring>
#include <fstream>
#include <iostream>

#include "bitcount.h"
#include "mersenne.h"
#include "movegen.h"
#include "movepick.h"
#include "position.h"
#include "psqtab.h"
#include "san.h"
#include "ucioption.h"

using std::string;


////
//// Variables
////

int Position::castleRightsMask[64];

Key Position::zobrist[2][8][64];
Key Position::zobEp[64];
Key Position::zobCastle[16];
Key Position::zobMaterial[2][8][16];
Key Position::zobSideToMove;

Value Position::MgPieceSquareTable[16][64];
Value Position::EgPieceSquareTable[16][64];

static bool RequestPending = false;

////
//// Functions
////

/// Constructors

Position::Position(const Position& pos) {
  copy(pos);
}

Position::Position(const string& fen) {
  from_fen(fen);
}


/// Position::from_fen() initializes the position object with the given FEN
/// string. This function is not very robust - make sure that input FENs are
/// correct (this is assumed to be the responsibility of the GUI).

void Position::from_fen(const string& fen) {

  static const string pieceLetters = "KQRBNPkqrbnp";
  static const Piece pieces[] = { WK, WQ, WR, WB, WN, WP, BK, BQ, BR, BB, BN, BP };

  clear();

  // Board
  Rank rank = RANK_8;
  File file = FILE_A;
  size_t i = 0;
  for ( ; fen[i] != ' '; i++)
  {
      if (isdigit(fen[i]))
      {
          // Skip the given number of files
          file += (fen[i] - '1' + 1);
          continue;
      }
      else if (fen[i] == '/')
      {
          file = FILE_A;
          rank--;
          continue;
      }
      size_t idx = pieceLetters.find(fen[i]);
      if (idx == string::npos)
      {
           std::cout << "Error in FEN at character " << i << std::endl;
           return;
      }
      Square square = make_square(file, rank);
      put_piece(pieces[idx], square);
      file++;
  }

  // Side to move
  i++;
  if (fen[i] != 'w' && fen[i] != 'b')
  {
      std::cout << "Error in FEN at character " << i << std::endl;
      return;
  }
  sideToMove = (fen[i] == 'w' ? WHITE : BLACK);

  // Castling rights
  i++;
  if (fen[i] != ' ')
  {
      std::cout << "Error in FEN at character " << i << std::endl;
      return;
  }

  i++;
  while(strchr("KQkqabcdefghABCDEFGH-", fen[i])) {
    if (fen[i] == '-')
    {
      i++;
      break;
    }
    else if(fen[i] == 'K') allow_oo(WHITE);
    else if(fen[i] == 'Q') allow_ooo(WHITE);
    else if(fen[i] == 'k') allow_oo(BLACK);
    else if(fen[i] == 'q') allow_ooo(BLACK);
    else if(fen[i] >= 'A' && fen[i] <= 'H') {
      File rookFile, kingFile = FILE_NONE;
      for(Square square = SQ_B1; square <= SQ_G1; square++)
        if(piece_on(square) == WK)
          kingFile = square_file(square);
      if(kingFile == FILE_NONE) {
        std::cout << "Error in FEN at character " << i << std::endl;
        return;
      }
      initialKFile = kingFile;
      rookFile = File(fen[i] - 'A') + FILE_A;
      if(rookFile < initialKFile) {
        allow_ooo(WHITE);
        initialQRFile = rookFile;
      }
      else {
        allow_oo(WHITE);
        initialKRFile = rookFile;
      }
    }
    else if(fen[i] >= 'a' && fen[i] <= 'h') {
      File rookFile, kingFile = FILE_NONE;
      for(Square square = SQ_B8; square <= SQ_G8; square++)
        if(piece_on(square) == BK)
          kingFile = square_file(square);
      if(kingFile == FILE_NONE) {
        std::cout << "Error in FEN at character " << i << std::endl;
        return;
      }
      initialKFile = kingFile;
      rookFile = File(fen[i] - 'a') + FILE_A;
      if(rookFile < initialKFile) {
        allow_ooo(BLACK);
        initialQRFile = rookFile;
      }
      else {
        allow_oo(BLACK);
        initialKRFile = rookFile;
      }
    }
    else {
      std::cout << "Error in FEN at character " << i << std::endl;
      return;
    }
    i++;
  }

  // Skip blanks
  while (fen[i] == ' ')
      i++;

  // En passant square
  if (    i <= fen.length() - 2
      && (fen[i] >= 'a' && fen[i] <= 'h')
      && (fen[i+1] == '3' || fen[i+1] == '6'))
      st->epSquare = square_from_string(fen.substr(i, 2));

  // Various initialisation
  for (Square sq = SQ_A1; sq <= SQ_H8; sq++)
      castleRightsMask[sq] = ALL_CASTLES;

  castleRightsMask[make_square(initialKFile,  RANK_1)] ^= (WHITE_OO|WHITE_OOO);
  castleRightsMask[make_square(initialKFile,  RANK_8)] ^= (BLACK_OO|BLACK_OOO);
  castleRightsMask[make_square(initialKRFile, RANK_1)] ^= WHITE_OO;
  castleRightsMask[make_square(initialKRFile, RANK_8)] ^= BLACK_OO;
  castleRightsMask[make_square(initialQRFile, RANK_1)] ^= WHITE_OOO;
  castleRightsMask[make_square(initialQRFile, RANK_8)] ^= BLACK_OOO;

  find_checkers();

  st->key = compute_key();
  st->pawnKey = compute_pawn_key();
  st->materialKey = compute_material_key();
  st->mgValue = compute_value<MidGame>();
  st->egValue = compute_value<EndGame>();
  st->npMaterial[WHITE] = compute_non_pawn_material(WHITE);
  st->npMaterial[BLACK] = compute_non_pawn_material(BLACK);
}


/// Position::to_fen() converts the position object to a FEN string. This is
/// probably only useful for debugging.

const string Position::to_fen() const {

  static const string pieceLetters = " PNBRQK  pnbrqk";
  string fen;
  int skip;

  for (Rank rank = RANK_8; rank >= RANK_1; rank--)
  {
      skip = 0;
      for (File file = FILE_A; file <= FILE_H; file++)
      {
          Square sq = make_square(file, rank);
          if (!square_is_occupied(sq))
          {   skip++;
              continue;
          }
          if (skip > 0)
          {
              fen += (char)skip + '0';
              skip = 0;
          }
          fen += pieceLetters[piece_on(sq)];
      }
      if (skip > 0)
          fen += (char)skip + '0';

      fen += (rank > RANK_1 ? '/' : ' ');
  }
  fen += (sideToMove == WHITE ? "w " : "b ");
  if (st->castleRights != NO_CASTLES)
  {
    if (can_castle_kingside(WHITE))  fen += 'K';
    if (can_castle_queenside(WHITE)) fen += 'Q';
    if (can_castle_kingside(BLACK))  fen += 'k';
    if (can_castle_queenside(BLACK)) fen += 'q';
  } else
      fen += '-';

  fen += ' ';
  if (ep_square() != SQ_NONE)
      fen += square_to_string(ep_square());
  else
      fen += '-';

  return fen;
}


/// Position::print() prints an ASCII representation of the position to
/// the standard output. If a move is given then also the san is print.

void Position::print(Move m) const {

  static const string pieceLetters = " PNBRQK  PNBRQK .";

  // Check for reentrancy, as example when called from inside
  // MovePicker that is used also here in move_to_san()
  if (RequestPending)
      return;

  RequestPending = true;

  std::cout << std::endl;
  if (m != MOVE_NONE)
  {
      string col = (color_of_piece_on(move_from(m)) == BLACK ? ".." : "");
      std::cout << "Move is: " << col << move_to_san(*this, m) << std::endl;
  }
  for (Rank rank = RANK_8; rank >= RANK_1; rank--)
  {
      std::cout << "+---+---+---+---+---+---+---+---+" << std::endl;
      for (File file = FILE_A; file <= FILE_H; file++)
      {
          Square sq = make_square(file, rank);
          Piece piece = piece_on(sq);
          if (piece == EMPTY && square_color(sq) == WHITE)
              piece = NO_PIECE;

          char col = (color_of_piece_on(sq) == BLACK ? '=' : ' ');
          std::cout << '|' << col << pieceLetters[piece] << col;
      }
      std::cout << '|' << std::endl;
  }
  std::cout << "+---+---+---+---+---+---+---+---+" << std::endl
            << "Fen is: " << to_fen() << std::endl
            << "Key is: " << st->key << std::endl;

  RequestPending = false;
}


/// Position::copy() creates a copy of the input position.

void Position::copy(const Position& pos) {

  memcpy(this, &pos, sizeof(Position));
  saveState(); // detach and copy state info
}


/// Position:hidden_checkers<>() returns a bitboard of all pinned (against the
/// king) pieces for the given color and for the given pinner type. Or, when
/// template parameter FindPinned is false, the pieces of the given color
/// candidate for a discovery check against the enemy king.
/// Note that checkersBB bitboard must be already updated.

template<bool FindPinned>
Bitboard Position::hidden_checkers(Color c) const {

  Bitboard pinners, result = EmptyBoardBB;

  // Pinned pieces protect our king, dicovery checks attack
  // the enemy king.
  Square ksq = king_square(FindPinned ? c : opposite_color(c));

  // Pinners are sliders, not checkers, that give check when
  // candidate pinned is removed.
  pinners =  (rooks_and_queens(FindPinned ? opposite_color(c) : c) & RookPseudoAttacks[ksq])
           | (bishops_and_queens(FindPinned ? opposite_color(c) : c) & BishopPseudoAttacks[ksq]);

  if (FindPinned && pinners)
      pinners &= ~st->checkersBB;

  while (pinners)
  {
      Square s = pop_1st_bit(&pinners);
      Bitboard b = squares_between(s, ksq) & occupied_squares();

      assert(b);

      if (  !(b & (b - 1)) // Only one bit set?
          && (b & pieces_of_color(c))) // Is an our piece?
          result |= b;
  }
  return result;
}


/// Position:pinned_pieces() returns a bitboard of all pinned (against the
/// king) pieces for the given color.

Bitboard Position::pinned_pieces(Color c) const {

  return hidden_checkers<true>(c);
}


/// Position:discovered_check_candidates() returns a bitboard containing all
/// pieces for the given side which are candidates for giving a discovered
/// check.

Bitboard Position::discovered_check_candidates(Color c) const {

  return hidden_checkers<false>(c);
}

/// Position::attacks_to() computes a bitboard containing all pieces which
/// attacks a given square.

Bitboard Position::attacks_to(Square s) const {

  return  (pawn_attacks(BLACK, s)   & pawns(WHITE))
        | (pawn_attacks(WHITE, s)   & pawns(BLACK))
        | (piece_attacks<KNIGHT>(s) & pieces_of_type(KNIGHT))
        | (piece_attacks<ROOK>(s)   & rooks_and_queens())
        | (piece_attacks<BISHOP>(s) & bishops_and_queens())
        | (piece_attacks<KING>(s)   & pieces_of_type(KING));
}

/// Position::piece_attacks_square() tests whether the piece on square f
/// attacks square t.

bool Position::piece_attacks_square(Piece p, Square f, Square t) const {

  assert(square_is_ok(f));
  assert(square_is_ok(t));

  switch (p)
  {
  case WP:          return pawn_attacks_square(WHITE, f, t);
  case BP:          return pawn_attacks_square(BLACK, f, t);
  case WN: case BN: return piece_attacks_square<KNIGHT>(f, t);
  case WB: case BB: return piece_attacks_square<BISHOP>(f, t);
  case WR: case BR: return piece_attacks_square<ROOK>(f, t);
  case WQ: case BQ: return piece_attacks_square<QUEEN>(f, t);
  case WK: case BK: return piece_attacks_square<KING>(f, t);
  default: break;
  }
  return false;
}


/// Position::move_attacks_square() tests whether a move from the current
/// position attacks a given square.

bool Position::move_attacks_square(Move m, Square s) const {

  assert(move_is_ok(m));
  assert(square_is_ok(s));

  Square f = move_from(m), t = move_to(m);

  assert(square_is_occupied(f));

  if (piece_attacks_square(piece_on(f), t, s))
      return true;

  // Move the piece and scan for X-ray attacks behind it
  Bitboard occ = occupied_squares();
  Color us = color_of_piece_on(f);
  clear_bit(&occ, f);
  set_bit(&occ, t);
  Bitboard xray = ( (rook_attacks_bb(s, occ) & rooks_and_queens())
                   |(bishop_attacks_bb(s, occ) & bishops_and_queens())) & pieces_of_color(us);

  // If we have attacks we need to verify that are caused by our move
  // and are not already existent ones.
  return xray && (xray ^ (xray & piece_attacks<QUEEN>(s)));
}


/// Position::find_checkers() computes the checkersBB bitboard, which
/// contains a nonzero bit for each checking piece (0, 1 or 2). It
/// currently works by calling Position::attacks_to, which is probably
/// inefficient. Consider rewriting this function to use the last move
/// played, like in non-bitboard versions of Glaurung.

void Position::find_checkers() {

  Color us = side_to_move();
  st->checkersBB = attacks_to(king_square(us), opposite_color(us));
}


/// Position::pl_move_is_legal() tests whether a pseudo-legal move is legal

bool Position::pl_move_is_legal(Move m) const {

  // If we're in check, all pseudo-legal moves are legal, because our
  // check evasion generator only generates true legal moves.
  return is_check() || pl_move_is_legal(m, pinned_pieces(side_to_move()));
}

bool Position::pl_move_is_legal(Move m, Bitboard pinned) const {

  assert(is_ok());
  assert(move_is_ok(m));
  assert(pinned == pinned_pieces(side_to_move()));
  assert(!is_check());

  // Castling moves are checked for legality during move generation.
  if (move_is_castle(m))
      return true;

  Color us = side_to_move();
  Square from = move_from(m);
  Square ksq = king_square(us);

  assert(color_of_piece_on(from) == us);
  assert(piece_on(ksq) == piece_of_color_and_type(us, KING));

  // En passant captures are a tricky special case.  Because they are
  // rather uncommon, we do it simply by testing whether the king is attacked
  // after the move is made
  if (move_is_ep(m))
  {
      Color them = opposite_color(us);
      Square to = move_to(m);
      Square capsq = make_square(square_file(to), square_rank(from));
      Bitboard b = occupied_squares();

      assert(to == ep_square());
      assert(piece_on(from) == piece_of_color_and_type(us, PAWN));
      assert(piece_on(capsq) == piece_of_color_and_type(them, PAWN));
      assert(piece_on(to) == EMPTY);

      clear_bit(&b, from);
      clear_bit(&b, capsq);
      set_bit(&b, to);

      return   !(rook_attacks_bb(ksq, b) & rooks_and_queens(them))
            && !(bishop_attacks_bb(ksq, b) & bishops_and_queens(them));
  }

  // If the moving piece is a king, check whether the destination
  // square is attacked by the opponent.
  if (from == ksq)
      return !(square_is_attacked(move_to(m), opposite_color(us)));

  // A non-king move is legal if and only if it is not pinned or it
  // is moving along the ray towards or away from the king.
  return (   !pinned
          || !bit_is_set(pinned, from)
          || (direction_between_squares(from, ksq) == direction_between_squares(move_to(m), ksq)));
}


/// Position::move_is_check() tests whether a pseudo-legal move is a check

bool Position::move_is_check(Move m) const {

  Bitboard dc = discovered_check_candidates(side_to_move());
  return move_is_check(m, dc);
}

bool Position::move_is_check(Move m, Bitboard dcCandidates) const {

  assert(is_ok());
  assert(move_is_ok(m));
  assert(dcCandidates == discovered_check_candidates(side_to_move()));

  Color us = side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Square to = move_to(m);
  Square ksq = king_square(them);

  assert(color_of_piece_on(from) == us);
  assert(piece_on(ksq) == piece_of_color_and_type(them, KING));

  // Proceed according to the type of the moving piece
  switch (type_of_piece_on(from))
  {
  case PAWN:

      if (bit_is_set(pawn_attacks(them, ksq), to)) // Normal check?
          return true;

      if (   dcCandidates // Discovered check?
          && bit_is_set(dcCandidates, from)
          && (direction_between_squares(from, ksq) != direction_between_squares(to, ksq)))
          return true;

      if (move_is_promotion(m)) // Promotion with check?
      {
          Bitboard b = occupied_squares();
          clear_bit(&b, from);

          switch (move_promotion_piece(m))
          {
          case KNIGHT:
              return bit_is_set(piece_attacks<KNIGHT>(to), ksq);
          case BISHOP:
              return bit_is_set(bishop_attacks_bb(to, b), ksq);
          case ROOK:
              return bit_is_set(rook_attacks_bb(to, b), ksq);
          case QUEEN:
              return bit_is_set(queen_attacks_bb(to, b), ksq);
          default:
              assert(false);
          }
      }
      // En passant capture with check?  We have already handled the case
      // of direct checks and ordinary discovered check, the only case we
      // need to handle is the unusual case of a discovered check through the
      // captured pawn.
      else if (move_is_ep(m))
      {
          Square capsq = make_square(square_file(to), square_rank(from));
          Bitboard b = occupied_squares();
          clear_bit(&b, from);
          clear_bit(&b, capsq);
          set_bit(&b, to);
          return  (rook_attacks_bb(ksq, b) & rooks_and_queens(us))
                ||(bishop_attacks_bb(ksq, b) & bishops_and_queens(us));
      }
      return false;

  // Test discovered check and normal check according to piece type
  case KNIGHT:
    return   (dcCandidates && bit_is_set(dcCandidates, from))
          || bit_is_set(piece_attacks<KNIGHT>(ksq), to);

  case BISHOP:
    return   (dcCandidates && bit_is_set(dcCandidates, from))
          || (direction_is_diagonal(ksq, to) && bit_is_set(piece_attacks<BISHOP>(ksq), to));

  case ROOK:
    return   (dcCandidates && bit_is_set(dcCandidates, from))
          || (direction_is_straight(ksq, to) && bit_is_set(piece_attacks<ROOK>(ksq), to));

  case QUEEN:
      // Discovered checks are impossible!
      assert(!bit_is_set(dcCandidates, from));
      return (   (direction_is_straight(ksq, to) && bit_is_set(piece_attacks<ROOK>(ksq), to))
              || (direction_is_diagonal(ksq, to) && bit_is_set(piece_attacks<BISHOP>(ksq), to)));

  case KING:
      // Discovered check?
      if (   bit_is_set(dcCandidates, from)
          && (direction_between_squares(from, ksq) != direction_between_squares(to, ksq)))
          return true;

      // Castling with check?
      if (move_is_castle(m))
      {
          Square kfrom, kto, rfrom, rto;
          Bitboard b = occupied_squares();
          kfrom = from;
          rfrom = to;

          if (rfrom > kfrom)
          {
              kto = relative_square(us, SQ_G1);
              rto = relative_square(us, SQ_F1);
          } else {
              kto = relative_square(us, SQ_C1);
              rto = relative_square(us, SQ_D1);
          }
          clear_bit(&b, kfrom);
          clear_bit(&b, rfrom);
          set_bit(&b, rto);
          set_bit(&b, kto);
          return bit_is_set(rook_attacks_bb(rto, b), ksq);
      }
      return false;

  default: // NO_PIECE_TYPE
      break;
  }
  assert(false);
  return false;
}


/// Position::update_checkers() udpates chekers info given the move. It is called
/// in do_move() and is faster then find_checkers().

template<PieceType Piece>
inline void Position::update_checkers(Bitboard* pCheckersBB, Square ksq, Square from,
                                      Square to, Bitboard dcCandidates) {

  const bool Bishop = (Piece == QUEEN || Piece == BISHOP);
  const bool Rook   = (Piece == QUEEN || Piece == ROOK);
  const bool Slider = Bishop || Rook;

  // Direct checks
  if (  (   (Bishop && bit_is_set(BishopPseudoAttacks[ksq], to))
         || (Rook   && bit_is_set(RookPseudoAttacks[ksq], to)))
      && bit_is_set(piece_attacks<Piece>(ksq), to)) // slow, try to early skip
      set_bit(pCheckersBB, to);

  else if (   Piece != KING
           && !Slider
           && bit_is_set(piece_attacks<Piece>(ksq), to))
      set_bit(pCheckersBB, to);

  // Discovery checks
  if (Piece != QUEEN && bit_is_set(dcCandidates, from))
  {
      if (Piece != ROOK)
          (*pCheckersBB) |= (piece_attacks<ROOK>(ksq) & rooks_and_queens(side_to_move()));

      if (Piece != BISHOP)
          (*pCheckersBB) |= (piece_attacks<BISHOP>(ksq) & bishops_and_queens(side_to_move()));
  }
}


/// Position::do_move() makes a move, and saves all information necessary
/// to a StateInfo object. The move is assumed to be legal.
/// Pseudo-legal moves should be filtered out before this function is called.

void Position::do_move(Move m, StateInfo& newSt) {

  do_move(m, newSt, discovered_check_candidates(side_to_move()));
}

void Position::do_move(Move m, StateInfo& newSt, Bitboard dcCandidates) {

  assert(is_ok());
  assert(move_is_ok(m));

  // Copy some fields of old state to our new StateInfo object except the
  // ones which are recalculated from scratch anyway, then switch our state
  // pointer to point to the new, ready to be updated, state.
  struct ReducedStateInfo {
    Key key, pawnKey, materialKey;
    int castleRights, rule50;
    Square epSquare;
    Value mgValue, egValue;
    Value npMaterial[2];
  };

  memcpy(&newSt, st, sizeof(ReducedStateInfo));
  newSt.capture = NO_PIECE_TYPE;
  newSt.previous = st;
  st = &newSt;

  // Save the current key to the history[] array, in order to be able to
  // detect repetition draws.
  history[gamePly] = st->key;

  // Increment the 50 moves rule draw counter. Resetting it to zero in the
  // case of non-reversible moves is taken care of later.
  st->rule50++;

  if (move_is_castle(m))
      do_castle_move(m);
  else if (move_is_promotion(m))
      do_promotion_move(m);
  else if (move_is_ep(m))
      do_ep_move(m);
  else
  {
    Color us = side_to_move();
    Color them = opposite_color(us);
    Square from = move_from(m);
    Square to = move_to(m);

    assert(color_of_piece_on(from) == us);
    assert(color_of_piece_on(to) == them || piece_on(to) == EMPTY);

    Piece piece = piece_on(from);
    PieceType pt = type_of_piece(piece);

    st->capture = type_of_piece_on(to);

    if (st->capture)
      do_capture_move(st->capture, them, to);

    // Move the piece
    Bitboard move_bb = make_move_bb(from, to);
    do_move_bb(&(byColorBB[us]), move_bb);
    do_move_bb(&(byTypeBB[pt]), move_bb);
    do_move_bb(&(byTypeBB[0]), move_bb); // HACK: byTypeBB[0] == occupied squares

    board[to] = board[from];
    board[from] = EMPTY;

    // Update hash key
    st->key ^= zobrist[us][pt][from] ^ zobrist[us][pt][to];

    // Update incremental scores
    st->mgValue += pst_delta<MidGame>(piece, from, to);
    st->egValue += pst_delta<EndGame>(piece, from, to);

    // If the moving piece was a king, update the king square
    if (pt == KING)
        kingSquare[us] = to;

    // Reset en passant square
    if (st->epSquare != SQ_NONE)
    {
        st->key ^= zobEp[st->epSquare];
        st->epSquare = SQ_NONE;
    }

    // If the moving piece was a pawn do some special extra work
    if (pt == PAWN)
    {
        // Reset rule 50 draw counter
        st->rule50 = 0;

        // Update pawn hash key
        st->pawnKey ^= zobrist[us][PAWN][from] ^ zobrist[us][PAWN][to];

        // Set en passant square, only if moved pawn can be captured
        if (abs(int(to) - int(from)) == 16)
        {
            if (   (us == WHITE && (pawn_attacks(WHITE, from + DELTA_N) & pawns(BLACK)))
                || (us == BLACK && (pawn_attacks(BLACK, from + DELTA_S) & pawns(WHITE))))
            {
                st->epSquare = Square((int(from) + int(to)) / 2);
                st->key ^= zobEp[st->epSquare];
            }
        }
    }

    // Update piece lists
    pieceList[us][pt][index[from]] = to;
    index[to] = index[from];

    // Update castle rights, try to shortcut a common case
    if ((castleRightsMask[from] & castleRightsMask[to]) != ALL_CASTLES)
    {
        st->key ^= zobCastle[st->castleRights];
        st->castleRights &= castleRightsMask[from];
        st->castleRights &= castleRightsMask[to];
        st->key ^= zobCastle[st->castleRights];
    }

    // Update checkers bitboard, piece must be already moved
    st->checkersBB = EmptyBoardBB;
    Square ksq = king_square(them);
    switch (pt)
    {
    case PAWN:   update_checkers<PAWN>(&(st->checkersBB), ksq, from, to, dcCandidates);   break;
    case KNIGHT: update_checkers<KNIGHT>(&(st->checkersBB), ksq, from, to, dcCandidates); break;
    case BISHOP: update_checkers<BISHOP>(&(st->checkersBB), ksq, from, to, dcCandidates); break;
    case ROOK:   update_checkers<ROOK>(&(st->checkersBB), ksq, from, to, dcCandidates);   break;
    case QUEEN:  update_checkers<QUEEN>(&(st->checkersBB), ksq, from, to, dcCandidates);  break;
    case KING:   update_checkers<KING>(&(st->checkersBB), ksq, from, to, dcCandidates);   break;
    default: assert(false); break;
    }
  }

  // Finish
  st->key ^= zobSideToMove;
  sideToMove = opposite_color(sideToMove);
  gamePly++;

  st->mgValue += (sideToMove == WHITE)? TempoValueMidgame : -TempoValueMidgame;
  st->egValue += (sideToMove == WHITE)? TempoValueEndgame : -TempoValueEndgame;

  assert(is_ok());
}


/// Position::do_capture_move() is a private method used to update captured
/// piece info. It is called from the main Position::do_move function.

void Position::do_capture_move(PieceType capture, Color them, Square to) {

    assert(capture != KING);

    // Remove captured piece
    clear_bit(&(byColorBB[them]), to);
    clear_bit(&(byTypeBB[capture]), to);
    clear_bit(&(byTypeBB[0]), to);

    // Update hash key
    st->key ^= zobrist[them][capture][to];

    // If the captured piece was a pawn, update pawn hash key
    if (capture == PAWN)
        st->pawnKey ^= zobrist[them][PAWN][to];

    // Update incremental scores
    st->mgValue -= pst<MidGame>(them, capture, to);
    st->egValue -= pst<EndGame>(them, capture, to);

    // Update material
    if (capture != PAWN)
        st->npMaterial[them] -= piece_value_midgame(capture);

    // Update material hash key
    st->materialKey ^= zobMaterial[them][capture][pieceCount[them][capture]];

    // Update piece count
    pieceCount[them][capture]--;

    // Update piece list
    pieceList[them][capture][index[to]] = pieceList[them][capture][pieceCount[them][capture]];
    index[pieceList[them][capture][index[to]]] = index[to];

    // Reset rule 50 counter
    st->rule50 = 0;
}


/// Position::do_castle_move() is a private method used to make a castling
/// move. It is called from the main Position::do_move function. Note that
/// castling moves are encoded as "king captures friendly rook" moves, for
/// instance white short castling in a non-Chess960 game is encoded as e1h1.

void Position::do_castle_move(Move m) {

  assert(is_ok());
  assert(move_is_ok(m));
  assert(move_is_castle(m));

  Color us = side_to_move();
  Color them = opposite_color(us);

  // Find source squares for king and rook
  Square kfrom = move_from(m);
  Square rfrom = move_to(m);  // HACK: See comment at beginning of function
  Square kto, rto;

  assert(piece_on(kfrom) == piece_of_color_and_type(us, KING));
  assert(piece_on(rfrom) == piece_of_color_and_type(us, ROOK));

  // Find destination squares for king and rook
  if (rfrom > kfrom) // O-O
  {
      kto = relative_square(us, SQ_G1);
      rto = relative_square(us, SQ_F1);
  } else { // O-O-O
      kto = relative_square(us, SQ_C1);
      rto = relative_square(us, SQ_D1);
  }

  // Remove pieces from source squares
  clear_bit(&(byColorBB[us]), kfrom);
  clear_bit(&(byTypeBB[KING]), kfrom);
  clear_bit(&(byTypeBB[0]), kfrom); // HACK: byTypeBB[0] == occupied squares
  clear_bit(&(byColorBB[us]), rfrom);
  clear_bit(&(byTypeBB[ROOK]), rfrom);
  clear_bit(&(byTypeBB[0]), rfrom); // HACK: byTypeBB[0] == occupied squares

  // Put pieces on destination squares
  set_bit(&(byColorBB[us]), kto);
  set_bit(&(byTypeBB[KING]), kto);
  set_bit(&(byTypeBB[0]), kto); // HACK: byTypeBB[0] == occupied squares
  set_bit(&(byColorBB[us]), rto);
  set_bit(&(byTypeBB[ROOK]), rto);
  set_bit(&(byTypeBB[0]), rto); // HACK: byTypeBB[0] == occupied squares

  // Update board array
  Piece king = piece_of_color_and_type(us, KING);
  Piece rook = piece_of_color_and_type(us, ROOK);
  board[kfrom] = board[rfrom] = EMPTY;
  board[kto] = king;
  board[rto] = rook;

  // Update king square
  kingSquare[us] = kto;

  // Update piece lists
  pieceList[us][KING][index[kfrom]] = kto;
  pieceList[us][ROOK][index[rfrom]] = rto;
  int tmp = index[rfrom];
  index[kto] = index[kfrom];
  index[rto] = tmp;

  // Update incremental scores
  st->mgValue += pst_delta<MidGame>(king, kfrom, kto);
  st->egValue += pst_delta<EndGame>(king, kfrom, kto);
  st->mgValue += pst_delta<MidGame>(rook, rfrom, rto);
  st->egValue += pst_delta<EndGame>(rook, rfrom, rto);

  // Update hash key
  st->key ^= zobrist[us][KING][kfrom] ^ zobrist[us][KING][kto];
  st->key ^= zobrist[us][ROOK][rfrom] ^ zobrist[us][ROOK][rto];

  // Clear en passant square
  if (st->epSquare != SQ_NONE)
  {
      st->key ^= zobEp[st->epSquare];
      st->epSquare = SQ_NONE;
  }

  // Update castling rights
  st->key ^= zobCastle[st->castleRights];
  st->castleRights &= castleRightsMask[kfrom];
  st->key ^= zobCastle[st->castleRights];

  // Reset rule 50 counter
  st->rule50 = 0;

  // Update checkers BB
  st->checkersBB = attacks_to(king_square(them), us);
}


/// Position::do_promotion_move() is a private method used to make a promotion
/// move. It is called from the main Position::do_move function.

void Position::do_promotion_move(Move m) {

  Color us, them;
  Square from, to;
  PieceType promotion;

  assert(is_ok());
  assert(move_is_ok(m));
  assert(move_is_promotion(m));

  us = side_to_move();
  them = opposite_color(us);
  from = move_from(m);
  to = move_to(m);

  assert(relative_rank(us, to) == RANK_8);
  assert(piece_on(from) == piece_of_color_and_type(us, PAWN));
  assert(color_of_piece_on(to) == them || square_is_empty(to));

  st->capture = type_of_piece_on(to);

  if (st->capture)
      do_capture_move(st->capture, them, to);

  // Remove pawn
  clear_bit(&(byColorBB[us]), from);
  clear_bit(&(byTypeBB[PAWN]), from);
  clear_bit(&(byTypeBB[0]), from); // HACK: byTypeBB[0] == occupied squares
  board[from] = EMPTY;

  // Insert promoted piece
  promotion = move_promotion_piece(m);
  assert(promotion >= KNIGHT && promotion <= QUEEN);
  set_bit(&(byColorBB[us]), to);
  set_bit(&(byTypeBB[promotion]), to);
  set_bit(&(byTypeBB[0]), to); // HACK: byTypeBB[0] == occupied squares
  board[to] = piece_of_color_and_type(us, promotion);

  // Update hash key
  st->key ^= zobrist[us][PAWN][from] ^ zobrist[us][promotion][to];

  // Update pawn hash key
  st->pawnKey ^= zobrist[us][PAWN][from];

  // Update material key
  st->materialKey ^= zobMaterial[us][PAWN][pieceCount[us][PAWN]];
  st->materialKey ^= zobMaterial[us][promotion][pieceCount[us][promotion]+1];

  // Update piece counts
  pieceCount[us][PAWN]--;
  pieceCount[us][promotion]++;

  // Update piece lists
  pieceList[us][PAWN][index[from]] = pieceList[us][PAWN][pieceCount[us][PAWN]];
  index[pieceList[us][PAWN][index[from]]] = index[from];
  pieceList[us][promotion][pieceCount[us][promotion] - 1] = to;
  index[to] = pieceCount[us][promotion] - 1;

  // Update incremental scores
  st->mgValue -= pst<MidGame>(us, PAWN, from);
  st->mgValue += pst<MidGame>(us, promotion, to);
  st->egValue -= pst<EndGame>(us, PAWN, from);
  st->egValue += pst<EndGame>(us, promotion, to);

  // Update material
  st->npMaterial[us] += piece_value_midgame(promotion);

  // Clear the en passant square
  if (st->epSquare != SQ_NONE)
  {
      st->key ^= zobEp[st->epSquare];
      st->epSquare = SQ_NONE;
  }

  // Update castle rights
  st->key ^= zobCastle[st->castleRights];
  st->castleRights &= castleRightsMask[to];
  st->key ^= zobCastle[st->castleRights];

  // Reset rule 50 counter
  st->rule50 = 0;

  // Update checkers BB
  st->checkersBB = attacks_to(king_square(them), us);
}


/// Position::do_ep_move() is a private method used to make an en passant
/// capture. It is called from the main Position::do_move function.

void Position::do_ep_move(Move m) {

  Color us, them;
  Square from, to, capsq;

  assert(is_ok());
  assert(move_is_ok(m));
  assert(move_is_ep(m));

  us = side_to_move();
  them = opposite_color(us);
  from = move_from(m);
  to = move_to(m);
  capsq = (us == WHITE)? (to - DELTA_N) : (to - DELTA_S);

  assert(to == st->epSquare);
  assert(relative_rank(us, to) == RANK_6);
  assert(piece_on(to) == EMPTY);
  assert(piece_on(from) == piece_of_color_and_type(us, PAWN));
  assert(piece_on(capsq) == piece_of_color_and_type(them, PAWN));

  // Remove captured pawn
  clear_bit(&(byColorBB[them]), capsq);
  clear_bit(&(byTypeBB[PAWN]), capsq);
  clear_bit(&(byTypeBB[0]), capsq); // HACK: byTypeBB[0] == occupied squares
  board[capsq] = EMPTY;

  // Move capturing pawn
  Bitboard move_bb = make_move_bb(from, to);
  do_move_bb(&(byColorBB[us]), move_bb);
  do_move_bb(&(byTypeBB[PAWN]), move_bb);
  do_move_bb(&(byTypeBB[0]), move_bb); // HACK: byTypeBB[0] == occupied squares
  board[to] = board[from];
  board[from] = EMPTY;

  // Update material hash key
  st->materialKey ^= zobMaterial[them][PAWN][pieceCount[them][PAWN]];

  // Update piece count
  pieceCount[them][PAWN]--;

  // Update piece list
  pieceList[us][PAWN][index[from]] = to;
  index[to] = index[from];
  pieceList[them][PAWN][index[capsq]] = pieceList[them][PAWN][pieceCount[them][PAWN]];
  index[pieceList[them][PAWN][index[capsq]]] = index[capsq];

  // Update hash key
  st->key ^= zobrist[us][PAWN][from] ^ zobrist[us][PAWN][to];
  st->key ^= zobrist[them][PAWN][capsq];
  st->key ^= zobEp[st->epSquare];

  // Update pawn hash key
  st->pawnKey ^= zobrist[us][PAWN][from] ^ zobrist[us][PAWN][to];
  st->pawnKey ^= zobrist[them][PAWN][capsq];

  // Update incremental scores
  Piece pawn = piece_of_color_and_type(us, PAWN);
  st->mgValue += pst_delta<MidGame>(pawn, from, to);
  st->egValue += pst_delta<EndGame>(pawn, from, to);
  st->mgValue -= pst<MidGame>(them, PAWN, capsq);
  st->egValue -= pst<EndGame>(them, PAWN, capsq);

  // Reset en passant square
  st->epSquare = SQ_NONE;

  // Reset rule 50 counter
  st->rule50 = 0;

  // Update checkers BB
  st->checkersBB = attacks_to(king_square(them), us);
}


/// Position::undo_move() unmakes a move. When it returns, the position should
/// be restored to exactly the same state as before the move was made.

void Position::undo_move(Move m) {

  assert(is_ok());
  assert(move_is_ok(m));

  gamePly--;
  sideToMove = opposite_color(sideToMove);

  if (move_is_castle(m))
      undo_castle_move(m);
  else if (move_is_promotion(m))
      undo_promotion_move(m);
  else if (move_is_ep(m))
      undo_ep_move(m);
  else
  {
      Color us, them;
      Square from, to;
      PieceType piece;

      us = side_to_move();
      them = opposite_color(us);
      from = move_from(m);
      to = move_to(m);

      assert(piece_on(from) == EMPTY);
      assert(color_of_piece_on(to) == us);

      // Put the piece back at the source square
      Bitboard move_bb = make_move_bb(to, from);
      piece = type_of_piece_on(to);
      do_move_bb(&(byColorBB[us]), move_bb);
      do_move_bb(&(byTypeBB[piece]), move_bb);
      do_move_bb(&(byTypeBB[0]), move_bb); // HACK: byTypeBB[0] == occupied squares
      board[from] = piece_of_color_and_type(us, piece);

      // If the moving piece was a king, update the king square
      if (piece == KING)
          kingSquare[us] = from;

      // Update piece list
      pieceList[us][piece][index[to]] = from;
      index[from] = index[to];

      if (st->capture)
      {
          assert(st->capture != KING);

          // Restore the captured piece
          set_bit(&(byColorBB[them]), to);
          set_bit(&(byTypeBB[st->capture]), to);
          set_bit(&(byTypeBB[0]), to);
          board[to] = piece_of_color_and_type(them, st->capture);

          // Update piece list
          pieceList[them][st->capture][pieceCount[them][st->capture]] = to;
          index[to] = pieceCount[them][st->capture];

          // Update piece count
          pieceCount[them][st->capture]++;
      } else
          board[to] = EMPTY;
  }

  // Finally point our state pointer back to the previous state
  st = st->previous;

  assert(is_ok());
}


/// Position::undo_castle_move() is a private method used to unmake a castling
/// move. It is called from the main Position::undo_move function. Note that
/// castling moves are encoded as "king captures friendly rook" moves, for
/// instance white short castling in a non-Chess960 game is encoded as e1h1.

void Position::undo_castle_move(Move m) {

  assert(move_is_ok(m));
  assert(move_is_castle(m));

  // When we have arrived here, some work has already been done by
  // Position::undo_move.  In particular, the side to move has been switched,
  // so the code below is correct.
  Color us = side_to_move();

  // Find source squares for king and rook
  Square kfrom = move_from(m);
  Square rfrom = move_to(m);  // HACK: See comment at beginning of function
  Square kto, rto;

  // Find destination squares for king and rook
  if (rfrom > kfrom) // O-O
  {
      kto = relative_square(us, SQ_G1);
      rto = relative_square(us, SQ_F1);
  } else { // O-O-O
      kto = relative_square(us, SQ_C1);
      rto = relative_square(us, SQ_D1);
  }

  assert(piece_on(kto) == piece_of_color_and_type(us, KING));
  assert(piece_on(rto) == piece_of_color_and_type(us, ROOK));

  // Remove pieces from destination squares
  clear_bit(&(byColorBB[us]), kto);
  clear_bit(&(byTypeBB[KING]), kto);
  clear_bit(&(byTypeBB[0]), kto); // HACK: byTypeBB[0] == occupied squares
  clear_bit(&(byColorBB[us]), rto);
  clear_bit(&(byTypeBB[ROOK]), rto);
  clear_bit(&(byTypeBB[0]), rto); // HACK: byTypeBB[0] == occupied squares

  // Put pieces on source squares
  set_bit(&(byColorBB[us]), kfrom);
  set_bit(&(byTypeBB[KING]), kfrom);
  set_bit(&(byTypeBB[0]), kfrom); // HACK: byTypeBB[0] == occupied squares
  set_bit(&(byColorBB[us]), rfrom);
  set_bit(&(byTypeBB[ROOK]), rfrom);
  set_bit(&(byTypeBB[0]), rfrom); // HACK: byTypeBB[0] == occupied squares

  // Update board
  board[rto] = board[kto] = EMPTY;
  board[rfrom] = piece_of_color_and_type(us, ROOK);
  board[kfrom] = piece_of_color_and_type(us, KING);

  // Update king square
  kingSquare[us] = kfrom;

  // Update piece lists
  pieceList[us][KING][index[kto]] = kfrom;
  pieceList[us][ROOK][index[rto]] = rfrom;
  int tmp = index[rto];  // Necessary because we may have rto == kfrom in FRC.
  index[kfrom] = index[kto];
  index[rfrom] = tmp;
}


/// Position::undo_promotion_move() is a private method used to unmake a
/// promotion move. It is called from the main Position::do_move
/// function.

void Position::undo_promotion_move(Move m) {

  Color us, them;
  Square from, to;
  PieceType promotion;

  assert(move_is_ok(m));
  assert(move_is_promotion(m));

  // When we have arrived here, some work has already been done by
  // Position::undo_move.  In particular, the side to move has been switched,
  // so the code below is correct.
  us = side_to_move();
  them = opposite_color(us);
  from = move_from(m);
  to = move_to(m);

  assert(relative_rank(us, to) == RANK_8);
  assert(piece_on(from) == EMPTY);

  // Remove promoted piece
  promotion = move_promotion_piece(m);
  assert(piece_on(to)==piece_of_color_and_type(us, promotion));
  assert(promotion >= KNIGHT && promotion <= QUEEN);
  clear_bit(&(byColorBB[us]), to);
  clear_bit(&(byTypeBB[promotion]), to);
  clear_bit(&(byTypeBB[0]), to); // HACK: byTypeBB[0] == occupied squares

  // Insert pawn at source square
  set_bit(&(byColorBB[us]), from);
  set_bit(&(byTypeBB[PAWN]), from);
  set_bit(&(byTypeBB[0]), from); // HACK: byTypeBB[0] == occupied squares
  board[from] = piece_of_color_and_type(us, PAWN);

  // Update piece list
  pieceList[us][PAWN][pieceCount[us][PAWN]] = from;
  index[from] = pieceCount[us][PAWN];
  pieceList[us][promotion][index[to]] =
    pieceList[us][promotion][pieceCount[us][promotion] - 1];
  index[pieceList[us][promotion][index[to]]] = index[to];

  // Update piece counts
  pieceCount[us][promotion]--;
  pieceCount[us][PAWN]++;

  if (st->capture)
  {
      assert(st->capture != KING);

      // Insert captured piece:
      set_bit(&(byColorBB[them]), to);
      set_bit(&(byTypeBB[st->capture]), to);
      set_bit(&(byTypeBB[0]), to); // HACK: byTypeBB[0] == occupied squares
      board[to] = piece_of_color_and_type(them, st->capture);

      // Update piece list
      pieceList[them][st->capture][pieceCount[them][st->capture]] = to;
      index[to] = pieceCount[them][st->capture];

      // Update piece count
      pieceCount[them][st->capture]++;
  } else
      board[to] = EMPTY;
}


/// Position::undo_ep_move() is a private method used to unmake an en passant
/// capture. It is called from the main Position::undo_move function.

void Position::undo_ep_move(Move m) {

  assert(move_is_ok(m));
  assert(move_is_ep(m));

  // When we have arrived here, some work has already been done by
  // Position::undo_move. In particular, the side to move has been switched,
  // so the code below is correct.
  Color us = side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Square to = move_to(m);
  Square capsq = (us == WHITE)? (to - DELTA_N) : (to - DELTA_S);

  assert(to == st->previous->epSquare);
  assert(relative_rank(us, to) == RANK_6);
  assert(piece_on(to) == piece_of_color_and_type(us, PAWN));
  assert(piece_on(from) == EMPTY);
  assert(piece_on(capsq) == EMPTY);

  // Restore captured pawn
  set_bit(&(byColorBB[them]), capsq);
  set_bit(&(byTypeBB[PAWN]), capsq);
  set_bit(&(byTypeBB[0]), capsq);
  board[capsq] = piece_of_color_and_type(them, PAWN);

  // Move capturing pawn back to source square
  Bitboard move_bb = make_move_bb(to, from);
  do_move_bb(&(byColorBB[us]), move_bb);
  do_move_bb(&(byTypeBB[PAWN]), move_bb);
  do_move_bb(&(byTypeBB[0]), move_bb);
  board[to] = EMPTY;
  board[from] = piece_of_color_and_type(us, PAWN);

  // Update piece list
  pieceList[us][PAWN][index[to]] = from;
  index[from] = index[to];
  pieceList[them][PAWN][pieceCount[them][PAWN]] = capsq;
  index[capsq] = pieceCount[them][PAWN];

  // Update piece count
  pieceCount[them][PAWN]++;
}


/// Position::do_null_move makes() a "null move": It switches the side to move
/// and updates the hash key without executing any move on the board.

void Position::do_null_move(StateInfo& backupSt) {

  assert(is_ok());
  assert(!is_check());

  // Back up the information necessary to undo the null move to the supplied
  // StateInfo object.
  // Note that differently from normal case here backupSt is actually used as
  // a backup storage not as a new state to be used.
  backupSt.epSquare = st->epSquare;
  backupSt.key = st->key;
  backupSt.mgValue = st->mgValue;
  backupSt.egValue = st->egValue;
  backupSt.previous = st->previous;
  st->previous = &backupSt;

  // Save the current key to the history[] array, in order to be able to
  // detect repetition draws.
  history[gamePly] = st->key;

  // Update the necessary information
  sideToMove = opposite_color(sideToMove);
  if (st->epSquare != SQ_NONE)
      st->key ^= zobEp[st->epSquare];

  st->epSquare = SQ_NONE;
  st->rule50++;
  gamePly++;
  st->key ^= zobSideToMove;

  st->mgValue += (sideToMove == WHITE)? TempoValueMidgame : -TempoValueMidgame;
  st->egValue += (sideToMove == WHITE)? TempoValueEndgame : -TempoValueEndgame;

  assert(is_ok());
}


/// Position::undo_null_move() unmakes a "null move".

void Position::undo_null_move() {

  assert(is_ok());
  assert(!is_check());

  // Restore information from the our backup StateInfo object
  st->epSquare = st->previous->epSquare;
  st->key = st->previous->key;
  st->mgValue = st->previous->mgValue;
  st->egValue = st->previous->egValue;
  st->previous = st->previous->previous;

  // Update the necessary information
  sideToMove = opposite_color(sideToMove);
  st->rule50--;
  gamePly--;

  assert(is_ok());
}


/// Position::see() is a static exchange evaluator: It tries to estimate the
/// material gain or loss resulting from a move. There are three versions of
/// this function: One which takes a destination square as input, one takes a
/// move, and one which takes a 'from' and a 'to' square. The function does
/// not yet understand promotions captures.

int Position::see(Square to) const {

  assert(square_is_ok(to));
  return see(SQ_NONE, to);
}

int Position::see(Move m) const {

  assert(move_is_ok(m));
  return see(move_from(m), move_to(m));
}

int Position::see_sign(Move m) const {

  assert(move_is_ok(m));

  Square from = move_from(m);
  Square to = move_to(m);

  // Early return if SEE cannot be negative because capturing piece value
  // is not bigger then captured one.
  if (   midgame_value_of_piece_on(from) <= midgame_value_of_piece_on(to)
      && type_of_piece_on(from) != KING)
         return 1;

  return see(from, to);
}

int Position::see(Square from, Square to) const {

  // Material values
  static const int seeValues[18] = {
    0, PawnValueMidgame, KnightValueMidgame, BishopValueMidgame,
       RookValueMidgame, QueenValueMidgame, QueenValueMidgame*10, 0,
    0, PawnValueMidgame, KnightValueMidgame, BishopValueMidgame,
       RookValueMidgame, QueenValueMidgame, QueenValueMidgame*10, 0,
    0, 0
  };

  Bitboard attackers, stmAttackers, occ, b;

  assert(square_is_ok(from) || from == SQ_NONE);
  assert(square_is_ok(to));

  // Initialize colors
  Color us = (from != SQ_NONE ? color_of_piece_on(from) : opposite_color(color_of_piece_on(to)));
  Color them = opposite_color(us);

  // Initialize pieces
  Piece piece = piece_on(from);
  Piece capture = piece_on(to);

  // Find all attackers to the destination square, with the moving piece
  // removed, but possibly an X-ray attacker added behind it.
  occ = occupied_squares();

  // Handle en passant moves
  if (st->epSquare == to && type_of_piece_on(from) == PAWN)
  {
      assert(capture == EMPTY);

      Square capQq = (side_to_move() == WHITE)? (to - DELTA_N) : (to - DELTA_S);
      capture = piece_on(capQq);
      assert(type_of_piece_on(capQq) == PAWN);

      // Remove the captured pawn
      clear_bit(&occ, capQq);
  }

  while (true)
  {
      clear_bit(&occ, from);
      attackers =  (rook_attacks_bb(to, occ)   & rooks_and_queens())
                 | (bishop_attacks_bb(to, occ) & bishops_and_queens())
                 | (piece_attacks<KNIGHT>(to)  & knights())
                 | (piece_attacks<KING>(to)    & kings())
                 | (pawn_attacks(WHITE, to)    & pawns(BLACK))
                 | (pawn_attacks(BLACK, to)    & pawns(WHITE));

      if (from != SQ_NONE)
          break;

      // If we don't have any attacker we are finished
      if ((attackers & pieces_of_color(us)) == EmptyBoardBB)
          return 0;

      // Locate the least valuable attacker to the destination square
      // and use it to initialize from square.
      PieceType pt;
      for (pt = PAWN; !(attackers & pieces_of_color_and_type(us, pt)); pt++)
          assert(pt < KING);

      from = first_1(attackers & pieces_of_color_and_type(us, pt));
      piece = piece_on(from);
  }

  // If the opponent has no attackers we are finished
  stmAttackers = attackers & pieces_of_color(them);
  if (!stmAttackers)
      return seeValues[capture];

  attackers &= occ; // Remove the moving piece

  // The destination square is defended, which makes things rather more
  // difficult to compute. We proceed by building up a "swap list" containing
  // the material gain or loss at each stop in a sequence of captures to the
  // destination square, where the sides alternately capture, and always
  // capture with the least valuable piece. After each capture, we look for
  // new X-ray attacks from behind the capturing piece.
  int lastCapturingPieceValue = seeValues[piece];
  int swapList[32], n = 1;
  Color c = them;
  PieceType pt;

  swapList[0] = seeValues[capture];

  do {
      // Locate the least valuable attacker for the side to move. The loop
      // below looks like it is potentially infinite, but it isn't. We know
      // that the side to move still has at least one attacker left.
      for (pt = PAWN; !(stmAttackers & pieces_of_type(pt)); pt++)
          assert(pt < KING);

      // Remove the attacker we just found from the 'attackers' bitboard,
      // and scan for new X-ray attacks behind the attacker.
      b = stmAttackers & pieces_of_type(pt);
      occ ^= (b & (~b + 1));
      attackers |=  (rook_attacks_bb(to, occ) & rooks_and_queens())
                  | (bishop_attacks_bb(to, occ) & bishops_and_queens());

      attackers &= occ;

      // Add the new entry to the swap list
      assert(n < 32);
      swapList[n] = -swapList[n - 1] + lastCapturingPieceValue;
      n++;

      // Remember the value of the capturing piece, and change the side to move
      // before beginning the next iteration
      lastCapturingPieceValue = seeValues[pt];
      c = opposite_color(c);
      stmAttackers = attackers & pieces_of_color(c);

      // Stop after a king capture
      if (pt == KING && stmAttackers)
      {
          assert(n < 32);
          swapList[n++] = QueenValueMidgame*10;
          break;
      }
  } while (stmAttackers);

  // Having built the swap list, we negamax through it to find the best
  // achievable score from the point of view of the side to move
  while (--n)
      swapList[n-1] = Min(-swapList[n], swapList[n-1]);

  return swapList[0];
}


/// Position::saveState() copies the content of the current state
/// inside startState and makes st point to it. This is needed
/// when the st pointee could become stale, as example because
/// the caller is about to going out of scope.

void Position::saveState() {

  startState = *st;
  st = &startState;
  st->previous = NULL; // as a safe guard
}


/// Position::clear() erases the position object to a pristine state, with an
/// empty board, white to move, and no castling rights.

void Position::clear() {

  st = &startState;
  memset(st, 0, sizeof(StateInfo));
  st->epSquare = SQ_NONE;

  memset(index, 0, sizeof(int) * 64);
  memset(byColorBB, 0, sizeof(Bitboard) * 2);

  for (int i = 0; i < 64; i++)
      board[i] = EMPTY;

  for (int i = 0; i < 7; i++)
  {
      byTypeBB[i] = EmptyBoardBB;
      pieceCount[0][i] = pieceCount[1][i] = 0;
      for (int j = 0; j < 8; j++)
          pieceList[0][i][j] = pieceList[1][i][j] = SQ_NONE;
  }

  sideToMove = WHITE;
  gamePly = 0;
  initialKFile = FILE_E;
  initialKRFile = FILE_H;
  initialQRFile = FILE_A;
}


/// Position::reset_game_ply() simply sets gamePly to 0. It is used from the
/// UCI interface code, whenever a non-reversible move is made in a
/// 'position fen <fen> moves m1 m2 ...' command.  This makes it possible
/// for the program to handle games of arbitrary length, as long as the GUI
/// handles draws by the 50 move rule correctly.

void Position::reset_game_ply() {

  gamePly = 0;
}


/// Position::put_piece() puts a piece on the given square of the board,
/// updating the board array, bitboards, and piece counts.

void Position::put_piece(Piece p, Square s) {

  Color c = color_of_piece(p);
  PieceType pt = type_of_piece(p);

  board[s] = p;
  index[s] = pieceCount[c][pt];
  pieceList[c][pt][index[s]] = s;

  set_bit(&(byTypeBB[pt]), s);
  set_bit(&(byColorBB[c]), s);
  set_bit(&byTypeBB[0], s); // HACK: byTypeBB[0] contains all occupied squares.

  pieceCount[c][pt]++;

  if (pt == KING)
      kingSquare[c] = s;
}


/// Position::allow_oo() gives the given side the right to castle kingside.
/// Used when setting castling rights during parsing of FEN strings.

void Position::allow_oo(Color c) {

  st->castleRights |= (1 + int(c));
}


/// Position::allow_ooo() gives the given side the right to castle queenside.
/// Used when setting castling rights during parsing of FEN strings.

void Position::allow_ooo(Color c) {

  st->castleRights |= (4 + 4*int(c));
}


/// Position::compute_key() computes the hash key of the position. The hash
/// key is usually updated incrementally as moves are made and unmade, the
/// compute_key() function is only used when a new position is set up, and
/// to verify the correctness of the hash key when running in debug mode.

Key Position::compute_key() const {

  Key result = Key(0ULL);

  for (Square s = SQ_A1; s <= SQ_H8; s++)
      if (square_is_occupied(s))
          result ^= zobrist[color_of_piece_on(s)][type_of_piece_on(s)][s];

  if (ep_square() != SQ_NONE)
      result ^= zobEp[ep_square()];

  result ^= zobCastle[st->castleRights];
  if (side_to_move() == BLACK)
      result ^= zobSideToMove;

  return result;
}


/// Position::compute_pawn_key() computes the hash key of the position. The
/// hash key is usually updated incrementally as moves are made and unmade,
/// the compute_pawn_key() function is only used when a new position is set
/// up, and to verify the correctness of the pawn hash key when running in
/// debug mode.

Key Position::compute_pawn_key() const {

  Key result = Key(0ULL);
  Bitboard b;
  Square s;

  for (Color c = WHITE; c <= BLACK; c++)
  {
      b = pawns(c);
      while(b)
      {
          s = pop_1st_bit(&b);
          result ^= zobrist[c][PAWN][s];
      }
  }
  return result;
}


/// Position::compute_material_key() computes the hash key of the position.
/// The hash key is usually updated incrementally as moves are made and unmade,
/// the compute_material_key() function is only used when a new position is set
/// up, and to verify the correctness of the material hash key when running in
/// debug mode.

Key Position::compute_material_key() const {

  Key result = Key(0ULL);
  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= QUEEN; pt++)
      {
          int count = piece_count(c, pt);
          for (int i = 0; i <= count; i++)
              result ^= zobMaterial[c][pt][i];
      }
  return result;
}


/// Position::compute_value() compute the incremental scores for the middle
/// game and the endgame. These functions are used to initialize the incremental
/// scores when a new position is set up, and to verify that the scores are correctly
/// updated by do_move and undo_move when the program is running in debug mode.
template<Position::GamePhase Phase>
Value Position::compute_value() const {

  Value result = Value(0);
  Bitboard b;
  Square s;

  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= KING; pt++)
      {
          b = pieces_of_color_and_type(c, pt);
          while(b)
          {
              s = pop_1st_bit(&b);
              assert(piece_on(s) == piece_of_color_and_type(c, pt));
              result += pst<Phase>(c, pt, s);
          }
      }

  const Value TempoValue = (Phase == MidGame ? TempoValueMidgame : TempoValueEndgame);
  result += (side_to_move() == WHITE)? TempoValue / 2 : -TempoValue / 2;
  return result;
}


/// Position::compute_non_pawn_material() computes the total non-pawn middle
/// game material score for the given side. Material scores are updated
/// incrementally during the search, this function is only used while
/// initializing a new Position object.

Value Position::compute_non_pawn_material(Color c) const {

  Value result = Value(0);

  for (PieceType pt = KNIGHT; pt <= QUEEN; pt++)
  {
      Bitboard b = pieces_of_color_and_type(c, pt);
      while (b)
      {
          assert(piece_on(first_1(b)) == piece_of_color_and_type(c, pt));
          pop_1st_bit(&b);
          result += piece_value_midgame(pt);
      }
  }
  return result;
}


/// Position::is_draw() tests whether the position is drawn by material,
/// repetition, or the 50 moves rule. It does not detect stalemates, this
/// must be done by the search.

bool Position::is_draw() const {

  // Draw by material?
  if (   !pawns()
      && (non_pawn_material(WHITE) + non_pawn_material(BLACK) <= BishopValueMidgame))
      return true;

  // Draw by the 50 moves rule?
  if (st->rule50 > 100 || (st->rule50 == 100 && !is_check()))
      return true;

  // Draw by repetition?
  for (int i = 2; i < Min(gamePly, st->rule50); i += 2)
      if (history[gamePly - i] == st->key)
          return true;

  return false;
}


/// Position::is_mate() returns true or false depending on whether the
/// side to move is checkmated.

bool Position::is_mate() const {

  MoveStack moves[256];

  return is_check() && !generate_evasions(*this, moves, pinned_pieces(sideToMove));
}


/// Position::has_mate_threat() tests whether a given color has a mate in one
/// from the current position.

bool Position::has_mate_threat(Color c) {

  StateInfo st1, st2;
  Color stm = side_to_move();

  if (is_check())
      return false;

  // If the input color is not equal to the side to move, do a null move
  if (c != stm)
      do_null_move(st1);

  MoveStack mlist[120];
  int count;
  bool result = false;
  Bitboard dc = discovered_check_candidates(sideToMove);
  Bitboard pinned = pinned_pieces(sideToMove);

  // Generate pseudo-legal non-capture and capture check moves
  count = generate_non_capture_checks(*this, mlist, dc);
  count += generate_captures(*this, mlist + count);

  // Loop through the moves, and see if one of them is mate
  for (int i = 0; i < count; i++)
  {
      Move move = mlist[i].move;

      if (!pl_move_is_legal(move, pinned))
          continue;

      do_move(move, st2);
      if (is_mate())
          result = true;

      undo_move(move);
  }

  // Undo null move, if necessary
  if (c != stm)
      undo_null_move();

  return result;
}


/// Position::init_zobrist() is a static member function which initializes the
/// various arrays used to compute hash keys.

void Position::init_zobrist() {

  for (int i = 0; i < 2; i++)
      for (int j = 0; j < 8; j++)
          for (int k = 0; k < 64; k++)
              zobrist[i][j][k] = Key(genrand_int64());

  for (int i = 0; i < 64; i++)
      zobEp[i] = Key(genrand_int64());

  for (int i = 0; i < 16; i++)
      zobCastle[i] = genrand_int64();

  zobSideToMove = genrand_int64();

  for (int i = 0; i < 2; i++)
      for (int j = 0; j < 8; j++)
          for (int k = 0; k < 16; k++)
              zobMaterial[i][j][k] = (k > 0)? Key(genrand_int64()) : Key(0LL);

  for (int i = 0; i < 16; i++)
      zobMaterial[0][KING][i] = zobMaterial[1][KING][i] = Key(0ULL);
}


/// Position::init_piece_square_tables() initializes the piece square tables.
/// This is a two-step operation:  First, the white halves of the tables are
/// copied from the MgPST[][] and EgPST[][] arrays, with a small random number
/// added to each entry if the "Randomness" UCI parameter is non-zero.
/// Second, the black halves of the tables are initialized by mirroring
/// and changing the sign of the corresponding white scores.

void Position::init_piece_square_tables() {

  int r = get_option_value_int("Randomness"), i;
  for (Square s = SQ_A1; s <= SQ_H8; s++)
      for (Piece p = WP; p <= WK; p++)
      {
          i = (r == 0)? 0 : (genrand_int32() % (r*2) - r);
          MgPieceSquareTable[p][s] = Value(MgPST[p][s] + i);
          EgPieceSquareTable[p][s] = Value(EgPST[p][s] + i);
      }

  for (Square s = SQ_A1; s <= SQ_H8; s++)
      for (Piece p = BP; p <= BK; p++)
      {
          MgPieceSquareTable[p][s] = -MgPieceSquareTable[p-8][flip_square(s)];
          EgPieceSquareTable[p][s] = -EgPieceSquareTable[p-8][flip_square(s)];
      }
}


/// Position::flipped_copy() makes a copy of the input position, but with
/// the white and black sides reversed. This is only useful for debugging,
/// especially for finding evaluation symmetry bugs.

void Position::flipped_copy(const Position& pos) {

  assert(pos.is_ok());

  clear();

  // Board
  for (Square s = SQ_A1; s <= SQ_H8; s++)
      if (!pos.square_is_empty(s))
          put_piece(Piece(int(pos.piece_on(s)) ^ 8), flip_square(s));

  // Side to move
  sideToMove = opposite_color(pos.side_to_move());

  // Castling rights
  if (pos.can_castle_kingside(WHITE))  allow_oo(BLACK);
  if (pos.can_castle_queenside(WHITE)) allow_ooo(BLACK);
  if (pos.can_castle_kingside(BLACK))  allow_oo(WHITE);
  if (pos.can_castle_queenside(BLACK)) allow_ooo(WHITE);

  initialKFile  = pos.initialKFile;
  initialKRFile = pos.initialKRFile;
  initialQRFile = pos.initialQRFile;

  for (Square sq = SQ_A1; sq <= SQ_H8; sq++)
      castleRightsMask[sq] = ALL_CASTLES;

  castleRightsMask[make_square(initialKFile,  RANK_1)] ^= (WHITE_OO | WHITE_OOO);
  castleRightsMask[make_square(initialKFile,  RANK_8)] ^= (BLACK_OO | BLACK_OOO);
  castleRightsMask[make_square(initialKRFile, RANK_1)] ^=  WHITE_OO;
  castleRightsMask[make_square(initialKRFile, RANK_8)] ^=  BLACK_OO;
  castleRightsMask[make_square(initialQRFile, RANK_1)] ^=  WHITE_OOO;
  castleRightsMask[make_square(initialQRFile, RANK_8)] ^=  BLACK_OOO;

  // En passant square
  if (pos.st->epSquare != SQ_NONE)
      st->epSquare = flip_square(pos.st->epSquare);

  // Checkers
  find_checkers();

  // Hash keys
  st->key = compute_key();
  st->pawnKey = compute_pawn_key();
  st->materialKey = compute_material_key();

  // Incremental scores
  st->mgValue = compute_value<MidGame>();
  st->egValue = compute_value<EndGame>();

  // Material
  st->npMaterial[WHITE] = compute_non_pawn_material(WHITE);
  st->npMaterial[BLACK] = compute_non_pawn_material(BLACK);

  assert(is_ok());
}


/// Position::is_ok() performs some consitency checks for the position object.
/// This is meant to be helpful when debugging.

bool Position::is_ok(int* failedStep) const {

  // What features of the position should be verified?
  static const bool debugBitboards = false;
  static const bool debugKingCount = false;
  static const bool debugKingCapture = false;
  static const bool debugCheckerCount = false;
  static const bool debugKey = false;
  static const bool debugMaterialKey = false;
  static const bool debugPawnKey = false;
  static const bool debugIncrementalEval = false;
  static const bool debugNonPawnMaterial = false;
  static const bool debugPieceCounts = false;
  static const bool debugPieceList = false;

  if (failedStep) *failedStep = 1;

  // Side to move OK?
  if (!color_is_ok(side_to_move()))
      return false;

  // Are the king squares in the position correct?
  if (failedStep) (*failedStep)++;
  if (piece_on(king_square(WHITE)) != WK)
      return false;

  if (failedStep) (*failedStep)++;
  if (piece_on(king_square(BLACK)) != BK)
      return false;

  // Castle files OK?
  if (failedStep) (*failedStep)++;
  if (!file_is_ok(initialKRFile))
      return false;

  if (!file_is_ok(initialQRFile))
      return false;

  // Do both sides have exactly one king?
  if (failedStep) (*failedStep)++;
  if (debugKingCount)
  {
      int kingCount[2] = {0, 0};
      for (Square s = SQ_A1; s <= SQ_H8; s++)
          if (type_of_piece_on(s) == KING)
              kingCount[color_of_piece_on(s)]++;

      if (kingCount[0] != 1 || kingCount[1] != 1)
          return false;
  }

  // Can the side to move capture the opponent's king?
  if (failedStep) (*failedStep)++;
  if (debugKingCapture)
  {
      Color us = side_to_move();
      Color them = opposite_color(us);
      Square ksq = king_square(them);
      if (square_is_attacked(ksq, us))
          return false;
  }

  // Is there more than 2 checkers?
  if (failedStep) (*failedStep)++;
  if (debugCheckerCount && count_1s(st->checkersBB) > 2)
      return false;

  // Bitboards OK?
  if (failedStep) (*failedStep)++;
  if (debugBitboards)
  {
      // The intersection of the white and black pieces must be empty
      if ((pieces_of_color(WHITE) & pieces_of_color(BLACK)) != EmptyBoardBB)
          return false;

      // The union of the white and black pieces must be equal to all
      // occupied squares
      if ((pieces_of_color(WHITE) | pieces_of_color(BLACK)) != occupied_squares())
          return false;

      // Separate piece type bitboards must have empty intersections
      for (PieceType p1 = PAWN; p1 <= KING; p1++)
          for (PieceType p2 = PAWN; p2 <= KING; p2++)
              if (p1 != p2 && (pieces_of_type(p1) & pieces_of_type(p2)))
                  return false;
  }

  // En passant square OK?
  if (failedStep) (*failedStep)++;
  if (ep_square() != SQ_NONE)
  {
      // The en passant square must be on rank 6, from the point of view of the
      // side to move.
      if (relative_rank(side_to_move(), ep_square()) != RANK_6)
          return false;
  }

  // Hash key OK?
  if (failedStep) (*failedStep)++;
  if (debugKey && st->key != compute_key())
      return false;

  // Pawn hash key OK?
  if (failedStep) (*failedStep)++;
  if (debugPawnKey && st->pawnKey != compute_pawn_key())
      return false;

  // Material hash key OK?
  if (failedStep) (*failedStep)++;
  if (debugMaterialKey && st->materialKey != compute_material_key())
      return false;

  // Incremental eval OK?
  if (failedStep) (*failedStep)++;
  if (debugIncrementalEval)
  {
      if (st->mgValue != compute_value<MidGame>())
          return false;

      if (st->egValue != compute_value<EndGame>())
          return false;
  }

  // Non-pawn material OK?
  if (failedStep) (*failedStep)++;
  if (debugNonPawnMaterial)
  {
      if (st->npMaterial[WHITE] != compute_non_pawn_material(WHITE))
          return false;

      if (st->npMaterial[BLACK] != compute_non_pawn_material(BLACK))
          return false;
  }

  // Piece counts OK?
  if (failedStep) (*failedStep)++;
  if (debugPieceCounts)
      for (Color c = WHITE; c <= BLACK; c++)
          for (PieceType pt = PAWN; pt <= KING; pt++)
              if (pieceCount[c][pt] != count_1s(pieces_of_color_and_type(c, pt)))
                  return false;

  if (failedStep) (*failedStep)++;
  if (debugPieceList)
  {
      for(Color c = WHITE; c <= BLACK; c++)
          for(PieceType pt = PAWN; pt <= KING; pt++)
              for(int i = 0; i < pieceCount[c][pt]; i++)
              {
                  if (piece_on(piece_list(c, pt, i)) != piece_of_color_and_type(c, pt))
                      return false;

                  if (index[piece_list(c, pt, i)] != i)
                      return false;
              }
  }
  if (failedStep) *failedStep = 0;
  return true;
}
