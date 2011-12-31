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

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

#include "bitcount.h"
#include "movegen.h"
#include "position.h"
#include "psqtab.h"
#include "rkiss.h"
#include "thread.h"
#include "tt.h"

using std::string;
using std::cout;
using std::endl;

Key Position::zobrist[2][8][64];
Key Position::zobEp[64];
Key Position::zobCastle[16];
Key Position::zobSideToMove;
Key Position::zobExclusion;

Score Position::pieceSquareTable[16][64];

// Material values arrays, indexed by Piece
const Value PieceValueMidgame[17] = {
  VALUE_ZERO,
  PawnValueMidgame, KnightValueMidgame, BishopValueMidgame,
  RookValueMidgame, QueenValueMidgame,
  VALUE_ZERO, VALUE_ZERO, VALUE_ZERO,
  PawnValueMidgame, KnightValueMidgame, BishopValueMidgame,
  RookValueMidgame, QueenValueMidgame
};

const Value PieceValueEndgame[17] = {
  VALUE_ZERO,
  PawnValueEndgame, KnightValueEndgame, BishopValueEndgame,
  RookValueEndgame, QueenValueEndgame,
  VALUE_ZERO, VALUE_ZERO, VALUE_ZERO,
  PawnValueEndgame, KnightValueEndgame, BishopValueEndgame,
  RookValueEndgame, QueenValueEndgame
};


namespace {

  // Bonus for having the side to move (modified by Joona Kiiski)
  const Score TempoValue = make_score(48, 22);

  // To convert a Piece to and from a FEN char
  const string PieceToChar(" PNBRQK  pnbrqk  .");
}


/// CheckInfo c'tor

CheckInfo::CheckInfo(const Position& pos) {

  Color them = flip(pos.side_to_move());
  Square ksq = pos.king_square(them);

  pinned = pos.pinned_pieces();
  dcCandidates = pos.discovered_check_candidates();

  checkSq[PAWN]   = pos.attacks_from<PAWN>(ksq, them);
  checkSq[KNIGHT] = pos.attacks_from<KNIGHT>(ksq);
  checkSq[BISHOP] = pos.attacks_from<BISHOP>(ksq);
  checkSq[ROOK]   = pos.attacks_from<ROOK>(ksq);
  checkSq[QUEEN]  = checkSq[BISHOP] | checkSq[ROOK];
  checkSq[KING]   = 0;
}


/// Position c'tors. Here we always create a copy of the original position
/// or the FEN string, we want the new born Position object do not depend
/// on any external data so we detach state pointer from the source one.

void Position::copy(const Position& pos, int th) {

  memcpy(this, &pos, sizeof(Position));
  startState = *st;
  st = &startState;
  threadID = th;
  nodes = 0;

  assert(pos_is_ok());
}

Position::Position(const string& fen, bool isChess960, int th) {

  from_fen(fen, isChess960);
  threadID = th;
}


/// Position::from_fen() initializes the position object with the given FEN
/// string. This function is not very robust - make sure that input FENs are
/// correct (this is assumed to be the responsibility of the GUI).

void Position::from_fen(const string& fenStr, bool isChess960) {
/*
   A FEN string defines a particular position using only the ASCII character set.

   A FEN string contains six fields separated by a space. The fields are:

   1) Piece placement (from white's perspective). Each rank is described, starting
      with rank 8 and ending with rank 1; within each rank, the contents of each
      square are described from file A through file H. Following the Standard
      Algebraic Notation (SAN), each piece is identified by a single letter taken
      from the standard English names. White pieces are designated using upper-case
      letters ("PNBRQK") while Black take lowercase ("pnbrqk"). Blank squares are
      noted using digits 1 through 8 (the number of blank squares), and "/"
      separates ranks.

   2) Active color. "w" means white moves next, "b" means black.

   3) Castling availability. If neither side can castle, this is "-". Otherwise,
      this has one or more letters: "K" (White can castle kingside), "Q" (White
      can castle queenside), "k" (Black can castle kingside), and/or "q" (Black
      can castle queenside).

   4) En passant target square (in algebraic notation). If there's no en passant
      target square, this is "-". If a pawn has just made a 2-square move, this
      is the position "behind" the pawn. This is recorded regardless of whether
      there is a pawn in position to make an en passant capture.

   5) Halfmove clock. This is the number of halfmoves since the last pawn advance
      or capture. This is used to determine if a draw can be claimed under the
      fifty-move rule.

   6) Fullmove number. The number of the full move. It starts at 1, and is
      incremented after Black's move.
*/

  char col, row, token;
  size_t p;
  Square sq = SQ_A8;
  std::istringstream fen(fenStr);

  clear();
  fen >> std::noskipws;

  // 1. Piece placement
  while ((fen >> token) && !isspace(token))
  {
      if (token == '/')
          sq -= Square(16); // Jump back of 2 rows

      else if (isdigit(token))
          sq += Square(token - '0'); // Skip the given number of files

      else if ((p = PieceToChar.find(token)) != string::npos)
      {
          put_piece(Piece(p), sq);
          sq++;
      }
  }

  // 2. Active color
  fen >> token;
  sideToMove = (token == 'w' ? WHITE : BLACK);
  fen >> token;

  // 3. Castling availability. Compatible with 3 standards: Normal FEN standard,
  // Shredder-FEN that uses the letters of the columns on which the rooks began
  // the game instead of KQkq and also X-FEN standard that, in case of Chess960,
  // if an inner rook is associated with the castling right, the castling tag is
  // replaced by the file letter of the involved rook, as for the Shredder-FEN.
  while ((fen >> token) && !isspace(token))
  {
      Square rsq;
      Color c = islower(token) ? BLACK : WHITE;
      Piece rook = make_piece(c, ROOK);

      token = char(toupper(token));

      if (token == 'K')
          for (rsq = relative_square(c, SQ_H1); piece_on(rsq) != rook; rsq--) {}

      else if (token == 'Q')
          for (rsq = relative_square(c, SQ_A1); piece_on(rsq) != rook; rsq++) {}

      else if (token >= 'A' && token <= 'H')
          rsq = make_square(File(token - 'A'), relative_rank(c, RANK_1));

      else
          continue;

      set_castle_right(king_square(c), rsq);
  }

  // 4. En passant square. Ignore if no pawn capture is possible
  if (   ((fen >> col) && (col >= 'a' && col <= 'h'))
      && ((fen >> row) && (row == '3' || row == '6')))
  {
      st->epSquare = make_square(File(col - 'a'), Rank(row - '1'));

      if (!(attackers_to(st->epSquare) & pieces(PAWN, sideToMove)))
          st->epSquare = SQ_NONE;
  }

  // 5-6. Halfmove clock and fullmove number
  fen >> std::skipws >> st->rule50 >> startPosPly;

  // Convert from fullmove starting from 1 to ply starting from 0,
  // handle also common incorrect FEN with fullmove = 0.
  startPosPly = std::max(2 * (startPosPly - 1), 0) + int(sideToMove == BLACK);

  st->key = compute_key();
  st->pawnKey = compute_pawn_key();
  st->materialKey = compute_material_key();
  st->value = compute_value();
  st->npMaterial[WHITE] = compute_non_pawn_material(WHITE);
  st->npMaterial[BLACK] = compute_non_pawn_material(BLACK);
  st->checkersBB = attackers_to(king_square(sideToMove)) & pieces(flip(sideToMove));
  chess960 = isChess960;

  assert(pos_is_ok());
}


/// Position::set_castle_right() is an helper function used to set castling
/// rights given the corresponding king and rook starting squares.

void Position::set_castle_right(Square ksq, Square rsq) {

  int f = (rsq < ksq ? WHITE_OOO : WHITE_OO) << color_of(piece_on(ksq));

  st->castleRights |= f;
  castleRightsMask[ksq] ^= f;
  castleRightsMask[rsq] ^= f;
  castleRookSquare[f] = rsq;
}


/// Position::to_fen() returns a FEN representation of the position. In case
/// of Chess960 the Shredder-FEN notation is used. Mainly a debugging function.

const string Position::to_fen() const {

  std::ostringstream fen;
  Square sq;
  int emptyCnt;

  for (Rank rank = RANK_8; rank >= RANK_1; rank--)
  {
      emptyCnt = 0;

      for (File file = FILE_A; file <= FILE_H; file++)
      {
          sq = make_square(file, rank);

          if (square_is_empty(sq))
              emptyCnt++;
          else
          {
              if (emptyCnt > 0)
              {
                  fen << emptyCnt;
                  emptyCnt = 0;
              }
              fen << PieceToChar[piece_on(sq)];
          }
      }

      if (emptyCnt > 0)
          fen << emptyCnt;

      if (rank > RANK_1)
          fen << '/';
  }

  fen << (sideToMove == WHITE ? " w " : " b ");

  if (can_castle(WHITE_OO))
      fen << (chess960 ? char(toupper(file_to_char(file_of(castle_rook_square(WHITE_OO))))) : 'K');

  if (can_castle(WHITE_OOO))
      fen << (chess960 ? char(toupper(file_to_char(file_of(castle_rook_square(WHITE_OOO))))) : 'Q');

  if (can_castle(BLACK_OO))
      fen << (chess960 ? file_to_char(file_of(castle_rook_square(BLACK_OO))) : 'k');

  if (can_castle(BLACK_OOO))
      fen << (chess960 ? file_to_char(file_of(castle_rook_square(BLACK_OOO))) : 'q');

  if (st->castleRights == CASTLES_NONE)
      fen << '-';

  fen << (ep_square() == SQ_NONE ? " - " : " " + square_to_string(ep_square()) + " ")
      << st->rule50 << " " << 1 + (startPosPly - int(sideToMove == BLACK)) / 2;

  return fen.str();
}


/// Position::print() prints an ASCII representation of the position to
/// the standard output. If a move is given then also the san is printed.

void Position::print(Move move) const {

  const char* dottedLine = "\n+---+---+---+---+---+---+---+---+\n";

  if (move)
  {
      Position p(*this, thread());
      cout << "\nMove is: " << (sideToMove == BLACK ? ".." : "") << move_to_san(p, move);
  }

  for (Rank rank = RANK_8; rank >= RANK_1; rank--)
  {
      cout << dottedLine << '|';
      for (File file = FILE_A; file <= FILE_H; file++)
      {
          Square sq = make_square(file, rank);
          Piece piece = piece_on(sq);
          char c = (color_of(piece) == BLACK ? '=' : ' ');

          if (piece == NO_PIECE && !opposite_colors(sq, SQ_A1))
              piece++; // Index the dot

          cout << c << PieceToChar[piece] << c << '|';
      }
  }
  cout << dottedLine << "Fen is: " << to_fen() << "\nKey is: " << st->key << endl;
}


/// Position:hidden_checkers<>() returns a bitboard of all pinned (against the
/// king) pieces for the given color. Or, when template parameter FindPinned is
/// false, the function return the pieces of the given color candidate for a
/// discovery check against the enemy king.
template<bool FindPinned>
Bitboard Position::hidden_checkers() const {

  // Pinned pieces protect our king, dicovery checks attack the enemy king
  Bitboard b, result = 0;
  Bitboard pinners = pieces(FindPinned ? flip(sideToMove) : sideToMove);
  Square ksq = king_square(FindPinned ? sideToMove : flip(sideToMove));

  // Pinners are sliders, that give check when candidate pinned is removed
  pinners &=  (pieces(ROOK, QUEEN) & RookPseudoAttacks[ksq])
            | (pieces(BISHOP, QUEEN) & BishopPseudoAttacks[ksq]);

  while (pinners)
  {
      b = squares_between(ksq, pop_1st_bit(&pinners)) & occupied_squares();

      // Only one bit set and is an our piece?
      if (b && !(b & (b - 1)) && (b & pieces(sideToMove)))
          result |= b;
  }
  return result;
}

// Explicit template instantiations
template Bitboard Position::hidden_checkers<true>() const;
template Bitboard Position::hidden_checkers<false>() const;


/// Position::attackers_to() computes a bitboard of all pieces which attack a
/// given square. Slider attacks use occ bitboard as occupancy.

Bitboard Position::attackers_to(Square s, Bitboard occ) const {

  return  (attacks_from<PAWN>(s, BLACK) & pieces(PAWN, WHITE))
        | (attacks_from<PAWN>(s, WHITE) & pieces(PAWN, BLACK))
        | (attacks_from<KNIGHT>(s)      & pieces(KNIGHT))
        | (rook_attacks_bb(s, occ)      & pieces(ROOK, QUEEN))
        | (bishop_attacks_bb(s, occ)    & pieces(BISHOP, QUEEN))
        | (attacks_from<KING>(s)        & pieces(KING));
}


/// Position::attacks_from() computes a bitboard of all attacks of a given piece
/// put in a given square. Slider attacks use occ bitboard as occupancy.

Bitboard Position::attacks_from(Piece p, Square s, Bitboard occ) {

  assert(square_is_ok(s));

  switch (type_of(p))
  {
  case BISHOP: return bishop_attacks_bb(s, occ);
  case ROOK  : return rook_attacks_bb(s, occ);
  case QUEEN : return bishop_attacks_bb(s, occ) | rook_attacks_bb(s, occ);
  default    : return StepAttacksBB[p][s];
  }
}


/// Position::move_attacks_square() tests whether a move from the current
/// position attacks a given square.

bool Position::move_attacks_square(Move m, Square s) const {

  assert(is_ok(m));
  assert(square_is_ok(s));

  Bitboard occ, xray;
  Square from = move_from(m);
  Square to = move_to(m);
  Piece piece = piece_on(from);

  assert(!square_is_empty(from));

  // Update occupancy as if the piece is moving
  occ = occupied_squares();
  do_move_bb(&occ, make_move_bb(from, to));

  // The piece moved in 'to' attacks the square 's' ?
  if (bit_is_set(attacks_from(piece, to, occ), s))
      return true;

  // Scan for possible X-ray attackers behind the moved piece
  xray = (rook_attacks_bb(s, occ)   & pieces(ROOK, QUEEN, color_of(piece)))
        |(bishop_attacks_bb(s, occ) & pieces(BISHOP, QUEEN, color_of(piece)));

  // Verify attackers are triggered by our move and not already existing
  return xray && (xray ^ (xray & attacks_from<QUEEN>(s)));
}


/// Position::pl_move_is_legal() tests whether a pseudo-legal move is legal

bool Position::pl_move_is_legal(Move m, Bitboard pinned) const {

  assert(is_ok(m));
  assert(pinned == pinned_pieces());

  Color us = side_to_move();
  Square from = move_from(m);

  assert(color_of(piece_on(from)) == us);
  assert(piece_on(king_square(us)) == make_piece(us, KING));

  // En passant captures are a tricky special case. Because they are rather
  // uncommon, we do it simply by testing whether the king is attacked after
  // the move is made.
  if (is_enpassant(m))
  {
      Color them = flip(us);
      Square to = move_to(m);
      Square capsq = to + pawn_push(them);
      Square ksq = king_square(us);
      Bitboard b = occupied_squares();

      assert(to == ep_square());
      assert(piece_on(from) == make_piece(us, PAWN));
      assert(piece_on(capsq) == make_piece(them, PAWN));
      assert(piece_on(to) == NO_PIECE);

      clear_bit(&b, from);
      clear_bit(&b, capsq);
      set_bit(&b, to);

      return   !(rook_attacks_bb(ksq, b) & pieces(ROOK, QUEEN, them))
            && !(bishop_attacks_bb(ksq, b) & pieces(BISHOP, QUEEN, them));
  }

  // If the moving piece is a king, check whether the destination
  // square is attacked by the opponent. Castling moves are checked
  // for legality during move generation.
  if (type_of(piece_on(from)) == KING)
      return is_castle(m) || !(attackers_to(move_to(m)) & pieces(flip(us)));

  // A non-king move is legal if and only if it is not pinned or it
  // is moving along the ray towards or away from the king.
  return   !pinned
        || !bit_is_set(pinned, from)
        ||  squares_aligned(from, move_to(m), king_square(us));
}


/// Position::move_is_legal() takes a random move and tests whether the move
/// is legal. This version is not very fast and should be used only in non
/// time-critical paths.

bool Position::move_is_legal(const Move m) const {

  for (MoveList<MV_LEGAL> ml(*this); !ml.end(); ++ml)
      if (ml.move() == m)
          return true;

  return false;
}


/// Position::is_pseudo_legal() takes a random move and tests whether the move
/// is pseudo legal. It is used to validate moves from TT that can be corrupted
/// due to SMP concurrent access or hash position key aliasing.

bool Position::is_pseudo_legal(const Move m) const {

  Color us = sideToMove;
  Color them = flip(sideToMove);
  Square from = move_from(m);
  Square to = move_to(m);
  Piece pc = piece_on(from);

  // Use a slower but simpler function for uncommon cases
  if (is_special(m))
      return move_is_legal(m);

  // Is not a promotion, so promotion piece must be empty
  if (promotion_piece_type(m) - 2 != NO_PIECE_TYPE)
      return false;

  // If the from square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if (pc == NO_PIECE || color_of(pc) != us)
      return false;

  // The destination square cannot be occupied by a friendly piece
  if (color_of(piece_on(to)) == us)
      return false;

  // Handle the special case of a pawn move
  if (type_of(pc) == PAWN)
  {
      // Move direction must be compatible with pawn color
      int direction = to - from;
      if ((us == WHITE) != (direction > 0))
          return false;

      // We have already handled promotion moves, so destination
      // cannot be on the 8/1th rank.
      if (rank_of(to) == RANK_8 || rank_of(to) == RANK_1)
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
      if (color_of(piece_on(to)) != them)
          return false;

      // From and to files must be one file apart, avoids a7h5
      if (abs(file_of(from) - file_of(to)) != 1)
          return false;
      break;

      case DELTA_N:
      case DELTA_S:
      // Pawn push. The destination square must be empty.
      if (!square_is_empty(to))
          return false;
      break;

      case DELTA_NN:
      // Double white pawn push. The destination square must be on the fourth
      // rank, and both the destination square and the square between the
      // source and destination squares must be empty.
      if (   rank_of(to) != RANK_4
          || !square_is_empty(to)
          || !square_is_empty(from + DELTA_N))
          return false;
      break;

      case DELTA_SS:
      // Double black pawn push. The destination square must be on the fifth
      // rank, and both the destination square and the square between the
      // source and destination squares must be empty.
      if (   rank_of(to) != RANK_5
          || !square_is_empty(to)
          || !square_is_empty(from + DELTA_S))
          return false;
      break;

      default:
          return false;
      }
  }
  else if (!bit_is_set(attacks_from(pc, from), to))
      return false;

  // Evasions generator already takes care to avoid some kind of illegal moves
  // and pl_move_is_legal() relies on this. So we have to take care that the
  // same kind of moves are filtered out here.
  if (in_check())
  {
      // In case of king moves under check we have to remove king so to catch
      // as invalid moves like b1a1 when opposite queen is on c1.
      if (type_of(piece_on(from)) == KING)
      {
          Bitboard b = occupied_squares();
          clear_bit(&b, from);
          if (attackers_to(move_to(m), b) & pieces(flip(us)))
              return false;
      }
      else
      {
          Bitboard target = checkers();
          Square checksq = pop_1st_bit(&target);

          if (target) // double check ? In this case a king move is required
              return false;

          // Our move must be a blocking evasion or a capture of the checking piece
          target = squares_between(checksq, king_square(us)) | checkers();
          if (!bit_is_set(target, move_to(m)))
              return false;
      }
  }

  return true;
}


/// Position::move_gives_check() tests whether a pseudo-legal move gives a check

bool Position::move_gives_check(Move m, const CheckInfo& ci) const {

  assert(is_ok(m));
  assert(ci.dcCandidates == discovered_check_candidates());
  assert(color_of(piece_on(move_from(m))) == side_to_move());

  Square from = move_from(m);
  Square to = move_to(m);
  PieceType pt = type_of(piece_on(from));

  // Direct check ?
  if (bit_is_set(ci.checkSq[pt], to))
      return true;

  // Discovery check ?
  if (ci.dcCandidates && bit_is_set(ci.dcCandidates, from))
  {
      // For pawn and king moves we need to verify also direction
      if (  (pt != PAWN && pt != KING)
          || !squares_aligned(from, to, king_square(flip(side_to_move()))))
          return true;
  }

  // Can we skip the ugly special cases ?
  if (!is_special(m))
      return false;

  Color us = side_to_move();
  Bitboard b = occupied_squares();
  Square ksq = king_square(flip(us));

  // Promotion with check ?
  if (is_promotion(m))
  {
      clear_bit(&b, from);
      return bit_is_set(attacks_from(Piece(promotion_piece_type(m)), to, b), ksq);
  }

  // En passant capture with check ? We have already handled the case
  // of direct checks and ordinary discovered check, the only case we
  // need to handle is the unusual case of a discovered check through
  // the captured pawn.
  if (is_enpassant(m))
  {
      Square capsq = make_square(file_of(to), rank_of(from));
      clear_bit(&b, from);
      clear_bit(&b, capsq);
      set_bit(&b, to);
      return  (rook_attacks_bb(ksq, b) & pieces(ROOK, QUEEN, us))
            ||(bishop_attacks_bb(ksq, b) & pieces(BISHOP, QUEEN, us));
  }

  // Castling with check ?
  if (is_castle(m))
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
      return bit_is_set(rook_attacks_bb(rto, b), ksq);
  }

  return false;
}


/// Position::do_move() makes a move, and saves all information necessary
/// to a StateInfo object. The move is assumed to be legal. Pseudo-legal
/// moves should be filtered out before this function is called.

void Position::do_move(Move m, StateInfo& newSt) {

  CheckInfo ci(*this);
  do_move(m, newSt, ci, move_gives_check(m, ci));
}

void Position::do_move(Move m, StateInfo& newSt, const CheckInfo& ci, bool moveIsCheck) {

  assert(is_ok(m));
  assert(&newSt != st);

  nodes++;
  Key k = st->key;

  // Copy some fields of old state to our new StateInfo object except the ones
  // which are recalculated from scratch anyway, then switch our state pointer
  // to point to the new, ready to be updated, state.
  struct ReducedStateInfo {
    Key pawnKey, materialKey;
    Value npMaterial[2];
    int castleRights, rule50, pliesFromNull;
    Score value;
    Square epSquare;
  };

  memcpy(&newSt, st, sizeof(ReducedStateInfo));

  newSt.previous = st;
  st = &newSt;

  // Update side to move
  k ^= zobSideToMove;

  // Increment the 50 moves rule draw counter. Resetting it to zero in the
  // case of non-reversible moves is taken care of later.
  st->rule50++;
  st->pliesFromNull++;

  if (is_castle(m))
  {
      st->key = k;
      do_castle_move<true>(m);
      return;
  }

  Color us = side_to_move();
  Color them = flip(us);
  Square from = move_from(m);
  Square to = move_to(m);
  Piece piece = piece_on(from);
  PieceType pt = type_of(piece);
  PieceType capture = is_enpassant(m) ? PAWN : type_of(piece_on(to));

  assert(color_of(piece) == us);
  assert(color_of(piece_on(to)) != us);
  assert(capture != KING);

  if (capture)
  {
      Square capsq = to;

      // If the captured piece is a pawn, update pawn hash key, otherwise
      // update non-pawn material.
      if (capture == PAWN)
      {
          if (is_enpassant(m))
          {
              capsq += pawn_push(them);

              assert(pt == PAWN);
              assert(to == st->epSquare);
              assert(relative_rank(us, to) == RANK_6);
              assert(piece_on(to) == NO_PIECE);
              assert(piece_on(capsq) == make_piece(them, PAWN));

              board[capsq] = NO_PIECE;
          }

          st->pawnKey ^= zobrist[them][PAWN][capsq];
      }
      else
          st->npMaterial[them] -= PieceValueMidgame[capture];

      // Remove the captured piece
      clear_bit(&byColorBB[them], capsq);
      clear_bit(&byTypeBB[capture], capsq);
      clear_bit(&occupied, capsq);

      // Update piece list, move the last piece at index[capsq] position and
      // shrink the list.
      //
      // WARNING: This is a not revresible operation. When we will reinsert the
      // captured piece in undo_move() we will put it at the end of the list and
      // not in its original place, it means index[] and pieceList[] are not
      // guaranteed to be invariant to a do_move() + undo_move() sequence.
      Square lastSquare = pieceList[them][capture][--pieceCount[them][capture]];
      index[lastSquare] = index[capsq];
      pieceList[them][capture][index[lastSquare]] = lastSquare;
      pieceList[them][capture][pieceCount[them][capture]] = SQ_NONE;

      // Update hash keys
      k ^= zobrist[them][capture][capsq];
      st->materialKey ^= zobrist[them][capture][pieceCount[them][capture]];

      // Update incremental scores
      st->value -= pst(make_piece(them, capture), capsq);

      // Reset rule 50 counter
      st->rule50 = 0;
  }

  // Update hash key
  k ^= zobrist[us][pt][from] ^ zobrist[us][pt][to];

  // Reset en passant square
  if (st->epSquare != SQ_NONE)
  {
      k ^= zobEp[st->epSquare];
      st->epSquare = SQ_NONE;
  }

  // Update castle rights if needed
  if (    st->castleRights != CASTLES_NONE
      && (castleRightsMask[from] & castleRightsMask[to]) != ALL_CASTLES)
  {
      k ^= zobCastle[st->castleRights];
      st->castleRights &= castleRightsMask[from] & castleRightsMask[to];
      k ^= zobCastle[st->castleRights];
  }

  // Prefetch TT access as soon as we know key is updated
  prefetch((char*)TT.first_entry(k));

  // Move the piece
  Bitboard move_bb = make_move_bb(from, to);
  do_move_bb(&byColorBB[us], move_bb);
  do_move_bb(&byTypeBB[pt], move_bb);
  do_move_bb(&occupied, move_bb);

  board[to] = board[from];
  board[from] = NO_PIECE;

  // Update piece lists, index[from] is not updated and becomes stale. This
  // works as long as index[] is accessed just by known occupied squares.
  index[to] = index[from];
  pieceList[us][pt][index[to]] = to;

  // If the moving piece is a pawn do some special extra work
  if (pt == PAWN)
  {
      // Set en-passant square, only if moved pawn can be captured
      if (   (to ^ from) == 16
          && (attacks_from<PAWN>(from + pawn_push(us), us) & pieces(PAWN, them)))
      {
          st->epSquare = Square((from + to) / 2);
          k ^= zobEp[st->epSquare];
      }

      if (is_promotion(m))
      {
          PieceType promotion = promotion_piece_type(m);

          assert(relative_rank(us, to) == RANK_8);
          assert(promotion >= KNIGHT && promotion <= QUEEN);

          // Replace the pawn with the promoted piece
          clear_bit(&byTypeBB[PAWN], to);
          set_bit(&byTypeBB[promotion], to);
          board[to] = make_piece(us, promotion);

          // Update piece lists, move the last pawn at index[to] position
          // and shrink the list. Add a new promotion piece to the list.
          Square lastSquare = pieceList[us][PAWN][--pieceCount[us][PAWN]];
          index[lastSquare] = index[to];
          pieceList[us][PAWN][index[lastSquare]] = lastSquare;
          pieceList[us][PAWN][pieceCount[us][PAWN]] = SQ_NONE;
          index[to] = pieceCount[us][promotion];
          pieceList[us][promotion][index[to]] = to;

          // Update hash keys
          k ^= zobrist[us][PAWN][to] ^ zobrist[us][promotion][to];
          st->pawnKey ^= zobrist[us][PAWN][to];
          st->materialKey ^=  zobrist[us][promotion][pieceCount[us][promotion]++]
                            ^ zobrist[us][PAWN][pieceCount[us][PAWN]];

          // Update incremental score
          st->value +=  pst(make_piece(us, promotion), to)
                      - pst(make_piece(us, PAWN), to);

          // Update material
          st->npMaterial[us] += PieceValueMidgame[promotion];
      }

      // Update pawn hash key
      st->pawnKey ^= zobrist[us][PAWN][from] ^ zobrist[us][PAWN][to];

      // Reset rule 50 draw counter
      st->rule50 = 0;
  }

  // Prefetch pawn and material hash tables
  Threads[threadID].pawnTable.prefetch(st->pawnKey);
  Threads[threadID].materialTable.prefetch(st->materialKey);

  // Update incremental scores
  st->value += pst_delta(piece, from, to);

  // Set capture piece
  st->capturedType = capture;

  // Update the key with the final value
  st->key = k;

  // Update checkers bitboard, piece must be already moved
  st->checkersBB = 0;

  if (moveIsCheck)
  {
      if (is_special(m))
          st->checkersBB = attackers_to(king_square(them)) & pieces(us);
      else
      {
          // Direct checks
          if (bit_is_set(ci.checkSq[pt], to))
              st->checkersBB = SetMaskBB[to];

          // Discovery checks
          if (ci.dcCandidates && bit_is_set(ci.dcCandidates, from))
          {
              if (pt != ROOK)
                  st->checkersBB |= attacks_from<ROOK>(king_square(them)) & pieces(ROOK, QUEEN, us);

              if (pt != BISHOP)
                  st->checkersBB |= attacks_from<BISHOP>(king_square(them)) & pieces(BISHOP, QUEEN, us);
          }
      }
  }

  // Finish
  sideToMove = flip(sideToMove);
  st->value += (sideToMove == WHITE ?  TempoValue : -TempoValue);

  assert(pos_is_ok());
}


/// Position::undo_move() unmakes a move. When it returns, the position should
/// be restored to exactly the same state as before the move was made.

void Position::undo_move(Move m) {

  assert(is_ok(m));

  sideToMove = flip(sideToMove);

  if (is_castle(m))
  {
      do_castle_move<false>(m);
      return;
  }

  Color us = side_to_move();
  Color them = flip(us);
  Square from = move_from(m);
  Square to = move_to(m);
  Piece piece = piece_on(to);
  PieceType pt = type_of(piece);
  PieceType capture = st->capturedType;

  assert(square_is_empty(from));
  assert(color_of(piece) == us);
  assert(capture != KING);

  if (is_promotion(m))
  {
      PieceType promotion = promotion_piece_type(m);

      assert(promotion == pt);
      assert(relative_rank(us, to) == RANK_8);
      assert(promotion >= KNIGHT && promotion <= QUEEN);

      // Replace the promoted piece with the pawn
      clear_bit(&byTypeBB[promotion], to);
      set_bit(&byTypeBB[PAWN], to);
      board[to] = make_piece(us, PAWN);

      // Update piece lists, move the last promoted piece at index[to] position
      // and shrink the list. Add a new pawn to the list.
      Square lastSquare = pieceList[us][promotion][--pieceCount[us][promotion]];
      index[lastSquare] = index[to];
      pieceList[us][promotion][index[lastSquare]] = lastSquare;
      pieceList[us][promotion][pieceCount[us][promotion]] = SQ_NONE;
      index[to] = pieceCount[us][PAWN]++;
      pieceList[us][PAWN][index[to]] = to;

      pt = PAWN;
  }

  // Put the piece back at the source square
  Bitboard move_bb = make_move_bb(to, from);
  do_move_bb(&byColorBB[us], move_bb);
  do_move_bb(&byTypeBB[pt], move_bb);
  do_move_bb(&occupied, move_bb);

  board[from] = board[to];
  board[to] = NO_PIECE;

  // Update piece lists, index[to] is not updated and becomes stale. This
  // works as long as index[] is accessed just by known occupied squares.
  index[from] = index[to];
  pieceList[us][pt][index[from]] = from;

  if (capture)
  {
      Square capsq = to;

      if (is_enpassant(m))
      {
          capsq -= pawn_push(us);

          assert(pt == PAWN);
          assert(to == st->previous->epSquare);
          assert(relative_rank(us, to) == RANK_6);
          assert(piece_on(capsq) == NO_PIECE);
      }

      // Restore the captured piece
      set_bit(&byColorBB[them], capsq);
      set_bit(&byTypeBB[capture], capsq);
      set_bit(&occupied, capsq);

      board[capsq] = make_piece(them, capture);

      // Update piece list, add a new captured piece in capsq square
      index[capsq] = pieceCount[them][capture]++;
      pieceList[them][capture][index[capsq]] = capsq;
  }

  // Finally point our state pointer back to the previous state
  st = st->previous;

  assert(pos_is_ok());
}


/// Position::do_castle_move() is a private method used to do/undo a castling
/// move. Note that castling moves are encoded as "king captures friendly rook"
/// moves, for instance white short castling in a non-Chess960 game is encoded
/// as e1h1.
template<bool Do>
void Position::do_castle_move(Move m) {

  assert(is_ok(m));
  assert(is_castle(m));

  Square kto, kfrom, rfrom, rto, kAfter, rAfter;

  Color us = side_to_move();
  Square kBefore = move_from(m);
  Square rBefore = move_to(m);

  // Find after-castle squares for king and rook
  if (rBefore > kBefore) // O-O
  {
      kAfter = relative_square(us, SQ_G1);
      rAfter = relative_square(us, SQ_F1);
  }
  else // O-O-O
  {
      kAfter = relative_square(us, SQ_C1);
      rAfter = relative_square(us, SQ_D1);
  }

  kfrom = Do ? kBefore : kAfter;
  rfrom = Do ? rBefore : rAfter;

  kto = Do ? kAfter : kBefore;
  rto = Do ? rAfter : rBefore;

  assert(piece_on(kfrom) == make_piece(us, KING));
  assert(piece_on(rfrom) == make_piece(us, ROOK));

  // Remove pieces from source squares
  clear_bit(&byColorBB[us], kfrom);
  clear_bit(&byTypeBB[KING], kfrom);
  clear_bit(&occupied, kfrom);
  clear_bit(&byColorBB[us], rfrom);
  clear_bit(&byTypeBB[ROOK], rfrom);
  clear_bit(&occupied, rfrom);

  // Put pieces on destination squares
  set_bit(&byColorBB[us], kto);
  set_bit(&byTypeBB[KING], kto);
  set_bit(&occupied, kto);
  set_bit(&byColorBB[us], rto);
  set_bit(&byTypeBB[ROOK], rto);
  set_bit(&occupied, rto);

  // Update board
  Piece king = make_piece(us, KING);
  Piece rook = make_piece(us, ROOK);
  board[kfrom] = board[rfrom] = NO_PIECE;
  board[kto] = king;
  board[rto] = rook;

  // Update piece lists
  pieceList[us][KING][index[kfrom]] = kto;
  pieceList[us][ROOK][index[rfrom]] = rto;
  int tmp = index[rfrom]; // In Chess960 could be kto == rfrom
  index[kto] = index[kfrom];
  index[rto] = tmp;

  if (Do)
  {
      // Reset capture field
      st->capturedType = NO_PIECE_TYPE;

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
      st->checkersBB = attackers_to(king_square(flip(us))) & pieces(us);

      // Finish
      sideToMove = flip(sideToMove);
      st->value += (sideToMove == WHITE ?  TempoValue : -TempoValue);
  }
  else
      // Undo: point our state pointer back to the previous state
      st = st->previous;

  assert(pos_is_ok());
}


/// Position::do_null_move() is used to do/undo a "null move": It flips the side
/// to move and updates the hash key without executing any move on the board.
template<bool Do>
void Position::do_null_move(StateInfo& backupSt) {

  assert(!in_check());

  // Back up the information necessary to undo the null move to the supplied
  // StateInfo object. Note that differently from normal case here backupSt
  // is actually used as a backup storage not as the new state. This reduces
  // the number of fields to be copied.
  StateInfo* src = Do ? st : &backupSt;
  StateInfo* dst = Do ? &backupSt : st;

  dst->key      = src->key;
  dst->epSquare = src->epSquare;
  dst->value    = src->value;
  dst->rule50   = src->rule50;
  dst->pliesFromNull = src->pliesFromNull;

  sideToMove = flip(sideToMove);

  if (Do)
  {
      if (st->epSquare != SQ_NONE)
          st->key ^= zobEp[st->epSquare];

      st->key ^= zobSideToMove;
      prefetch((char*)TT.first_entry(st->key));

      st->epSquare = SQ_NONE;
      st->rule50++;
      st->pliesFromNull = 0;
      st->value += (sideToMove == WHITE) ?  TempoValue : -TempoValue;
  }

  assert(pos_is_ok());
}

// Explicit template instantiations
template void Position::do_null_move<false>(StateInfo& backupSt);
template void Position::do_null_move<true>(StateInfo& backupSt);


/// Position::see() is a static exchange evaluator: It tries to estimate the
/// material gain or loss resulting from a move. There are three versions of
/// this function: One which takes a destination square as input, one takes a
/// move, and one which takes a 'from' and a 'to' square. The function does
/// not yet understand promotions captures.

int Position::see_sign(Move m) const {

  assert(is_ok(m));

  Square from = move_from(m);
  Square to = move_to(m);

  // Early return if SEE cannot be negative because captured piece value
  // is not less then capturing one. Note that king moves always return
  // here because king midgame value is set to 0.
  if (PieceValueMidgame[piece_on(to)] >= PieceValueMidgame[piece_on(from)])
      return 1;

  return see(m);
}

int Position::see(Move m) const {

  Square from, to;
  Bitboard occ, attackers, stmAttackers, b;
  int swapList[32], slIndex = 1;
  PieceType capturedType, pt;
  Color stm;

  assert(is_ok(m));

  // As castle moves are implemented as capturing the rook, they have
  // SEE == RookValueMidgame most of the times (unless the rook is under
  // attack).
  if (is_castle(m))
      return 0;

  from = move_from(m);
  to = move_to(m);
  capturedType = type_of(piece_on(to));
  occ = occupied_squares();

  // Handle en passant moves
  if (is_enpassant(m))
  {
      Square capQq = to - pawn_push(side_to_move());

      assert(capturedType == NO_PIECE_TYPE);
      assert(type_of(piece_on(capQq)) == PAWN);

      // Remove the captured pawn
      clear_bit(&occ, capQq);
      capturedType = PAWN;
  }

  // Find all attackers to the destination square, with the moving piece
  // removed, but possibly an X-ray attacker added behind it.
  clear_bit(&occ, from);
  attackers = attackers_to(to, occ);

  // If the opponent has no attackers we are finished
  stm = flip(color_of(piece_on(from)));
  stmAttackers = attackers & pieces(stm);
  if (!stmAttackers)
      return PieceValueMidgame[capturedType];

  // The destination square is defended, which makes things rather more
  // difficult to compute. We proceed by building up a "swap list" containing
  // the material gain or loss at each stop in a sequence of captures to the
  // destination square, where the sides alternately capture, and always
  // capture with the least valuable piece. After each capture, we look for
  // new X-ray attacks from behind the capturing piece.
  swapList[0] = PieceValueMidgame[capturedType];
  capturedType = type_of(piece_on(from));

  do {
      // Locate the least valuable attacker for the side to move. The loop
      // below looks like it is potentially infinite, but it isn't. We know
      // that the side to move still has at least one attacker left.
      for (pt = PAWN; !(stmAttackers & pieces(pt)); pt++)
          assert(pt < KING);

      // Remove the attacker we just found from the 'occupied' bitboard,
      // and scan for new X-ray attacks behind the attacker.
      b = stmAttackers & pieces(pt);
      occ ^= (b & (~b + 1));
      attackers |=  (rook_attacks_bb(to, occ)   & pieces(ROOK, QUEEN))
                  | (bishop_attacks_bb(to, occ) & pieces(BISHOP, QUEEN));

      attackers &= occ; // Cut out pieces we've already done

      // Add the new entry to the swap list
      assert(slIndex < 32);
      swapList[slIndex] = -swapList[slIndex - 1] + PieceValueMidgame[capturedType];
      slIndex++;

      // Remember the value of the capturing piece, and change the side to
      // move before beginning the next iteration.
      capturedType = pt;
      stm = flip(stm);
      stmAttackers = attackers & pieces(stm);

      // Stop before processing a king capture
      if (capturedType == KING && stmAttackers)
      {
          assert(slIndex < 32);
          swapList[slIndex++] = QueenValueMidgame*10;
          break;
      }
  } while (stmAttackers);

  // Having built the swap list, we negamax through it to find the best
  // achievable score from the point of view of the side to move.
  while (--slIndex)
      swapList[slIndex-1] = std::min(-swapList[slIndex], swapList[slIndex-1]);

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

  for (int i = 0; i < 8; i++)
      for (int j = 0; j < 16; j++)
          pieceList[0][i][j] = pieceList[1][i][j] = SQ_NONE;

  for (Square sq = SQ_A1; sq <= SQ_H8; sq++)
  {
      board[sq] = NO_PIECE;
      castleRightsMask[sq] = ALL_CASTLES;
  }
  sideToMove = WHITE;
  nodes = 0;
  occupied = 0;
}


/// Position::put_piece() puts a piece on the given square of the board,
/// updating the board array, pieces list, bitboards, and piece counts.

void Position::put_piece(Piece p, Square s) {

  Color c = color_of(p);
  PieceType pt = type_of(p);

  board[s] = p;
  index[s] = pieceCount[c][pt]++;
  pieceList[c][pt][index[s]] = s;

  set_bit(&byTypeBB[pt], s);
  set_bit(&byColorBB[c], s);
  set_bit(&occupied, s);
}


/// Position::compute_key() computes the hash key of the position. The hash
/// key is usually updated incrementally as moves are made and unmade, the
/// compute_key() function is only used when a new position is set up, and
/// to verify the correctness of the hash key when running in debug mode.

Key Position::compute_key() const {

  Key result = zobCastle[st->castleRights];

  for (Square s = SQ_A1; s <= SQ_H8; s++)
      if (!square_is_empty(s))
          result ^= zobrist[color_of(piece_on(s))][type_of(piece_on(s))][s];

  if (ep_square() != SQ_NONE)
      result ^= zobEp[ep_square()];

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

  Bitboard b;
  Key result = 0;

  for (Color c = WHITE; c <= BLACK; c++)
  {
      b = pieces(PAWN, c);
      while (b)
          result ^= zobrist[c][PAWN][pop_1st_bit(&b)];
  }
  return result;
}


/// Position::compute_material_key() computes the hash key of the position.
/// The hash key is usually updated incrementally as moves are made and unmade,
/// the compute_material_key() function is only used when a new position is set
/// up, and to verify the correctness of the material hash key when running in
/// debug mode.

Key Position::compute_material_key() const {

  Key result = 0;

  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= QUEEN; pt++)
          for (int i = 0; i < piece_count(c, pt); i++)
              result ^= zobrist[c][pt][i];

  return result;
}


/// Position::compute_value() compute the incremental scores for the middle
/// game and the endgame. These functions are used to initialize the incremental
/// scores when a new position is set up, and to verify that the scores are correctly
/// updated by do_move and undo_move when the program is running in debug mode.
Score Position::compute_value() const {

  Bitboard b;
  Score result = SCORE_ZERO;

  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= KING; pt++)
      {
          b = pieces(pt, c);
          while (b)
              result += pst(make_piece(c, pt), pop_1st_bit(&b));
      }

  result += (side_to_move() == WHITE ? TempoValue / 2 : -TempoValue / 2);
  return result;
}


/// Position::compute_non_pawn_material() computes the total non-pawn middle
/// game material value for the given side. Material values are updated
/// incrementally during the search, this function is only used while
/// initializing a new Position object.

Value Position::compute_non_pawn_material(Color c) const {

  Value result = VALUE_ZERO;

  for (PieceType pt = KNIGHT; pt <= QUEEN; pt++)
      result += piece_count(c, pt) * PieceValueMidgame[pt];

  return result;
}


/// Position::is_draw() tests whether the position is drawn by material,
/// repetition, or the 50 moves rule. It does not detect stalemates, this
/// must be done by the search.
template<bool SkipRepetition>
bool Position::is_draw() const {

  // Draw by material?
  if (   !pieces(PAWN)
      && (non_pawn_material(WHITE) + non_pawn_material(BLACK) <= BishopValueMidgame))
      return true;

  // Draw by the 50 moves rule?
  if (st->rule50 > 99 && !is_mate())
      return true;

  // Draw by repetition?
  if (!SkipRepetition)
  {
      int i = 4, e = std::min(st->rule50, st->pliesFromNull);

      if (i <= e)
      {
          StateInfo* stp = st->previous->previous;

          do {
              stp = stp->previous->previous;

              if (stp->key == st->key)
                  return true;

              i +=2;

          } while (i <= e);
      }
  }

  return false;
}

// Explicit template instantiations
template bool Position::is_draw<false>() const;
template bool Position::is_draw<true>() const;


/// Position::is_mate() returns true or false depending on whether the
/// side to move is checkmated.

bool Position::is_mate() const {

  return in_check() && !MoveList<MV_LEGAL>(*this).size();
}


/// Position::init() is a static member function which initializes at startup
/// the various arrays used to compute hash keys and the piece square tables.
/// The latter is a two-step operation: First, the white halves of the tables
/// are copied from PSQT[] tables. Second, the black halves of the tables are
/// initialized by flipping and changing the sign of the white scores.

void Position::init() {

  RKISS rk;

  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= KING; pt++)
          for (Square s = SQ_A1; s <= SQ_H8; s++)
              zobrist[c][pt][s] = rk.rand<Key>();

  for (Square s = SQ_A1; s <= SQ_H8; s++)
      zobEp[s] = rk.rand<Key>();

  for (int i = 0; i < 16; i++)
      zobCastle[i] = rk.rand<Key>();

  zobSideToMove = rk.rand<Key>();
  zobExclusion  = rk.rand<Key>();

  for (Piece p = W_PAWN; p <= W_KING; p++)
  {
      Score ps = make_score(PieceValueMidgame[p], PieceValueEndgame[p]);

      for (Square s = SQ_A1; s <= SQ_H8; s++)
      {
          pieceSquareTable[p][s] = ps + PSQT[p][s];
          pieceSquareTable[p+8][flip(s)] = -pieceSquareTable[p][s];
      }
  }
}


/// Position::flip_me() flips position with the white and black sides reversed. This
/// is only useful for debugging especially for finding evaluation symmetry bugs.

void Position::flip_me() {

  // Make a copy of current position before to start changing
  const Position pos(*this, threadID);

  clear();
  threadID = pos.thread();

  // Board
  for (Square s = SQ_A1; s <= SQ_H8; s++)
      if (!pos.square_is_empty(s))
          put_piece(Piece(pos.piece_on(s) ^ 8), flip(s));

  // Side to move
  sideToMove = flip(pos.side_to_move());

  // Castling rights
  if (pos.can_castle(WHITE_OO))
      set_castle_right(king_square(BLACK), flip(pos.castle_rook_square(WHITE_OO)));
  if (pos.can_castle(WHITE_OOO))
      set_castle_right(king_square(BLACK), flip(pos.castle_rook_square(WHITE_OOO)));
  if (pos.can_castle(BLACK_OO))
      set_castle_right(king_square(WHITE), flip(pos.castle_rook_square(BLACK_OO)));
  if (pos.can_castle(BLACK_OOO))
      set_castle_right(king_square(WHITE), flip(pos.castle_rook_square(BLACK_OOO)));

  // En passant square
  if (pos.st->epSquare != SQ_NONE)
      st->epSquare = flip(pos.st->epSquare);

  // Checkers
  st->checkersBB = attackers_to(king_square(sideToMove)) & pieces(flip(sideToMove));

  // Hash keys
  st->key = compute_key();
  st->pawnKey = compute_pawn_key();
  st->materialKey = compute_material_key();

  // Incremental scores
  st->value = compute_value();

  // Material
  st->npMaterial[WHITE] = compute_non_pawn_material(WHITE);
  st->npMaterial[BLACK] = compute_non_pawn_material(BLACK);

  assert(pos_is_ok());
}


/// Position::pos_is_ok() performs some consitency checks for the position object.
/// This is meant to be helpful when debugging.

bool Position::pos_is_ok(int* failedStep) const {

  // What features of the position should be verified?
  const bool debugAll = false;

  const bool debugBitboards       = debugAll || false;
  const bool debugKingCount       = debugAll || false;
  const bool debugKingCapture     = debugAll || false;
  const bool debugCheckerCount    = debugAll || false;
  const bool debugKey             = debugAll || false;
  const bool debugMaterialKey     = debugAll || false;
  const bool debugPawnKey         = debugAll || false;
  const bool debugIncrementalEval = debugAll || false;
  const bool debugNonPawnMaterial = debugAll || false;
  const bool debugPieceCounts     = debugAll || false;
  const bool debugPieceList       = debugAll || false;
  const bool debugCastleSquares   = debugAll || false;

  if (failedStep) *failedStep = 1;

  // Side to move OK?
  if (side_to_move() != WHITE && side_to_move() != BLACK)
      return false;

  // Are the king squares in the position correct?
  if (failedStep) (*failedStep)++;
  if (piece_on(king_square(WHITE)) != W_KING)
      return false;

  if (failedStep) (*failedStep)++;
  if (piece_on(king_square(BLACK)) != B_KING)
      return false;

  // Do both sides have exactly one king?
  if (failedStep) (*failedStep)++;
  if (debugKingCount)
  {
      int kingCount[2] = {0, 0};
      for (Square s = SQ_A1; s <= SQ_H8; s++)
          if (type_of(piece_on(s)) == KING)
              kingCount[color_of(piece_on(s))]++;

      if (kingCount[0] != 1 || kingCount[1] != 1)
          return false;
  }

  // Can the side to move capture the opponent's king?
  if (failedStep) (*failedStep)++;
  if (debugKingCapture)
  {
      Color us = side_to_move();
      Color them = flip(us);
      Square ksq = king_square(them);
      if (attackers_to(ksq) & pieces(us))
          return false;
  }

  // Is there more than 2 checkers?
  if (failedStep) (*failedStep)++;
  if (debugCheckerCount && popcount<Full>(st->checkersBB) > 2)
      return false;

  // Bitboards OK?
  if (failedStep) (*failedStep)++;
  if (debugBitboards)
  {
      // The intersection of the white and black pieces must be empty
      if (!(pieces(WHITE) & pieces(BLACK)))
          return false;

      // The union of the white and black pieces must be equal to all
      // occupied squares
      if ((pieces(WHITE) | pieces(BLACK)) != occupied_squares())
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
              if (pieceCount[c][pt] != popcount<Full>(pieces(pt, c)))
                  return false;

  if (failedStep) (*failedStep)++;
  if (debugPieceList)
      for (Color c = WHITE; c <= BLACK; c++)
          for (PieceType pt = PAWN; pt <= KING; pt++)
              for (int i = 0; i < pieceCount[c][pt]; i++)
              {
                  if (piece_on(piece_list(c, pt)[i]) != make_piece(c, pt))
                      return false;

                  if (index[piece_list(c, pt)[i]] != i)
                      return false;
              }

  if (failedStep) (*failedStep)++;
  if (debugCastleSquares)
      for (CastleRight f = WHITE_OO; f <= BLACK_OOO; f = CastleRight(f << 1))
      {
          if (!can_castle(f))
              continue;

          Piece rook = (f & (WHITE_OO | WHITE_OOO) ? W_ROOK : B_ROOK);

          if (   castleRightsMask[castleRookSquare[f]] != (ALL_CASTLES ^ f)
              || piece_on(castleRookSquare[f]) != rook)
              return false;
      }

  if (failedStep) *failedStep = 0;
  return true;
}
