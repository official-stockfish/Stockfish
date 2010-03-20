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
#include "tt.h"
#include "ucioption.h"

using std::string;


////
//// Variables
////

Key Position::zobrist[2][8][64];
Key Position::zobEp[64];
Key Position::zobCastle[16];
Key Position::zobMaterial[2][8][16];
Key Position::zobSideToMove;
Key Position::zobExclusion;

Score Position::PieceSquareTable[16][64];

static bool RequestPending = false;


/// Constructors

CheckInfo::CheckInfo(const Position& pos) {

  Color us = pos.side_to_move();
  Color them = opposite_color(us);

  ksq = pos.king_square(them);
  dcCandidates = pos.discovered_check_candidates(us);

  checkSq[PAWN] = pos.attacks_from<PAWN>(ksq, them);
  checkSq[KNIGHT] = pos.attacks_from<KNIGHT>(ksq);
  checkSq[BISHOP] = pos.attacks_from<BISHOP>(ksq);
  checkSq[ROOK] = pos.attacks_from<ROOK>(ksq);
  checkSq[QUEEN] = checkSq[BISHOP] | checkSq[ROOK];
  checkSq[KING] = EmptyBoardBB;
}


/// Position c'tors. Here we always create a slower but safer copy of
/// the original position or the FEN string, we want the new born Position
/// object do not depend on any external data. Instead if we know what we
/// are doing and we need speed we can create a position with default
/// c'tor Position() and then use just fast_copy().

Position::Position() {}

Position::Position(const Position& pos) {

  memcpy(this, &pos, sizeof(Position));
  detach(); // Always detach() in copy c'tor to avoid surprises
}

Position::Position(const string& fen) {

  from_fen(fen);
}


/// Position::detach() copies the content of the current state and castling
/// masks inside the position itself. This is needed when the st pointee could
/// become stale, as example because the caller is about to going out of scope.

void Position::detach() {

  startState = *st;
  st = &startState;
  st->previous = NULL; // as a safe guard
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
  while (strchr("KQkqabcdefghABCDEFGH-", fen[i])) {
      if (fen[i] == '-')
      {
          i++;
          break;
      }
      else if (fen[i] == 'K') allow_oo(WHITE);
      else if (fen[i] == 'Q') allow_ooo(WHITE);
      else if (fen[i] == 'k') allow_oo(BLACK);
      else if (fen[i] == 'q') allow_ooo(BLACK);
      else if (fen[i] >= 'A' && fen[i] <= 'H') {
          File rookFile, kingFile = FILE_NONE;
          for (Square square = SQ_B1; square <= SQ_G1; square++)
              if (piece_on(square) == WK)
                  kingFile = square_file(square);
          if (kingFile == FILE_NONE) {
              std::cout << "Error in FEN at character " << i << std::endl;
              return;
          }
          initialKFile = kingFile;
          rookFile = File(fen[i] - 'A') + FILE_A;
          if (rookFile < initialKFile) {
              allow_ooo(WHITE);
              initialQRFile = rookFile;
          }
          else {
              allow_oo(WHITE);
              initialKRFile = rookFile;
          }
      }
      else if (fen[i] >= 'a' && fen[i] <= 'h') {
          File rookFile, kingFile = FILE_NONE;
          for (Square square = SQ_B8; square <= SQ_G8; square++)
              if (piece_on(square) == BK)
                  kingFile = square_file(square);
          if (kingFile == FILE_NONE) {
              std::cout << "Error in FEN at character " << i << std::endl;
              return;
          }
          initialKFile = kingFile;
          rookFile = File(fen[i] - 'a') + FILE_A;
          if (rookFile < initialKFile) {
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

  // En passant square -- ignore if no capture is possible
  if (    i <= fen.length() - 2
      && (fen[i] >= 'a' && fen[i] <= 'h')
      && (fen[i+1] == '3' || fen[i+1] == '6'))
  {
      Square fenEpSquare = square_from_string(fen.substr(i, 2));
      Color them = opposite_color(sideToMove);
      if (attacks_from<PAWN>(fenEpSquare, them) & this->pieces(PAWN, sideToMove))
          st->epSquare = square_from_string(fen.substr(i, 2));
  }

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
  st->value = compute_value();
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
     if (initialKFile == FILE_E && initialQRFile == FILE_A && initialKRFile == FILE_H)
     {
        if (can_castle_kingside(WHITE))  fen += 'K';
        if (can_castle_queenside(WHITE)) fen += 'Q';
        if (can_castle_kingside(BLACK))  fen += 'k';
        if (can_castle_queenside(BLACK)) fen += 'q';
     }
     else
     {
        if (can_castle_kingside(WHITE))
           fen += char(toupper(file_to_char(initialKRFile)));
        if (can_castle_queenside(WHITE))
           fen += char(toupper(file_to_char(initialQRFile)));
        if (can_castle_kingside(BLACK))
           fen += file_to_char(initialKRFile);
        if (can_castle_queenside(BLACK))
           fen += file_to_char(initialQRFile);
     }
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
      Position p(*this);
      string col = (color_of_piece_on(move_from(m)) == BLACK ? ".." : "");
      std::cout << "Move is: " << col << move_to_san(p, m) << std::endl;
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


/// Position:hidden_checkers<>() returns a bitboard of all pinned (against the
/// king) pieces for the given color and for the given pinner type. Or, when
/// template parameter FindPinned is false, the pieces of the given color
/// candidate for a discovery check against the enemy king.
/// Bitboard checkersBB must be already updated when looking for pinners.

template<bool FindPinned>
Bitboard Position::hidden_checkers(Color c) const {

  Bitboard result = EmptyBoardBB;
  Bitboard pinners = pieces_of_color(FindPinned ? opposite_color(c) : c);

  // Pinned pieces protect our king, dicovery checks attack
  // the enemy king.
  Square ksq = king_square(FindPinned ? c : opposite_color(c));

  // Pinners are sliders, not checkers, that give check when candidate pinned is removed
  pinners &= (pieces(ROOK, QUEEN) & RookPseudoAttacks[ksq]) | (pieces(BISHOP, QUEEN) & BishopPseudoAttacks[ksq]);

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
/// king) pieces for the given color. Note that checkersBB bitboard must
/// be already updated.

Bitboard Position::pinned_pieces(Color c) const {

  return hidden_checkers<true>(c);
}


/// Position:discovered_check_candidates() returns a bitboard containing all
/// pieces for the given side which are candidates for giving a discovered
/// check. Contrary to pinned_pieces() here there is no need of checkersBB
/// to be already updated.

Bitboard Position::discovered_check_candidates(Color c) const {

  return hidden_checkers<false>(c);
}

/// Position::attackers_to() computes a bitboard containing all pieces which
/// attacks a given square.

Bitboard Position::attackers_to(Square s) const {

  return  (attacks_from<PAWN>(s, BLACK) & pieces(PAWN, WHITE))
        | (attacks_from<PAWN>(s, WHITE) & pieces(PAWN, BLACK))
        | (attacks_from<KNIGHT>(s)      & pieces(KNIGHT))
        | (attacks_from<ROOK>(s)        & pieces(ROOK, QUEEN))
        | (attacks_from<BISHOP>(s)      & pieces(BISHOP, QUEEN))
        | (attacks_from<KING>(s)        & pieces(KING));
}

/// Position::attacks_from() computes a bitboard of all attacks
/// of a given piece put in a given square.

Bitboard Position::attacks_from(Piece p, Square s) const {

  assert(square_is_ok(s));

  switch (p)
  {
  case WP:          return attacks_from<PAWN>(s, WHITE);
  case BP:          return attacks_from<PAWN>(s, BLACK);
  case WN: case BN: return attacks_from<KNIGHT>(s);
  case WB: case BB: return attacks_from<BISHOP>(s);
  case WR: case BR: return attacks_from<ROOK>(s);
  case WQ: case BQ: return attacks_from<QUEEN>(s);
  case WK: case BK: return attacks_from<KING>(s);
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

  if (bit_is_set(attacks_from(piece_on(f), t), s))
      return true;

  // Move the piece and scan for X-ray attacks behind it
  Bitboard occ = occupied_squares();
  Color us = color_of_piece_on(f);
  clear_bit(&occ, f);
  set_bit(&occ, t);
  Bitboard xray = ( (rook_attacks_bb(s, occ) &  pieces(ROOK, QUEEN))
                   |(bishop_attacks_bb(s, occ) & pieces(BISHOP, QUEEN))) & pieces_of_color(us);

  // If we have attacks we need to verify that are caused by our move
  // and are not already existent ones.
  return xray && (xray ^ (xray & attacks_from<QUEEN>(s)));
}


/// Position::find_checkers() computes the checkersBB bitboard, which
/// contains a nonzero bit for each checking piece (0, 1 or 2). It
/// currently works by calling Position::attackers_to, which is probably
/// inefficient. Consider rewriting this function to use the last move
/// played, like in non-bitboard versions of Glaurung.

void Position::find_checkers() {

  Color us = side_to_move();
  st->checkersBB = attackers_to(king_square(us)) & pieces_of_color(opposite_color(us));
}


/// Position::pl_move_is_legal() tests whether a pseudo-legal move is legal

bool Position::pl_move_is_legal(Move m, Bitboard pinned) const {

  assert(is_ok());
  assert(move_is_ok(m));
  assert(pinned == pinned_pieces(side_to_move()));

  // Castling moves are checked for legality during move generation.
  if (move_is_castle(m))
      return true;

  Color us = side_to_move();
  Square from = move_from(m);

  assert(color_of_piece_on(from) == us);
  assert(piece_on(king_square(us)) == piece_of_color_and_type(us, KING));

  // En passant captures are a tricky special case. Because they are
  // rather uncommon, we do it simply by testing whether the king is attacked
  // after the move is made
  if (move_is_ep(m))
  {
      Color them = opposite_color(us);
      Square to = move_to(m);
      Square capsq = make_square(square_file(to), square_rank(from));
      Bitboard b = occupied_squares();
      Square ksq = king_square(us);

      assert(to == ep_square());
      assert(piece_on(from) == piece_of_color_and_type(us, PAWN));
      assert(piece_on(capsq) == piece_of_color_and_type(them, PAWN));
      assert(piece_on(to) == EMPTY);

      clear_bit(&b, from);
      clear_bit(&b, capsq);
      set_bit(&b, to);

      return   !(rook_attacks_bb(ksq, b) & pieces(ROOK, QUEEN, them))
            && !(bishop_attacks_bb(ksq, b) & pieces(BISHOP, QUEEN, them));
  }

  // If the moving piece is a king, check whether the destination
  // square is attacked by the opponent.
  if (type_of_piece_on(from) == KING)
      return !(attackers_to(move_to(m)) & pieces_of_color(opposite_color(us)));

  // A non-king move is legal if and only if it is not pinned or it
  // is moving along the ray towards or away from the king.
  return (   !pinned
          || !bit_is_set(pinned, from)
          || (direction_between_squares(from, king_square(us)) == direction_between_squares(move_to(m), king_square(us))));
}


/// Position::pl_move_is_evasion() tests whether a pseudo-legal move is a legal evasion

bool Position::pl_move_is_evasion(Move m, Bitboard pinned) const
{
  assert(is_check());

  Color us = side_to_move();
  Square from = move_from(m);
  Square to = move_to(m);

  // King moves and en-passant captures are verified in pl_move_is_legal()
  if (type_of_piece_on(from) == KING || move_is_ep(m))
      return pl_move_is_legal(m, pinned);

  Bitboard target = checkers();
  Square checksq = pop_1st_bit(&target);

  if (target) // double check ?
      return false;

  // Our move must be a blocking evasion or a capture of the checking piece
  target = squares_between(checksq, king_square(us)) | checkers();
  return bit_is_set(target, to) && pl_move_is_legal(m, pinned);
}


/// Position::move_is_check() tests whether a pseudo-legal move is a check

bool Position::move_is_check(Move m) const {

  return move_is_check(m, CheckInfo(*this));
}

bool Position::move_is_check(Move m, const CheckInfo& ci) const {

  assert(is_ok());
  assert(move_is_ok(m));
  assert(ci.dcCandidates == discovered_check_candidates(side_to_move()));
  assert(color_of_piece_on(move_from(m)) == side_to_move());
  assert(piece_on(ci.ksq) == piece_of_color_and_type(opposite_color(side_to_move()), KING));

  Square from = move_from(m);
  Square to = move_to(m);
  PieceType pt = type_of_piece_on(from);

  // Direct check ?
  if (bit_is_set(ci.checkSq[pt], to))
      return true;

  // Discovery check ?
  if (ci.dcCandidates && bit_is_set(ci.dcCandidates, from))
  {
      // For pawn and king moves we need to verify also direction
      if (  (pt != PAWN && pt != KING)
          ||(direction_between_squares(from, ci.ksq) != direction_between_squares(to, ci.ksq)))
          return true;
  }

  // Can we skip the ugly special cases ?
  if (!move_is_special(m))
      return false;

  Color us = side_to_move();
  Bitboard b = occupied_squares();

  // Promotion with check ?
  if (move_is_promotion(m))
  {
      clear_bit(&b, from);

      switch (move_promotion_piece(m))
      {
      case KNIGHT:
          return bit_is_set(attacks_from<KNIGHT>(to), ci.ksq);
      case BISHOP:
          return bit_is_set(bishop_attacks_bb(to, b), ci.ksq);
      case ROOK:
          return bit_is_set(rook_attacks_bb(to, b), ci.ksq);
      case QUEEN:
          return bit_is_set(queen_attacks_bb(to, b), ci.ksq);
      default:
          assert(false);
      }
  }

  // En passant capture with check?  We have already handled the case
  // of direct checks and ordinary discovered check, the only case we
  // need to handle is the unusual case of a discovered check through the
  // captured pawn.
  if (move_is_ep(m))
  {
      Square capsq = make_square(square_file(to), square_rank(from));
      clear_bit(&b, from);
      clear_bit(&b, capsq);
      set_bit(&b, to);
      return  (rook_attacks_bb(ci.ksq, b) & pieces(ROOK, QUEEN, us))
            ||(bishop_attacks_bb(ci.ksq, b) & pieces(BISHOP, QUEEN, us));
  }

  // Castling with check ?
  if (move_is_castle(m))
  {
      Square kfrom, kto, rfrom, rto;
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
      return bit_is_set(rook_attacks_bb(rto, b), ci.ksq);
  }

  return false;
}


/// Position::do_move() makes a move, and saves all information necessary
/// to a StateInfo object. The move is assumed to be legal.
/// Pseudo-legal moves should be filtered out before this function is called.

void Position::do_move(Move m, StateInfo& newSt) {

  CheckInfo ci(*this);
  do_move(m, newSt, ci, move_is_check(m, ci));
}

void Position::do_move(Move m, StateInfo& newSt, const CheckInfo& ci, bool moveIsCheck) {

  assert(is_ok());
  assert(move_is_ok(m));

  Bitboard key = st->key;

  // Copy some fields of old state to our new StateInfo object except the
  // ones which are recalculated from scratch anyway, then switch our state
  // pointer to point to the new, ready to be updated, state.
  struct ReducedStateInfo {
    Key pawnKey, materialKey;
    int castleRights, rule50, pliesFromNull;
    Square epSquare;
    Score value;
    Value npMaterial[2];
  };

  memcpy(&newSt, st, sizeof(ReducedStateInfo));
  newSt.previous = st;
  st = &newSt;

  // Save the current key to the history[] array, in order to be able to
  // detect repetition draws.
  history[gamePly] = key;
  gamePly++;

  // Update side to move
  key ^= zobSideToMove;

  // Increment the 50 moves rule draw counter. Resetting it to zero in the
  // case of non-reversible moves is taken care of later.
  st->rule50++;
  st->pliesFromNull++;

  if (move_is_castle(m))
  {
      st->key = key;
      do_castle_move(m);
      return;
  }

  Color us = side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Square to = move_to(m);
  bool ep = move_is_ep(m);
  bool pm = move_is_promotion(m);

  Piece piece = piece_on(from);
  PieceType pt = type_of_piece(piece);
  PieceType capture = ep ? PAWN : type_of_piece_on(to);

  assert(color_of_piece_on(from) == us);
  assert(color_of_piece_on(to) == them || square_is_empty(to));
  assert(!(ep || pm) || piece == piece_of_color_and_type(us, PAWN));
  assert(!pm || relative_rank(us, to) == RANK_8);

  if (capture)
      do_capture_move(key, capture, them, to, ep);

  // Update hash key
  key ^= zobrist[us][pt][from] ^ zobrist[us][pt][to];

  // Reset en passant square
  if (st->epSquare != SQ_NONE)
  {
      key ^= zobEp[st->epSquare];
      st->epSquare = SQ_NONE;
  }

  // Update castle rights, try to shortcut a common case
  int cm = castleRightsMask[from] & castleRightsMask[to];
  if (cm != ALL_CASTLES && ((cm & st->castleRights) != st->castleRights))
  {
      key ^= zobCastle[st->castleRights];
      st->castleRights &= castleRightsMask[from];
      st->castleRights &= castleRightsMask[to];
      key ^= zobCastle[st->castleRights];
  }

  // Prefetch TT access as soon as we know key is updated
  TT.prefetch(key);

  // Move the piece
  Bitboard move_bb = make_move_bb(from, to);
  do_move_bb(&(byColorBB[us]), move_bb);
  do_move_bb(&(byTypeBB[pt]), move_bb);
  do_move_bb(&(byTypeBB[0]), move_bb); // HACK: byTypeBB[0] == occupied squares

  board[to] = board[from];
  board[from] = EMPTY;

  // Update piece lists, note that index[from] is not updated and
  // becomes stale. This works as long as index[] is accessed just
  // by known occupied squares.
  index[to] = index[from];
  pieceList[us][pt][index[to]] = to;

  // If the moving piece was a pawn do some special extra work
  if (pt == PAWN)
  {
      // Reset rule 50 draw counter
      st->rule50 = 0;

      // Update pawn hash key
      st->pawnKey ^= zobrist[us][PAWN][from] ^ zobrist[us][PAWN][to];

      // Set en passant square, only if moved pawn can be captured
      if ((to ^ from) == 16)
      {
          if (attacks_from<PAWN>(from + (us == WHITE ? DELTA_N : DELTA_S), us) & pieces(PAWN, them))
          {
              st->epSquare = Square((int(from) + int(to)) / 2);
              key ^= zobEp[st->epSquare];
          }
      }
  }

  // Update incremental scores
  st->value += pst_delta(piece, from, to);

  // Set capture piece
  st->capture = capture;

  if (pm) // promotion ?
  {
      PieceType promotion = move_promotion_piece(m);

      assert(promotion >= KNIGHT && promotion <= QUEEN);

      // Insert promoted piece instead of pawn
      clear_bit(&(byTypeBB[PAWN]), to);
      set_bit(&(byTypeBB[promotion]), to);
      board[to] = piece_of_color_and_type(us, promotion);

      // Update material key
      st->materialKey ^= zobMaterial[us][PAWN][pieceCount[us][PAWN]];
      st->materialKey ^= zobMaterial[us][promotion][pieceCount[us][promotion]+1];

      // Update piece counts
      pieceCount[us][PAWN]--;
      pieceCount[us][promotion]++;

      // Update piece lists, move the last pawn at index[to] position
      // and shrink the list. Add a new promotion piece to the list.
      Square lastPawnSquare = pieceList[us][PAWN][pieceCount[us][PAWN]];
      index[lastPawnSquare] = index[to];
      pieceList[us][PAWN][index[lastPawnSquare]] = lastPawnSquare;
      pieceList[us][PAWN][pieceCount[us][PAWN]] = SQ_NONE;
      index[to] = pieceCount[us][promotion] - 1;
      pieceList[us][promotion][index[to]] = to;

      // Partially revert hash keys update
      key ^= zobrist[us][PAWN][to] ^ zobrist[us][promotion][to];
      st->pawnKey ^= zobrist[us][PAWN][to];

      // Partially revert and update incremental scores
      st->value -= pst(us, PAWN, to);
      st->value += pst(us, promotion, to);

      // Update material
      st->npMaterial[us] += piece_value_midgame(promotion);
  }

  // Update the key with the final value
  st->key = key;

  // Update checkers bitboard, piece must be already moved
  st->checkersBB = EmptyBoardBB;

  if (moveIsCheck)
  {
      if (ep | pm)
          st->checkersBB = attackers_to(king_square(them)) & pieces_of_color(us);
      else
      {
          // Direct checks
          if (bit_is_set(ci.checkSq[pt], to))
              st->checkersBB = SetMaskBB[to];

          // Discovery checks
          if (ci.dcCandidates && bit_is_set(ci.dcCandidates, from))
          {
              if (pt != ROOK)
                  st->checkersBB |= (attacks_from<ROOK>(ci.ksq) & pieces(ROOK, QUEEN, us));

              if (pt != BISHOP)
                  st->checkersBB |= (attacks_from<BISHOP>(ci.ksq) & pieces(BISHOP, QUEEN, us));
          }
      }
  }

  // Finish
  sideToMove = opposite_color(sideToMove);
  st->value += (sideToMove == WHITE ?  TempoValue : -TempoValue);

  assert(is_ok());
}


/// Position::do_capture_move() is a private method used to update captured
/// piece info. It is called from the main Position::do_move function.

void Position::do_capture_move(Bitboard& key, PieceType capture, Color them, Square to, bool ep) {

    assert(capture != KING);

    Square capsq = to;

    if (ep) // en passant ?
    {
        capsq = (them == BLACK)? (to - DELTA_N) : (to - DELTA_S);

        assert(to == st->epSquare);
        assert(relative_rank(opposite_color(them), to) == RANK_6);
        assert(piece_on(to) == EMPTY);
        assert(piece_on(capsq) == piece_of_color_and_type(them, PAWN));

        board[capsq] = EMPTY;
    }

    // Remove captured piece
    clear_bit(&(byColorBB[them]), capsq);
    clear_bit(&(byTypeBB[capture]), capsq);
    clear_bit(&(byTypeBB[0]), capsq);

    // Update hash key
    key ^= zobrist[them][capture][capsq];

    // Update incremental scores
    st->value -= pst(them, capture, capsq);

    // If the captured piece was a pawn, update pawn hash key,
    // otherwise update non-pawn material.
    if (capture == PAWN)
        st->pawnKey ^= zobrist[them][PAWN][capsq];
    else
        st->npMaterial[them] -= piece_value_midgame(capture);

    // Update material hash key
    st->materialKey ^= zobMaterial[them][capture][pieceCount[them][capture]];

    // Update piece count
    pieceCount[them][capture]--;

    // Update piece list, move the last piece at index[capsq] position
    //
    // WARNING: This is a not perfectly revresible operation. When we
    // will reinsert the captured piece in undo_move() we will put it
    // at the end of the list and not in its original place, it means
    // index[] and pieceList[] are not guaranteed to be invariant to a
    // do_move() + undo_move() sequence.
    Square lastPieceSquare = pieceList[them][capture][pieceCount[them][capture]];
    index[lastPieceSquare] = index[capsq];
    pieceList[them][capture][index[lastPieceSquare]] = lastPieceSquare;
    pieceList[them][capture][pieceCount[them][capture]] = SQ_NONE;

    // Reset rule 50 counter
    st->rule50 = 0;
}


/// Position::do_castle_move() is a private method used to make a castling
/// move. It is called from the main Position::do_move function. Note that
/// castling moves are encoded as "king captures friendly rook" moves, for
/// instance white short castling in a non-Chess960 game is encoded as e1h1.

void Position::do_castle_move(Move m) {

  assert(move_is_ok(m));
  assert(move_is_castle(m));

  Color us = side_to_move();
  Color them = opposite_color(us);

  // Reset capture field
  st->capture = NO_PIECE_TYPE;

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

  // Remove pieces from source squares:
  clear_bit(&(byColorBB[us]), kfrom);
  clear_bit(&(byTypeBB[KING]), kfrom);
  clear_bit(&(byTypeBB[0]), kfrom); // HACK: byTypeBB[0] == occupied squares
  clear_bit(&(byColorBB[us]), rfrom);
  clear_bit(&(byTypeBB[ROOK]), rfrom);
  clear_bit(&(byTypeBB[0]), rfrom); // HACK: byTypeBB[0] == occupied squares

  // Put pieces on destination squares:
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

  // Update piece lists
  pieceList[us][KING][index[kfrom]] = kto;
  pieceList[us][ROOK][index[rfrom]] = rto;
  int tmp = index[rfrom]; // In Chess960 could be rto == kfrom
  index[kto] = index[kfrom];
  index[rto] = tmp;

  // Update incremental scores
  st->value += pst_delta(king, kfrom, kto);
  st->value += pst_delta(rook, rfrom, rto);

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
  st->checkersBB = attackers_to(king_square(them)) & pieces_of_color(us);

  // Finish
  sideToMove = opposite_color(sideToMove);
  st->value += (sideToMove == WHITE ?  TempoValue : -TempoValue);

  assert(is_ok());
}


/// Position::undo_move() unmakes a move. When it returns, the position should
/// be restored to exactly the same state as before the move was made.

void Position::undo_move(Move m) {

  assert(is_ok());
  assert(move_is_ok(m));

  gamePly--;
  sideToMove = opposite_color(sideToMove);

  if (move_is_castle(m))
  {
      undo_castle_move(m);
      return;
  }

  Color us = side_to_move();
  Color them = opposite_color(us);
  Square from = move_from(m);
  Square to = move_to(m);
  bool ep = move_is_ep(m);
  bool pm = move_is_promotion(m);

  PieceType pt = type_of_piece_on(to);

  assert(square_is_empty(from));
  assert(color_of_piece_on(to) == us);
  assert(!pm || relative_rank(us, to) == RANK_8);
  assert(!ep || to == st->previous->epSquare);
  assert(!ep || relative_rank(us, to) == RANK_6);
  assert(!ep || piece_on(to) == piece_of_color_and_type(us, PAWN));

  if (pm) // promotion ?
  {
      PieceType promotion = move_promotion_piece(m);
      pt = PAWN;

      assert(promotion >= KNIGHT && promotion <= QUEEN);
      assert(piece_on(to) == piece_of_color_and_type(us, promotion));

      // Replace promoted piece with a pawn
      clear_bit(&(byTypeBB[promotion]), to);
      set_bit(&(byTypeBB[PAWN]), to);

      // Update piece counts
      pieceCount[us][promotion]--;
      pieceCount[us][PAWN]++;

      // Update piece list replacing promotion piece with a pawn
      Square lastPromotionSquare = pieceList[us][promotion][pieceCount[us][promotion]];
      index[lastPromotionSquare] = index[to];
      pieceList[us][promotion][index[lastPromotionSquare]] = lastPromotionSquare;
      pieceList[us][promotion][pieceCount[us][promotion]] = SQ_NONE;
      index[to] = pieceCount[us][PAWN] - 1;
      pieceList[us][PAWN][index[to]] = to;
  }


  // Put the piece back at the source square
  Bitboard move_bb = make_move_bb(to, from);
  do_move_bb(&(byColorBB[us]), move_bb);
  do_move_bb(&(byTypeBB[pt]), move_bb);
  do_move_bb(&(byTypeBB[0]), move_bb); // HACK: byTypeBB[0] == occupied squares

  board[from] = piece_of_color_and_type(us, pt);
  board[to] = EMPTY;

  // Update piece list
  index[from] = index[to];
  pieceList[us][pt][index[from]] = from;

  if (st->capture)
  {
      Square capsq = to;

      if (ep)
          capsq = (us == WHITE)? (to - DELTA_N) : (to - DELTA_S);

      assert(st->capture != KING);
      assert(!ep || square_is_empty(capsq));

      // Restore the captured piece
      set_bit(&(byColorBB[them]), capsq);
      set_bit(&(byTypeBB[st->capture]), capsq);
      set_bit(&(byTypeBB[0]), capsq);

      board[capsq] = piece_of_color_and_type(them, st->capture);

      // Update piece count
      pieceCount[them][st->capture]++;

      // Update piece list, add a new captured piece in capsq square
      index[capsq] = pieceCount[them][st->capture] - 1;
      pieceList[them][st->capture][index[capsq]] = capsq;
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

  // Remove pieces from destination squares:
  clear_bit(&(byColorBB[us]), kto);
  clear_bit(&(byTypeBB[KING]), kto);
  clear_bit(&(byTypeBB[0]), kto); // HACK: byTypeBB[0] == occupied squares
  clear_bit(&(byColorBB[us]), rto);
  clear_bit(&(byTypeBB[ROOK]), rto);
  clear_bit(&(byTypeBB[0]), rto); // HACK: byTypeBB[0] == occupied squares

  // Put pieces on source squares:
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

  // Update piece lists
  pieceList[us][KING][index[kto]] = kfrom;
  pieceList[us][ROOK][index[rto]] = rfrom;
  int tmp = index[rto];  // In Chess960 could be rto == kfrom
  index[kfrom] = index[kto];
  index[rfrom] = tmp;

  // Finally point our state pointer back to the previous state
  st = st->previous;

  assert(is_ok());
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
  backupSt.key      = st->key;
  backupSt.epSquare = st->epSquare;
  backupSt.value    = st->value;
  backupSt.previous = st->previous;
  backupSt.pliesFromNull = st->pliesFromNull;
  st->previous = &backupSt;

  // Save the current key to the history[] array, in order to be able to
  // detect repetition draws.
  history[gamePly] = st->key;

  // Update the necessary information
  if (st->epSquare != SQ_NONE)
      st->key ^= zobEp[st->epSquare];

  st->key ^= zobSideToMove;
  TT.prefetch(st->key);

  sideToMove = opposite_color(sideToMove);
  st->epSquare = SQ_NONE;
  st->rule50++;
  st->pliesFromNull = 0;
  st->value += (sideToMove == WHITE) ?  TempoValue : -TempoValue;
  gamePly++;
}


/// Position::undo_null_move() unmakes a "null move".

void Position::undo_null_move() {

  assert(is_ok());
  assert(!is_check());

  // Restore information from the our backup StateInfo object
  StateInfo* backupSt = st->previous;
  st->key      = backupSt->key;
  st->epSquare = backupSt->epSquare;
  st->value    = backupSt->value;
  st->previous = backupSt->previous;
  st->pliesFromNull = backupSt->pliesFromNull;

  // Update the necessary information
  sideToMove = opposite_color(sideToMove);
  st->rule50--;
  gamePly--;
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

  Bitboard attackers, stmAttackers, b;

  assert(square_is_ok(from) || from == SQ_NONE);
  assert(square_is_ok(to));

  // Initialize colors
  Color us = (from != SQ_NONE ? color_of_piece_on(from) : opposite_color(color_of_piece_on(to)));
  Color them = opposite_color(us);

  // Initialize pieces
  Piece piece = piece_on(from);
  Piece capture = piece_on(to);
  Bitboard occ = occupied_squares();

  // King cannot be recaptured
  if (type_of_piece(piece) == KING)
      return seeValues[capture];

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
      // Find all attackers to the destination square, with the moving piece
      // removed, but possibly an X-ray attacker added behind it.
      clear_bit(&occ, from);
      attackers =  (rook_attacks_bb(to, occ)      & pieces(ROOK, QUEEN))
                 | (bishop_attacks_bb(to, occ)    & pieces(BISHOP, QUEEN))
                 | (attacks_from<KNIGHT>(to)      & pieces(KNIGHT))
                 | (attacks_from<KING>(to)        & pieces(KING))
                 | (attacks_from<PAWN>(to, WHITE) & pieces(PAWN, BLACK))
                 | (attacks_from<PAWN>(to, BLACK) & pieces(PAWN, WHITE));

      if (from != SQ_NONE)
          break;

      // If we don't have any attacker we are finished
      if ((attackers & pieces_of_color(us)) == EmptyBoardBB)
          return 0;

      // Locate the least valuable attacker to the destination square
      // and use it to initialize from square.
      stmAttackers = attackers & pieces_of_color(us);
      PieceType pt;
      for (pt = PAWN; !(stmAttackers & pieces(pt)); pt++)
          assert(pt < KING);

      from = first_1(stmAttackers & pieces(pt));
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
      for (pt = PAWN; !(stmAttackers & pieces(pt)); pt++)
          assert(pt < KING);

      // Remove the attacker we just found from the 'attackers' bitboard,
      // and scan for new X-ray attacks behind the attacker.
      b = stmAttackers & pieces(pt);
      occ ^= (b & (~b + 1));
      attackers |=  (rook_attacks_bb(to, occ) &  pieces(ROOK, QUEEN))
                  | (bishop_attacks_bb(to, occ) & pieces(BISHOP, QUEEN));

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


/// Position::clear() erases the position object to a pristine state, with an
/// empty board, white to move, and no castling rights.

void Position::clear() {

  st = &startState;
  memset(st, 0, sizeof(StateInfo));
  st->epSquare = SQ_NONE;

  memset(byColorBB,  0, sizeof(Bitboard) * 2);
  memset(byTypeBB,   0, sizeof(Bitboard) * 8);
  memset(pieceCount, 0, sizeof(int) * 2 * 8);
  memset(index,      0, sizeof(int) * 64);

  for (int i = 0; i < 64; i++)
      board[i] = EMPTY;

  for (int i = 0; i < 8; i++)
      for (int j = 0; j < 16; j++)
          pieceList[0][i][j] = pieceList[1][i][j] = SQ_NONE;

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
      b = pieces(PAWN, c);
      while (b)
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
Score Position::compute_value() const {

  Score result = make_score(0, 0);
  Bitboard b;
  Square s;

  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= KING; pt++)
      {
          b = pieces(pt, c);
          while (b)
          {
              s = pop_1st_bit(&b);
              assert(piece_on(s) == piece_of_color_and_type(c, pt));
              result += pst(c, pt, s);
          }
      }

  result += (side_to_move() == WHITE ? TempoValue / 2 : -TempoValue / 2);
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
      Bitboard b = pieces(pt, c);
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
// FIXME: Currently we are not handling 50 move rule correctly when in check

bool Position::is_draw() const {

  // Draw by material?
  if (   !pieces(PAWN)
      && (non_pawn_material(WHITE) + non_pawn_material(BLACK) <= BishopValueMidgame))
      return true;

  // Draw by the 50 moves rule?
  if (st->rule50 > 100 || (st->rule50 == 100 && !is_check()))
      return true;

  // Draw by repetition?
  for (int i = 4; i <= Min(Min(gamePly, st->rule50), st->pliesFromNull); i += 2)
      if (history[gamePly - i] == st->key)
          return true;

  return false;
}


/// Position::is_mate() returns true or false depending on whether the
/// side to move is checkmated.

bool Position::is_mate() const {

  MoveStack moves[256];
  return is_check() && (generate_moves(*this, moves, false) == moves);
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
  bool result = false;
  Bitboard pinned = pinned_pieces(sideToMove);

  // Generate pseudo-legal non-capture and capture check moves
  MoveStack* last = generate_non_capture_checks(*this, mlist);
  last = generate_captures(*this, last);

  // Loop through the moves, and see if one of them is mate
  for (MoveStack* cur = mlist; cur != last; cur++)
  {
      Move move = cur->move;
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

  zobExclusion = genrand_int64();
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
          PieceSquareTable[p][s] = make_score(MgPST[p][s] + i, EgPST[p][s] + i);
      }

  for (Square s = SQ_A1; s <= SQ_H8; s++)
      for (Piece p = BP; p <= BK; p++)
          PieceSquareTable[p][s] = -PieceSquareTable[p-8][flip_square(s)];
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
  st->value = compute_value();

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
  static const bool debugCastleSquares = false;

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
      if (attackers_to(ksq) & pieces_of_color(us))
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
              if (p1 != p2 && (pieces(p1) & pieces(p2)))
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
  if (debugIncrementalEval && st->value != compute_value())
      return false;

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
              if (pieceCount[c][pt] != count_1s(pieces(pt, c)))
                  return false;

  if (failedStep) (*failedStep)++;
  if (debugPieceList)
  {
      for (Color c = WHITE; c <= BLACK; c++)
          for (PieceType pt = PAWN; pt <= KING; pt++)
              for (int i = 0; i < pieceCount[c][pt]; i++)
              {
                  if (piece_on(piece_list(c, pt, i)) != piece_of_color_and_type(c, pt))
                      return false;

                  if (index[piece_list(c, pt, i)] != i)
                      return false;
              }
  }

  if (failedStep) (*failedStep)++;
  if (debugCastleSquares) {
      for (Color c = WHITE; c <= BLACK; c++) {
          if (can_castle_kingside(c) && piece_on(initial_kr_square(c)) != piece_of_color_and_type(c, ROOK))
              return false;
          if (can_castle_queenside(c) && piece_on(initial_qr_square(c)) != piece_of_color_and_type(c, ROOK))
              return false;
      }
      if (castleRightsMask[initial_kr_square(WHITE)] != (ALL_CASTLES ^ WHITE_OO))
          return false;
      if (castleRightsMask[initial_qr_square(WHITE)] != (ALL_CASTLES ^ WHITE_OOO))
          return false;
      if (castleRightsMask[initial_kr_square(BLACK)] != (ALL_CASTLES ^ BLACK_OO))
          return false;
      if (castleRightsMask[initial_qr_square(BLACK)] != (ALL_CASTLES ^ BLACK_OOO))
          return false;
  }

  if (failedStep) *failedStep = 0;
  return true;
}
