/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <algorithm>

#include "bitcount.h"
#include "movegen.h"
#include "notation.h"
#include "position.h"
#include "psqtab.h"
#include "rkiss.h"
#include "thread.h"
#include "tt.h"

using std::string;
using std::cout;
using std::endl;

static const string PieceToChar(" PNBRQK  pnbrqk");

CACHE_LINE_ALIGNMENT

Score psq[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];
Value PieceValue[PHASE_NB][PIECE_NB] = {
{ VALUE_ZERO, PawnValueMg, KnightValueMg, BishopValueMg, RookValueMg, QueenValueMg },
{ VALUE_ZERO, PawnValueEg, KnightValueEg, BishopValueEg, RookValueEg, QueenValueEg } };

namespace Zobrist {

  Key psq[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];
  Key enpassant[FILE_NB];
  Key castle[CASTLE_RIGHT_NB];
  Key side;
  Key exclusion;
}

Key Position::exclusion_key() const { return st->key ^ Zobrist::exclusion;}

namespace {

// next_attacker() is an helper function used by see() to locate the least
// valuable attacker for the side to move, remove the attacker we just found
// from the 'occupied' bitboard and scan for new X-ray attacks behind it.

template<int Pt> FORCE_INLINE
PieceType next_attacker(const Bitboard* bb, const Square& to, const Bitboard& stmAttackers,
                        Bitboard& occupied, Bitboard& attackers) {

  if (stmAttackers & bb[Pt])
  {
      Bitboard b = stmAttackers & bb[Pt];
      occupied ^= b & ~(b - 1);

      if (Pt == PAWN || Pt == BISHOP || Pt == QUEEN)
          attackers |= attacks_bb<BISHOP>(to, occupied) & (bb[BISHOP] | bb[QUEEN]);

      if (Pt == ROOK || Pt == QUEEN)
          attackers |= attacks_bb<ROOK>(to, occupied) & (bb[ROOK] | bb[QUEEN]);

      return (PieceType)Pt;
  }
  return next_attacker<Pt+1>(bb, to, stmAttackers, occupied, attackers);
}

template<> FORCE_INLINE
PieceType next_attacker<KING>(const Bitboard*, const Square&, const Bitboard&, Bitboard&, Bitboard&) {
  return KING; // No need to update bitboards, it is the last cycle
}

} // namespace


/// CheckInfo c'tor

CheckInfo::CheckInfo(const Position& pos) {

  Color them = ~pos.side_to_move();
  ksq = pos.king_square(them);

  pinned = pos.pinned_pieces();
  dcCandidates = pos.discovered_check_candidates();

  checkSq[PAWN]   = pos.attacks_from<PAWN>(ksq, them);
  checkSq[KNIGHT] = pos.attacks_from<KNIGHT>(ksq);
  checkSq[BISHOP] = pos.attacks_from<BISHOP>(ksq);
  checkSq[ROOK]   = pos.attacks_from<ROOK>(ksq);
  checkSq[QUEEN]  = checkSq[BISHOP] | checkSq[ROOK];
  checkSq[KING]   = 0;
}


/// Position::init() initializes at startup the various arrays used to compute
/// hash keys and the piece square tables. The latter is a two-step operation:
/// First, the white halves of the tables are copied from PSQT[] tables. Second,
/// the black halves of the tables are initialized by flipping and changing the
/// sign of the white scores.

void Position::init() {

  RKISS rk;

  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= KING; pt++)
          for (Square s = SQ_A1; s <= SQ_H8; s++)
              Zobrist::psq[c][pt][s] = rk.rand<Key>();

  for (File f = FILE_A; f <= FILE_H; f++)
      Zobrist::enpassant[f] = rk.rand<Key>();

  for (int cr = CASTLES_NONE; cr <= ALL_CASTLES; cr++)
  {
      Bitboard b = cr;
      while (b)
      {
          Key k = Zobrist::castle[1ULL << pop_lsb(&b)];
          Zobrist::castle[cr] ^= k ? k : rk.rand<Key>();
      }
  }

  Zobrist::side = rk.rand<Key>();
  Zobrist::exclusion  = rk.rand<Key>();

  for (PieceType pt = PAWN; pt <= KING; pt++)
  {
      PieceValue[MG][make_piece(BLACK, pt)] = PieceValue[MG][pt];
      PieceValue[EG][make_piece(BLACK, pt)] = PieceValue[EG][pt];

      Score v = make_score(PieceValue[MG][pt], PieceValue[EG][pt]);

      for (Square s = SQ_A1; s <= SQ_H8; s++)
      {
         psq[WHITE][pt][ s] =  (v + PSQT[pt][s]);
         psq[BLACK][pt][~s] = -(v + PSQT[pt][s]);
      }
  }
}


/// Position::operator=() creates a copy of 'pos'. We want the new born Position
/// object do not depend on any external data so we detach state pointer from
/// the source one.

Position& Position::operator=(const Position& pos) {

  std::memcpy(this, &pos, sizeof(Position));
  startState = *st;
  st = &startState;
  nodes = 0;

  assert(pos_is_ok());

  return *this;
}


/// Position::set() initializes the position object with the given FEN string.
/// This function is not very robust - make sure that input FENs are correct,
/// this is assumed to be the responsibility of the GUI.

void Position::set(const string& fenStr, bool isChess960, Thread* th) {
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
  std::istringstream ss(fenStr);

  clear();
  ss >> std::noskipws;

  // 1. Piece placement
  while ((ss >> token) && !isspace(token))
  {
      if (isdigit(token))
          sq += Square(token - '0'); // Advance the given number of files

      else if (token == '/')
          sq -= Square(16);

      else if ((p = PieceToChar.find(token)) != string::npos)
      {
          put_piece(Piece(p), sq);
          sq++;
      }
  }

  // 2. Active color
  ss >> token;
  sideToMove = (token == 'w' ? WHITE : BLACK);
  ss >> token;

  // 3. Castling availability. Compatible with 3 standards: Normal FEN standard,
  // Shredder-FEN that uses the letters of the columns on which the rooks began
  // the game instead of KQkq and also X-FEN standard that, in case of Chess960,
  // if an inner rook is associated with the castling right, the castling tag is
  // replaced by the file letter of the involved rook, as for the Shredder-FEN.
  while ((ss >> token) && !isspace(token))
  {
      Square rsq;
      Color c = islower(token) ? BLACK : WHITE;

      token = char(toupper(token));

      if (token == 'K')
          for (rsq = relative_square(c, SQ_H1); type_of(piece_on(rsq)) != ROOK; rsq--) {}

      else if (token == 'Q')
          for (rsq = relative_square(c, SQ_A1); type_of(piece_on(rsq)) != ROOK; rsq++) {}

      else if (token >= 'A' && token <= 'H')
          rsq = File(token - 'A') | relative_rank(c, RANK_1);

      else
          continue;

      set_castle_right(c, rsq);
  }

  // 4. En passant square. Ignore if no pawn capture is possible
  if (   ((ss >> col) && (col >= 'a' && col <= 'h'))
      && ((ss >> row) && (row == '3' || row == '6')))
  {
      st->epSquare = File(col - 'a') | Rank(row - '1');

      if (!(attackers_to(st->epSquare) & pieces(sideToMove, PAWN)))
          st->epSquare = SQ_NONE;
  }

  // 5-6. Halfmove clock and fullmove number
  ss >> std::skipws >> st->rule50 >> gamePly;

  // Convert from fullmove starting from 1 to ply starting from 0,
  // handle also common incorrect FEN with fullmove = 0.
  gamePly = std::max(2 * (gamePly - 1), 0) + int(sideToMove == BLACK);

  st->key = compute_key();
  st->pawnKey = compute_pawn_key();
  st->materialKey = compute_material_key();
  st->psq = compute_psq_score();
  st->npMaterial[WHITE] = compute_non_pawn_material(WHITE);
  st->npMaterial[BLACK] = compute_non_pawn_material(BLACK);
  st->checkersBB = attackers_to(king_square(sideToMove)) & pieces(~sideToMove);
  chess960 = isChess960;
  thisThread = th;

  assert(pos_is_ok());
}


/// Position::set_castle_right() is an helper function used to set castling
/// rights given the corresponding color and the rook starting square.

void Position::set_castle_right(Color c, Square rfrom) {

  Square kfrom = king_square(c);
  CastlingSide cs = kfrom < rfrom ? KING_SIDE : QUEEN_SIDE;
  CastleRight cr = make_castle_right(c, cs);

  st->castleRights |= cr;
  castleRightsMask[kfrom] |= cr;
  castleRightsMask[rfrom] |= cr;
  castleRookSquare[c][cs] = rfrom;

  Square kto = relative_square(c, cs == KING_SIDE ? SQ_G1 : SQ_C1);
  Square rto = relative_square(c, cs == KING_SIDE ? SQ_F1 : SQ_D1);

  for (Square s = std::min(rfrom, rto); s <= std::max(rfrom, rto); s++)
      if (s != kfrom && s != rfrom)
          castlePath[c][cs] |= s;

  for (Square s = std::min(kfrom, kto); s <= std::max(kfrom, kto); s++)
      if (s != kfrom && s != rfrom)
          castlePath[c][cs] |= s;
}


/// Position::fen() returns a FEN representation of the position. In case
/// of Chess960 the Shredder-FEN notation is used. Mainly a debugging function.

const string Position::fen() const {

  std::ostringstream ss;

  for (Rank rank = RANK_8; rank >= RANK_1; rank--)
  {
      for (File file = FILE_A; file <= FILE_H; file++)
      {
          Square sq = file | rank;

          if (is_empty(sq))
          {
              int emptyCnt = 1;

              for ( ; file < FILE_H && is_empty(sq++); file++)
                  emptyCnt++;

              ss << emptyCnt;
          }
          else
              ss << PieceToChar[piece_on(sq)];
      }

      if (rank > RANK_1)
          ss << '/';
  }

  ss << (sideToMove == WHITE ? " w " : " b ");

  if (can_castle(WHITE_OO))
      ss << (chess960 ? file_to_char(file_of(castle_rook_square(WHITE,  KING_SIDE)), false) : 'K');

  if (can_castle(WHITE_OOO))
      ss << (chess960 ? file_to_char(file_of(castle_rook_square(WHITE, QUEEN_SIDE)), false) : 'Q');

  if (can_castle(BLACK_OO))
      ss << (chess960 ? file_to_char(file_of(castle_rook_square(BLACK,  KING_SIDE)),  true) : 'k');

  if (can_castle(BLACK_OOO))
      ss << (chess960 ? file_to_char(file_of(castle_rook_square(BLACK, QUEEN_SIDE)),  true) : 'q');

  if (st->castleRights == CASTLES_NONE)
      ss << '-';

  ss << (ep_square() == SQ_NONE ? " - " : " " + square_to_string(ep_square()) + " ")
      << st->rule50 << " " << 1 + (gamePly - int(sideToMove == BLACK)) / 2;

  return ss.str();
}


/// Position::pretty() returns an ASCII representation of the position to be
/// printed to the standard output together with the move's san notation.

const string Position::pretty(Move move) const {

  const string dottedLine =            "\n+---+---+---+---+---+---+---+---+";
  const string twoRows =  dottedLine + "\n|   | . |   | . |   | . |   | . |"
                        + dottedLine + "\n| . |   | . |   | . |   | . |   |";

  string brd = twoRows + twoRows + twoRows + twoRows + dottedLine;

  std::ostringstream ss;

  if (move)
      ss << "\nMove: " << (sideToMove == BLACK ? ".." : "")
         << move_to_san(*const_cast<Position*>(this), move);

  for (Square sq = SQ_A1; sq <= SQ_H8; sq++)
      if (piece_on(sq) != NO_PIECE)
          brd[513 - 68*rank_of(sq) + 4*file_of(sq)] = PieceToChar[piece_on(sq)];

  ss << brd << "\nFen: " << fen() << "\nKey: " << std::hex << std::uppercase
     << std::setfill('0') << std::setw(16) << st->key << "\nCheckers: ";

  for (Bitboard b = checkers(); b; )
      ss << square_to_string(pop_lsb(&b)) << " ";

  ss << "\nLegal moves: ";
  for (MoveList<LEGAL> it(*this); *it; ++it)
      ss << move_to_san(*const_cast<Position*>(this), *it) << " ";

  return ss.str();
}


/// Position:hidden_checkers() returns a bitboard of all pinned / discovery check
/// pieces, according to the call parameters. Pinned pieces protect our king,
/// discovery check pieces attack the enemy king.

Bitboard Position::hidden_checkers(Square ksq, Color c) const {

  Bitboard b, pinners, result = 0;

  // Pinners are sliders that give check when pinned piece is removed
  pinners = (  (pieces(  ROOK, QUEEN) & PseudoAttacks[ROOK  ][ksq])
             | (pieces(BISHOP, QUEEN) & PseudoAttacks[BISHOP][ksq])) & pieces(c);

  while (pinners)
  {
      b = between_bb(ksq, pop_lsb(&pinners)) & pieces();

      if (!more_than_one(b))
          result |= b & pieces(sideToMove);
  }
  return result;
}


/// Position::attackers_to() computes a bitboard of all pieces which attack a
/// given square. Slider attacks use occ bitboard as occupancy.

Bitboard Position::attackers_to(Square s, Bitboard occ) const {

  return  (attacks_from<PAWN>(s, BLACK) & pieces(WHITE, PAWN))
        | (attacks_from<PAWN>(s, WHITE) & pieces(BLACK, PAWN))
        | (attacks_from<KNIGHT>(s)      & pieces(KNIGHT))
        | (attacks_bb<ROOK>(s, occ)     & pieces(ROOK, QUEEN))
        | (attacks_bb<BISHOP>(s, occ)   & pieces(BISHOP, QUEEN))
        | (attacks_from<KING>(s)        & pieces(KING));
}


/// Position::attacks_from() computes a bitboard of all attacks of a given piece
/// put in a given square. Slider attacks use occ bitboard as occupancy.

Bitboard Position::attacks_from(Piece p, Square s, Bitboard occ) {

  assert(is_ok(s));

  switch (type_of(p))
  {
  case BISHOP: return attacks_bb<BISHOP>(s, occ);
  case ROOK  : return attacks_bb<ROOK>(s, occ);
  case QUEEN : return attacks_bb<BISHOP>(s, occ) | attacks_bb<ROOK>(s, occ);
  default    : return StepAttacksBB[p][s];
  }
}


/// Position::pl_move_is_legal() tests whether a pseudo-legal move is legal

bool Position::pl_move_is_legal(Move m, Bitboard pinned) const {

  assert(is_ok(m));
  assert(pinned == pinned_pieces());

  Color us = sideToMove;
  Square from = from_sq(m);

  assert(color_of(piece_moved(m)) == us);
  assert(piece_on(king_square(us)) == make_piece(us, KING));

  // En passant captures are a tricky special case. Because they are rather
  // uncommon, we do it simply by testing whether the king is attacked after
  // the move is made.
  if (type_of(m) == ENPASSANT)
  {
      Color them = ~us;
      Square to = to_sq(m);
      Square capsq = to + pawn_push(them);
      Square ksq = king_square(us);
      Bitboard b = (pieces() ^ from ^ capsq) | to;

      assert(to == ep_square());
      assert(piece_moved(m) == make_piece(us, PAWN));
      assert(piece_on(capsq) == make_piece(them, PAWN));
      assert(piece_on(to) == NO_PIECE);

      return   !(attacks_bb<  ROOK>(ksq, b) & pieces(them, QUEEN, ROOK))
            && !(attacks_bb<BISHOP>(ksq, b) & pieces(them, QUEEN, BISHOP));
  }

  // If the moving piece is a king, check whether the destination
  // square is attacked by the opponent. Castling moves are checked
  // for legality during move generation.
  if (type_of(piece_on(from)) == KING)
      return type_of(m) == CASTLE || !(attackers_to(to_sq(m)) & pieces(~us));

  // A non-king move is legal if and only if it is not pinned or it
  // is moving along the ray towards or away from the king.
  return   !pinned
        || !(pinned & from)
        ||  squares_aligned(from, to_sq(m), king_square(us));
}


/// Position::is_pseudo_legal() takes a random move and tests whether the move
/// is pseudo legal. It is used to validate moves from TT that can be corrupted
/// due to SMP concurrent access or hash position key aliasing.

bool Position::is_pseudo_legal(const Move m) const {

  Color us = sideToMove;
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = piece_moved(m);

  // Use a slower but simpler function for uncommon cases
  if (type_of(m) != NORMAL)
      return MoveList<LEGAL>(*this).contains(m);

  // Is not a promotion, so promotion piece must be empty
  if (promotion_type(m) - 2 != NO_PIECE_TYPE)
      return false;

  // If the from square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if (pc == NO_PIECE || color_of(pc) != us)
      return false;

  // The destination square cannot be occupied by a friendly piece
  if (piece_on(to) != NO_PIECE && color_of(piece_on(to)) == us)
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
      if (piece_on(to) == NO_PIECE || color_of(piece_on(to)) != ~us)
          return false;

      // From and to files must be one file apart, avoids a7h5
      if (abs(file_of(from) - file_of(to)) != 1)
          return false;
      break;

      case DELTA_N:
      case DELTA_S:
      // Pawn push. The destination square must be empty.
      if (!is_empty(to))
          return false;
      break;

      case DELTA_NN:
      // Double white pawn push. The destination square must be on the fourth
      // rank, and both the destination square and the square between the
      // source and destination squares must be empty.
      if (    rank_of(to) != RANK_4
          || !is_empty(to)
          || !is_empty(from + DELTA_N))
          return false;
      break;

      case DELTA_SS:
      // Double black pawn push. The destination square must be on the fifth
      // rank, and both the destination square and the square between the
      // source and destination squares must be empty.
      if (    rank_of(to) != RANK_5
          || !is_empty(to)
          || !is_empty(from + DELTA_S))
          return false;
      break;

      default:
          return false;
      }
  }
  else if (!(attacks_from(pc, from) & to))
      return false;

  // Evasions generator already takes care to avoid some kind of illegal moves
  // and pl_move_is_legal() relies on this. So we have to take care that the
  // same kind of moves are filtered out here.
  if (checkers())
  {
      if (type_of(pc) != KING)
      {
          // Double check? In this case a king move is required
          if (more_than_one(checkers()))
              return false;

          // Our move must be a blocking evasion or a capture of the checking piece
          if (!((between_bb(lsb(checkers()), king_square(us)) | checkers()) & to))
              return false;
      }
      // In case of king moves under check we have to remove king so to catch
      // as invalid moves like b1a1 when opposite queen is on c1.
      else if (attackers_to(to, pieces() ^ from) & pieces(~us))
          return false;
  }

  return true;
}


/// Position::move_gives_check() tests whether a pseudo-legal move gives a check

bool Position::move_gives_check(Move m, const CheckInfo& ci) const {

  assert(is_ok(m));
  assert(ci.dcCandidates == discovered_check_candidates());
  assert(color_of(piece_moved(m)) == sideToMove);

  Square from = from_sq(m);
  Square to = to_sq(m);
  PieceType pt = type_of(piece_on(from));

  // Direct check ?
  if (ci.checkSq[pt] & to)
      return true;

  // Discovery check ?
  if (ci.dcCandidates && (ci.dcCandidates & from))
  {
      // For pawn and king moves we need to verify also direction
      if (   (pt != PAWN && pt != KING)
          || !squares_aligned(from, to, king_square(~sideToMove)))
          return true;
  }

  // Can we skip the ugly special cases ?
  if (type_of(m) == NORMAL)
      return false;

  Color us = sideToMove;
  Square ksq = king_square(~us);

  switch (type_of(m))
  {
  case PROMOTION:
      return attacks_from(Piece(promotion_type(m)), to, pieces() ^ from) & ksq;

  // En passant capture with check ? We have already handled the case
  // of direct checks and ordinary discovered check, the only case we
  // need to handle is the unusual case of a discovered check through
  // the captured pawn.
  case ENPASSANT:
  {
      Square capsq = file_of(to) | rank_of(from);
      Bitboard b = (pieces() ^ from ^ capsq) | to;

      return  (attacks_bb<  ROOK>(ksq, b) & pieces(us, QUEEN, ROOK))
            | (attacks_bb<BISHOP>(ksq, b) & pieces(us, QUEEN, BISHOP));
  }
  case CASTLE:
  {
      Square kfrom = from;
      Square rfrom = to; // 'King captures the rook' notation
      Square kto = relative_square(us, rfrom > kfrom ? SQ_G1 : SQ_C1);
      Square rto = relative_square(us, rfrom > kfrom ? SQ_F1 : SQ_D1);
      Bitboard b = (pieces() ^ kfrom ^ rfrom) | rto | kto;

      return attacks_bb<ROOK>(rto, b) & ksq;
  }
  default:
      assert(false);
      return false;
  }
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
  // which are going to be recalculated from scratch anyway, then switch our state
  // pointer to point to the new, ready to be updated, state.
  std::memcpy(&newSt, st, StateCopySize64 * sizeof(uint64_t));

  newSt.previous = st;
  st = &newSt;

  // Update side to move
  k ^= Zobrist::side;

  // Increment ply counters.In particular rule50 will be later reset it to zero
  // in case of a capture or a pawn move.
  gamePly++;
  st->rule50++;
  st->pliesFromNull++;

  Color us = sideToMove;
  Color them = ~us;
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = piece_on(from);
  PieceType pt = type_of(pc);
  PieceType capture = type_of(m) == ENPASSANT ? PAWN : type_of(piece_on(to));

  assert(color_of(pc) == us);
  assert(piece_on(to) == NO_PIECE || color_of(piece_on(to)) == them || type_of(m) == CASTLE);
  assert(capture != KING);

  if (type_of(m) == CASTLE)
  {
      assert(pc == make_piece(us, KING));

      bool kingSide = to > from;
      Square rfrom = to; // Castle is encoded as "king captures friendly rook"
      Square rto = relative_square(us, kingSide ? SQ_F1 : SQ_D1);
      to = relative_square(us, kingSide ? SQ_G1 : SQ_C1);
      capture = NO_PIECE_TYPE;

      do_castle(from, to, rfrom, rto);

      st->psq += psq[us][ROOK][rto] - psq[us][ROOK][rfrom];
      k ^= Zobrist::psq[us][ROOK][rfrom] ^ Zobrist::psq[us][ROOK][rto];
  }

  if (capture)
  {
      Square capsq = to;

      // If the captured piece is a pawn, update pawn hash key, otherwise
      // update non-pawn material.
      if (capture == PAWN)
      {
          if (type_of(m) == ENPASSANT)
          {
              capsq += pawn_push(them);

              assert(pt == PAWN);
              assert(to == st->epSquare);
              assert(relative_rank(us, to) == RANK_6);
              assert(piece_on(to) == NO_PIECE);
              assert(piece_on(capsq) == make_piece(them, PAWN));

              board[capsq] = NO_PIECE;
          }

          st->pawnKey ^= Zobrist::psq[them][PAWN][capsq];
      }
      else
          st->npMaterial[them] -= PieceValue[MG][capture];

      // Remove the captured piece
      byTypeBB[ALL_PIECES] ^= capsq;
      byTypeBB[capture] ^= capsq;
      byColorBB[them] ^= capsq;

      // Update piece list, move the last piece at index[capsq] position and
      // shrink the list.
      //
      // WARNING: This is a not reversible operation. When we will reinsert the
      // captured piece in undo_move() we will put it at the end of the list and
      // not in its original place, it means index[] and pieceList[] are not
      // guaranteed to be invariant to a do_move() + undo_move() sequence.
      Square lastSquare = pieceList[them][capture][--pieceCount[them][capture]];
      index[lastSquare] = index[capsq];
      pieceList[them][capture][index[lastSquare]] = lastSquare;
      pieceList[them][capture][pieceCount[them][capture]] = SQ_NONE;

      // Update material hash key and prefetch access to materialTable
      k ^= Zobrist::psq[them][capture][capsq];
      st->materialKey ^= Zobrist::psq[them][capture][pieceCount[them][capture]];
      prefetch((char*)thisThread->materialTable[st->materialKey]);

      // Update incremental scores
      st->psq -= psq[them][capture][capsq];

      // Reset rule 50 counter
      st->rule50 = 0;
  }

  // Update hash key
  k ^= Zobrist::psq[us][pt][from] ^ Zobrist::psq[us][pt][to];

  // Reset en passant square
  if (st->epSquare != SQ_NONE)
  {
      k ^= Zobrist::enpassant[file_of(st->epSquare)];
      st->epSquare = SQ_NONE;
  }

  // Update castle rights if needed
  if (st->castleRights && (castleRightsMask[from] | castleRightsMask[to]))
  {
      int cr = castleRightsMask[from] | castleRightsMask[to];
      k ^= Zobrist::castle[st->castleRights & cr];
      st->castleRights &= ~cr;
  }

  // Prefetch TT access as soon as we know the new hash key
  prefetch((char*)TT.first_entry(k));

  // Move the piece. The tricky Chess960 castle is handled earlier
  if (type_of(m) != CASTLE)
  {
      Bitboard from_to_bb = SquareBB[from] ^ SquareBB[to];
      byTypeBB[ALL_PIECES] ^= from_to_bb;
      byTypeBB[pt] ^= from_to_bb;
      byColorBB[us] ^= from_to_bb;

      board[from] = NO_PIECE;
      board[to] = pc;

      // Update piece lists, index[from] is not updated and becomes stale. This
      // works as long as index[] is accessed just by known occupied squares.
      index[to] = index[from];
      pieceList[us][pt][index[to]] = to;
  }

  // If the moving piece is a pawn do some special extra work
  if (pt == PAWN)
  {
      // Set en-passant square, only if moved pawn can be captured
      if (   (int(to) ^ int(from)) == 16
          && (attacks_from<PAWN>(from + pawn_push(us), us) & pieces(them, PAWN)))
      {
          st->epSquare = Square((from + to) / 2);
          k ^= Zobrist::enpassant[file_of(st->epSquare)];
      }

      if (type_of(m) == PROMOTION)
      {
          PieceType promotion = promotion_type(m);

          assert(relative_rank(us, to) == RANK_8);
          assert(promotion >= KNIGHT && promotion <= QUEEN);

          // Replace the pawn with the promoted piece
          byTypeBB[PAWN] ^= to;
          byTypeBB[promotion] |= to;
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
          k ^= Zobrist::psq[us][PAWN][to] ^ Zobrist::psq[us][promotion][to];
          st->pawnKey ^= Zobrist::psq[us][PAWN][to];
          st->materialKey ^=  Zobrist::psq[us][promotion][pieceCount[us][promotion]++]
                            ^ Zobrist::psq[us][PAWN][pieceCount[us][PAWN]];

          // Update incremental score
          st->psq += psq[us][promotion][to] - psq[us][PAWN][to];

          // Update material
          st->npMaterial[us] += PieceValue[MG][promotion];
      }

      // Update pawn hash key and prefetch access to pawnsTable
      st->pawnKey ^= Zobrist::psq[us][PAWN][from] ^ Zobrist::psq[us][PAWN][to];
      prefetch((char*)thisThread->pawnsTable[st->pawnKey]);

      // Reset rule 50 draw counter
      st->rule50 = 0;
  }

  // Update incremental scores
  st->psq += psq[us][pt][to] - psq[us][pt][from];

  // Set capture piece
  st->capturedType = capture;

  // Update the key with the final value
  st->key = k;

  // Update checkers bitboard, piece must be already moved
  st->checkersBB = 0;

  if (moveIsCheck)
  {
      if (type_of(m) != NORMAL)
          st->checkersBB = attackers_to(king_square(them)) & pieces(us);
      else
      {
          // Direct checks
          if (ci.checkSq[pt] & to)
              st->checkersBB |= to;

          // Discovery checks
          if (ci.dcCandidates && (ci.dcCandidates & from))
          {
              if (pt != ROOK)
                  st->checkersBB |= attacks_from<ROOK>(king_square(them)) & pieces(us, QUEEN, ROOK);

              if (pt != BISHOP)
                  st->checkersBB |= attacks_from<BISHOP>(king_square(them)) & pieces(us, QUEEN, BISHOP);
          }
      }
  }

  sideToMove = ~sideToMove;

  assert(pos_is_ok());
}


/// Position::undo_move() unmakes a move. When it returns, the position should
/// be restored to exactly the same state as before the move was made.

void Position::undo_move(Move m) {

  assert(is_ok(m));

  sideToMove = ~sideToMove;

  Color us = sideToMove;
  Color them = ~us;
  Square from = from_sq(m);
  Square to = to_sq(m);
  PieceType pt = type_of(piece_on(to));
  PieceType capture = st->capturedType;

  assert(is_empty(from) || type_of(m) == CASTLE);
  assert(capture != KING);

  if (type_of(m) == PROMOTION)
  {
      PieceType promotion = promotion_type(m);

      assert(promotion == pt);
      assert(relative_rank(us, to) == RANK_8);
      assert(promotion >= KNIGHT && promotion <= QUEEN);

      // Replace the promoted piece with the pawn
      byTypeBB[promotion] ^= to;
      byTypeBB[PAWN] |= to;
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

  if (type_of(m) == CASTLE)
  {
      bool kingSide = to > from;
      Square rfrom = to; // Castle is encoded as "king captures friendly rook"
      Square rto = relative_square(us, kingSide ? SQ_F1 : SQ_D1);
      to = relative_square(us, kingSide ? SQ_G1 : SQ_C1);
      capture = NO_PIECE_TYPE;
      pt = KING;
      do_castle(to, from, rto, rfrom);
  }
  else
  {
      // Put the piece back at the source square
      Bitboard from_to_bb = SquareBB[from] ^ SquareBB[to];
      byTypeBB[ALL_PIECES] ^= from_to_bb;
      byTypeBB[pt] ^= from_to_bb;
      byColorBB[us] ^= from_to_bb;

      board[to] = NO_PIECE;
      board[from] = make_piece(us, pt);

      // Update piece lists, index[to] is not updated and becomes stale. This
      // works as long as index[] is accessed just by known occupied squares.
      index[from] = index[to];
      pieceList[us][pt][index[from]] = from;
  }

  if (capture)
  {
      Square capsq = to;

      if (type_of(m) == ENPASSANT)
      {
          capsq -= pawn_push(us);

          assert(pt == PAWN);
          assert(to == st->previous->epSquare);
          assert(relative_rank(us, to) == RANK_6);
          assert(piece_on(capsq) == NO_PIECE);
      }

      // Restore the captured piece
      byTypeBB[ALL_PIECES] |= capsq;
      byTypeBB[capture] |= capsq;
      byColorBB[them] |= capsq;

      board[capsq] = make_piece(them, capture);

      // Update piece list, add a new captured piece in capsq square
      index[capsq] = pieceCount[them][capture]++;
      pieceList[them][capture][index[capsq]] = capsq;
  }

  // Finally point our state pointer back to the previous state
  st = st->previous;
  gamePly--;

  assert(pos_is_ok());
}


/// Position::do_castle() is a helper used to do/undo a castling move. This
/// is a bit tricky, especially in Chess960.

void Position::do_castle(Square kfrom, Square kto, Square rfrom, Square rto) {

  Color us = sideToMove;
  Bitboard k_from_to_bb = SquareBB[kfrom] ^ SquareBB[kto];
  Bitboard r_from_to_bb = SquareBB[rfrom] ^ SquareBB[rto];
  byTypeBB[KING] ^= k_from_to_bb;
  byTypeBB[ROOK] ^= r_from_to_bb;
  byTypeBB[ALL_PIECES] ^= k_from_to_bb ^ r_from_to_bb;
  byColorBB[us] ^= k_from_to_bb ^ r_from_to_bb;

  // Could be from == to, so first set NO_PIECE then KING and ROOK
  board[kfrom] = board[rfrom] = NO_PIECE;
  board[kto] = make_piece(us, KING);
  board[rto] = make_piece(us, ROOK);

  // Could be kfrom == rto, so use a 'tmp' variable
  int tmp = index[kfrom];
  index[rto] = index[rfrom];
  index[kto] = tmp;
  pieceList[us][KING][index[kto]] = kto;
  pieceList[us][ROOK][index[rto]] = rto;
}


/// Position::do(undo)_null_move() is used to do(undo) a "null move": It flips
/// the side to move without executing any move on the board.

void Position::do_null_move(StateInfo& newSt) {

  assert(!checkers());

  std::memcpy(&newSt, st, sizeof(StateInfo)); // Fully copy here

  newSt.previous = st;
  st = &newSt;

  if (st->epSquare != SQ_NONE)
  {
      st->key ^= Zobrist::enpassant[file_of(st->epSquare)];
      st->epSquare = SQ_NONE;
  }

  st->key ^= Zobrist::side;
  prefetch((char*)TT.first_entry(st->key));

  st->rule50++;
  st->pliesFromNull = 0;

  sideToMove = ~sideToMove;

  assert(pos_is_ok());
}

void Position::undo_null_move() {

  assert(!checkers());

  st = st->previous;
  sideToMove = ~sideToMove;
}


/// Position::see() is a static exchange evaluator: It tries to estimate the
/// material gain or loss resulting from a move. Parameter 'asymmThreshold' takes
/// tempi into account. If the side who initiated the capturing sequence does the
/// last capture, he loses a tempo and if the result is below 'asymmThreshold'
/// the capturing sequence is considered bad.

int Position::see_sign(Move m) const {

  assert(is_ok(m));

  // Early return if SEE cannot be negative because captured piece value
  // is not less then capturing one. Note that king moves always return
  // here because king midgame value is set to 0.
  if (PieceValue[MG][piece_on(to_sq(m))] >= PieceValue[MG][piece_moved(m)])
      return 1;

  return see(m);
}

int Position::see(Move m, int asymmThreshold) const {

  Square from, to;
  Bitboard occupied, attackers, stmAttackers;
  int swapList[32], slIndex = 1;
  PieceType captured;
  Color stm;

  assert(is_ok(m));

  from = from_sq(m);
  to = to_sq(m);
  captured = type_of(piece_on(to));
  occupied = pieces() ^ from;

  // Handle en passant moves
  if (type_of(m) == ENPASSANT)
  {
      Square capQq = to - pawn_push(sideToMove);

      assert(!captured);
      assert(type_of(piece_on(capQq)) == PAWN);

      // Remove the captured pawn
      occupied ^= capQq;
      captured = PAWN;
  }
  else if (type_of(m) == CASTLE)
      // Castle moves are implemented as king capturing the rook so cannot be
      // handled correctly. Simply return 0 that is always the correct value
      // unless the rook is ends up under attack.
      return 0;

  // Find all attackers to the destination square, with the moving piece
  // removed, but possibly an X-ray attacker added behind it.
  attackers = attackers_to(to, occupied);

  // If the opponent has no attackers we are finished
  stm = ~color_of(piece_on(from));
  stmAttackers = attackers & pieces(stm);
  if (!stmAttackers)
      return PieceValue[MG][captured];

  // The destination square is defended, which makes things rather more
  // difficult to compute. We proceed by building up a "swap list" containing
  // the material gain or loss at each stop in a sequence of captures to the
  // destination square, where the sides alternately capture, and always
  // capture with the least valuable piece. After each capture, we look for
  // new X-ray attacks from behind the capturing piece.
  swapList[0] = PieceValue[MG][captured];
  captured = type_of(piece_on(from));

  do {
      assert(slIndex < 32);

      // Add the new entry to the swap list
      swapList[slIndex] = -swapList[slIndex - 1] + PieceValue[MG][captured];
      slIndex++;

      // Locate and remove from 'occupied' the next least valuable attacker
      captured = next_attacker<PAWN>(byTypeBB, to, stmAttackers, occupied, attackers);

      attackers &= occupied; // Remove the just found attacker
      stm = ~stm;
      stmAttackers = attackers & pieces(stm);

      if (captured == KING)
      {
          // Stop before processing a king capture
          if (stmAttackers)
              swapList[slIndex++] = QueenValueMg * 16;

          break;
      }

  } while (stmAttackers);

  // If we are doing asymmetric SEE evaluation and the same side does the first
  // and the last capture, he loses a tempo and gain must be at least worth
  // 'asymmThreshold', otherwise we replace the score with a very low value,
  // before negamaxing.
  if (asymmThreshold)
      for (int i = 0; i < slIndex; i += 2)
          if (swapList[i] < asymmThreshold)
              swapList[i] = - QueenValueMg * 16;

  // Having built the swap list, we negamax through it to find the best
  // achievable score from the point of view of the side to move.
  while (--slIndex)
      swapList[slIndex-1] = std::min(-swapList[slIndex], swapList[slIndex-1]);

  return swapList[0];
}


/// Position::clear() erases the position object to a pristine state, with an
/// empty board, white to move, and no castling rights.

void Position::clear() {

  std::memset(this, 0, sizeof(Position));
  startState.epSquare = SQ_NONE;
  st = &startState;

  for (int i = 0; i < 8; i++)
      for (int j = 0; j < 16; j++)
          pieceList[0][i][j] = pieceList[1][i][j] = SQ_NONE;
}


/// Position::put_piece() puts a piece on the given square of the board,
/// updating the board array, pieces list, bitboards, and piece counts.

void Position::put_piece(Piece p, Square s) {

  Color c = color_of(p);
  PieceType pt = type_of(p);

  board[s] = p;
  index[s] = pieceCount[c][pt]++;
  pieceList[c][pt][index[s]] = s;

  byTypeBB[ALL_PIECES] |= s;
  byTypeBB[pt] |= s;
  byColorBB[c] |= s;
}


/// Position::compute_key() computes the hash key of the position. The hash
/// key is usually updated incrementally as moves are made and unmade, the
/// compute_key() function is only used when a new position is set up, and
/// to verify the correctness of the hash key when running in debug mode.

Key Position::compute_key() const {

  Key k = Zobrist::castle[st->castleRights];

  for (Bitboard b = pieces(); b; )
  {
      Square s = pop_lsb(&b);
      k ^= Zobrist::psq[color_of(piece_on(s))][type_of(piece_on(s))][s];
  }

  if (ep_square() != SQ_NONE)
      k ^= Zobrist::enpassant[file_of(ep_square())];

  if (sideToMove == BLACK)
      k ^= Zobrist::side;

  return k;
}


/// Position::compute_pawn_key() computes the hash key of the position. The
/// hash key is usually updated incrementally as moves are made and unmade,
/// the compute_pawn_key() function is only used when a new position is set
/// up, and to verify the correctness of the pawn hash key when running in
/// debug mode.

Key Position::compute_pawn_key() const {

  Key k = 0;

  for (Bitboard b = pieces(PAWN); b; )
  {
      Square s = pop_lsb(&b);
      k ^= Zobrist::psq[color_of(piece_on(s))][PAWN][s];
  }

  return k;
}


/// Position::compute_material_key() computes the hash key of the position.
/// The hash key is usually updated incrementally as moves are made and unmade,
/// the compute_material_key() function is only used when a new position is set
/// up, and to verify the correctness of the material hash key when running in
/// debug mode.

Key Position::compute_material_key() const {

  Key k = 0;

  for (Color c = WHITE; c <= BLACK; c++)
      for (PieceType pt = PAWN; pt <= QUEEN; pt++)
          for (int cnt = 0; cnt < pieceCount[c][pt]; cnt++)
              k ^= Zobrist::psq[c][pt][cnt];

  return k;
}


/// Position::compute_psq_score() computes the incremental scores for the middle
/// game and the endgame. These functions are used to initialize the incremental
/// scores when a new position is set up, and to verify that the scores are correctly
/// updated by do_move and undo_move when the program is running in debug mode.
Score Position::compute_psq_score() const {

  Score score = SCORE_ZERO;

  for (Bitboard b = pieces(); b; )
  {
      Square s = pop_lsb(&b);
      Piece pc = piece_on(s);
      score += psq[color_of(pc)][type_of(pc)][s];
  }

  return score;
}


/// Position::compute_non_pawn_material() computes the total non-pawn middle
/// game material value for the given side. Material values are updated
/// incrementally during the search, this function is only used while
/// initializing a new Position object.

Value Position::compute_non_pawn_material(Color c) const {

  Value value = VALUE_ZERO;

  for (PieceType pt = KNIGHT; pt <= QUEEN; pt++)
      value += pieceCount[c][pt] * PieceValue[MG][pt];

  return value;
}


/// Position::is_draw() tests whether the position is drawn by material,
/// repetition, or the 50 moves rule. It does not detect stalemates, this
/// must be done by the search.
bool Position::is_draw() const {

  // Draw by material?
  if (   !pieces(PAWN)
      && (non_pawn_material(WHITE) + non_pawn_material(BLACK) <= BishopValueMg))
      return true;

  // Draw by the 50 moves rule?
  if (st->rule50 > 99 && (!checkers() || MoveList<LEGAL>(*this).size()))
      return true;

  // Draw by repetition?
  int i = 4, e = std::min(st->rule50, st->pliesFromNull);

  if (i <= e)
  {
      StateInfo* stp = st->previous->previous;

      do {
          stp = stp->previous->previous;

          if (stp->key == st->key)
              return true;

          i += 2;

      } while (i <= e);
  }

  return false;
}


/// Position::flip() flips position with the white and black sides reversed. This
/// is only useful for debugging especially for finding evaluation symmetry bugs.

void Position::flip() {

  const Position pos(*this);

  clear();

  sideToMove = ~pos.side_to_move();
  thisThread = pos.this_thread();
  nodes = pos.nodes_searched();
  chess960 = pos.is_chess960();
  gamePly = pos.game_ply();

  for (Square s = SQ_A1; s <= SQ_H8; s++)
      if (!pos.is_empty(s))
          put_piece(Piece(pos.piece_on(s) ^ 8), ~s);

  if (pos.can_castle(WHITE_OO))
      set_castle_right(BLACK, ~pos.castle_rook_square(WHITE, KING_SIDE));
  if (pos.can_castle(WHITE_OOO))
      set_castle_right(BLACK, ~pos.castle_rook_square(WHITE, QUEEN_SIDE));
  if (pos.can_castle(BLACK_OO))
      set_castle_right(WHITE, ~pos.castle_rook_square(BLACK, KING_SIDE));
  if (pos.can_castle(BLACK_OOO))
      set_castle_right(WHITE, ~pos.castle_rook_square(BLACK, QUEEN_SIDE));

  if (pos.st->epSquare != SQ_NONE)
      st->epSquare = ~pos.st->epSquare;

  st->checkersBB = attackers_to(king_square(sideToMove)) & pieces(~sideToMove);

  st->key = compute_key();
  st->pawnKey = compute_pawn_key();
  st->materialKey = compute_material_key();
  st->psq = compute_psq_score();
  st->npMaterial[WHITE] = compute_non_pawn_material(WHITE);
  st->npMaterial[BLACK] = compute_non_pawn_material(BLACK);

  assert(pos_is_ok());
}


/// Position::pos_is_ok() performs some consitency checks for the position object.
/// This is meant to be helpful when debugging.

bool Position::pos_is_ok(int* failedStep) const {

  int dummy, *step = failedStep ? failedStep : &dummy;

  // What features of the position should be verified?
  const bool all = false;

  const bool debugBitboards       = all || false;
  const bool debugKingCount       = all || false;
  const bool debugKingCapture     = all || false;
  const bool debugCheckerCount    = all || false;
  const bool debugKey             = all || false;
  const bool debugMaterialKey     = all || false;
  const bool debugPawnKey         = all || false;
  const bool debugIncrementalEval = all || false;
  const bool debugNonPawnMaterial = all || false;
  const bool debugPieceCounts     = all || false;
  const bool debugPieceList       = all || false;
  const bool debugCastleSquares   = all || false;

  *step = 1;

  if (sideToMove != WHITE && sideToMove != BLACK)
      return false;

  if ((*step)++, piece_on(king_square(WHITE)) != W_KING)
      return false;

  if ((*step)++, piece_on(king_square(BLACK)) != B_KING)
      return false;

  if ((*step)++, debugKingCount)
  {
      int kingCount[COLOR_NB] = {};

      for (Square s = SQ_A1; s <= SQ_H8; s++)
          if (type_of(piece_on(s)) == KING)
              kingCount[color_of(piece_on(s))]++;

      if (kingCount[0] != 1 || kingCount[1] != 1)
          return false;
  }

  if ((*step)++, debugKingCapture)
      if (attackers_to(king_square(~sideToMove)) & pieces(sideToMove))
          return false;

  if ((*step)++, debugCheckerCount && popcount<Full>(st->checkersBB) > 2)
      return false;

  if ((*step)++, debugBitboards)
  {
      // The intersection of the white and black pieces must be empty
      if (pieces(WHITE) & pieces(BLACK))
          return false;

      // The union of the white and black pieces must be equal to all
      // occupied squares
      if ((pieces(WHITE) | pieces(BLACK)) != pieces())
          return false;

      // Separate piece type bitboards must have empty intersections
      for (PieceType p1 = PAWN; p1 <= KING; p1++)
          for (PieceType p2 = PAWN; p2 <= KING; p2++)
              if (p1 != p2 && (pieces(p1) & pieces(p2)))
                  return false;
  }

  if ((*step)++, ep_square() != SQ_NONE && relative_rank(sideToMove, ep_square()) != RANK_6)
      return false;

  if ((*step)++, debugKey && st->key != compute_key())
      return false;

  if ((*step)++, debugPawnKey && st->pawnKey != compute_pawn_key())
      return false;

  if ((*step)++, debugMaterialKey && st->materialKey != compute_material_key())
      return false;

  if ((*step)++, debugIncrementalEval && st->psq != compute_psq_score())
      return false;

  if ((*step)++, debugNonPawnMaterial)
      if (   st->npMaterial[WHITE] != compute_non_pawn_material(WHITE)
          || st->npMaterial[BLACK] != compute_non_pawn_material(BLACK))
          return false;

  if ((*step)++, debugPieceCounts)
      for (Color c = WHITE; c <= BLACK; c++)
          for (PieceType pt = PAWN; pt <= KING; pt++)
              if (pieceCount[c][pt] != popcount<Full>(pieces(c, pt)))
                  return false;

  if ((*step)++, debugPieceList)
      for (Color c = WHITE; c <= BLACK; c++)
          for (PieceType pt = PAWN; pt <= KING; pt++)
              for (int i = 0; i < pieceCount[c][pt]; i++)
                  if (   board[pieceList[c][pt][i]] != make_piece(c, pt)
                      || index[pieceList[c][pt][i]] != i)
                      return false;

  if ((*step)++, debugCastleSquares)
      for (Color c = WHITE; c <= BLACK; c++)
          for (CastlingSide s = KING_SIDE; s <= QUEEN_SIDE; s = CastlingSide(s + 1))
          {
              CastleRight cr = make_castle_right(c, s);

              if (!can_castle(cr))
                  continue;

              if (  (castleRightsMask[king_square(c)] & cr) != cr
                  || piece_on(castleRookSquare[c][s]) != make_piece(c, ROOK)
                  || castleRightsMask[castleRookSquare[c][s]] != cr)
                  return false;
          }

  *step = 0;
  return true;
}
