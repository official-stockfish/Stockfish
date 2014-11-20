/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "bitcount.h"
#include "movegen.h"
#include "position.h"
#include "psqtab.h"
#include "rkiss.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

using std::string;

Value PieceValue[PHASE_NB][PIECE_NB] = {
{ VALUE_ZERO, PawnValueMg, KnightValueMg, BishopValueMg, RookValueMg, QueenValueMg },
{ VALUE_ZERO, PawnValueEg, KnightValueEg, BishopValueEg, RookValueEg, QueenValueEg } };

namespace Zobrist {

  Key psq[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];
  Key enpassant[FILE_NB];
  Key castling[CASTLING_RIGHT_NB];
  Key side;
  Key exclusion;
}

Key Position::exclusion_key() const { return state->key ^ Zobrist::exclusion;}

namespace {

const string PieceToChar(" PNBRQK  pnbrqk");
Score psq[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB];

// min_attacker() is a helper function used by see() to locate the least
// valuable attacker for the side to move, remove the attacker we just found
// from the bitboards and scan for new X-ray attacks behind it.

template<int Pt> FORCE_INLINE
PieceType min_attacker(const Bitboard* bitboard, const Square& to, const Bitboard& sideToMoveAttackers,
                       Bitboard& occupied, Bitboard& attackers) {

  Bitboard bitboard2 = sideToMoveAttackers & bitboard[Pt];
  if (!bitboard2)
      return min_attacker<Pt+1>(bitboard, to, sideToMoveAttackers, occupied, attackers);

  occupied ^= bitboard2 & ~(bitboard2 - 1);

  if (Pt == PAWN || Pt == BISHOP || Pt == QUEEN)
      attackers |= attacks_bb<BISHOP>(to, occupied) & (bitboard[BISHOP] | bitboard[QUEEN]);

  if (Pt == ROOK || Pt == QUEEN)
      attackers |= attacks_bb<ROOK>(to, occupied) & (bitboard[ROOK] | bitboard[QUEEN]);

  attackers &= occupied; // After X-ray that may add already processed pieces
  return (PieceType)Pt;
}

template<> FORCE_INLINE
PieceType min_attacker<KING>(const Bitboard*, const Square&, const Bitboard&, Bitboard&, Bitboard&) {
  return KING; // No need to update bitboards: it is the last cycle
}

} // namespace


/// CheckInfo c'tor

CheckInfo::CheckInfo(const Position& pos) {

  Color them = ~pos.side_to_move();
  kingSquare = pos.king_square(them);

  pinned = pos.pinned_pieces(pos.side_to_move());
  discoveredCheckCandidates = pos.discovered_check_candidates();

  checkSquares[PAWN]   = pos.attacks_from<PAWN>(kingSquare, them);
  checkSquares[KNIGHT] = pos.attacks_from<KNIGHT>(kingSquare);
  checkSquares[BISHOP] = pos.attacks_from<BISHOP>(kingSquare);
  checkSquares[ROOK]   = pos.attacks_from<ROOK>(kingSquare);
  checkSquares[QUEEN]  = checkSquares[BISHOP] | checkSquares[ROOK];
  checkSquares[KING]   = 0;
}


/// operator<<(Position) returns an ASCII representation of the position

std::ostream& operator<<(std::ostream& os, const Position& pos) {

  os << "\n +---+---+---+---+---+---+---+---+\n";

  for (Rank rank = RANK_8; rank >= RANK_1; --rank)
  {
      for (File file = FILE_A; file <= FILE_H; ++file)
          os << " | " << PieceToChar[pos.piece_on(make_square(file, rank))];

      os << " |\n +---+---+---+---+---+---+---+---+\n";
  }

  os << "\nFen: " << pos.fen() << "\nKey: " << std::hex << std::uppercase
     << std::setfill('0') << std::setw(16) << pos.state->key << std::dec << "\nCheckers: ";

  for (Bitboard bitboard = pos.checkers(); bitboard; )
      os << UCI::format_square(pop_lsb(&bitboard)) << " ";

  return os;
}


/// Position::init() initializes at startup the various arrays used to compute
/// hash keys and the piece square tables. The latter is a two-step operation:
/// Firstly, the white halves of the tables are copied from PSQT[] tables.
/// Secondly, the black halves of the tables are initialized by flipping and
/// changing the sign of the white scores.

void Position::init() {

  RKISS rk;

  for (Color color = WHITE; color <= BLACK; ++color)
      for (PieceType pieceType = PAWN; pieceType <= KING; ++pieceType)
          for (Square square = SQ_A1; square <= SQ_H8; ++square)
              Zobrist::psq[color][pieceType][square] = rk.rand<Key>();

  for (File file = FILE_A; file <= FILE_H; ++file)
      Zobrist::enpassant[file] = rk.rand<Key>();

  for (int castlingRight = NO_CASTLING; castlingRight <= ANY_CASTLING; ++castlingRight)
  {
      Bitboard bitboard = castlingRight;
      while (bitboard)
      {
          Key key = Zobrist::castling[1ULL << pop_lsb(&bitboard)];
          Zobrist::castling[castlingRight] ^= key ? key : rk.rand<Key>();
      }
  }

  Zobrist::side = rk.rand<Key>();
  Zobrist::exclusion  = rk.rand<Key>();

  for (PieceType pieceType = PAWN; pieceType <= KING; ++pieceType)
  {
      PieceValue[MG][make_piece(BLACK, pieceType)] = PieceValue[MG][pieceType];
      PieceValue[EG][make_piece(BLACK, pieceType)] = PieceValue[EG][pieceType];

      Score score = make_score(PieceValue[MG][pieceType], PieceValue[EG][pieceType]);

      for (Square square = SQ_A1; square <= SQ_H8; ++square)
      {
         psq[WHITE][pieceType][ square] =  (score + PSQT[pieceType][square]);
         psq[BLACK][pieceType][~square] = -(score + PSQT[pieceType][square]);
      }
  }
}


/// Position::operator=() creates a copy of 'pos'. We want the new born Position
/// object to not depend on any external data so we detach state pointer from
/// the source one.

Position& Position::operator=(const Position& pos) {

  std::memcpy(this, &pos, sizeof(Position));
  startState = *state;
  state = &startState;
  nodes = 0;

  assert(pos_is_ok());

  return *this;
}


/// Position::clear() erases the position object to a pristine state, with an
/// empty board, white to move, and no castling rights.

void Position::clear() {

  std::memset(this, 0, sizeof(Position));
  startState.enpassantSquare = SQ_NONE;
  state = &startState;

  for (int i = 0; i < PIECE_TYPE_NB; ++i)
      for (int j = 0; j < 16; ++j)
          pieceList[WHITE][i][j] = pieceList[BLACK][i][j] = SQ_NONE;
}


/// Position::set() initializes the position object with the given FEN string.
/// This function is not very robust - make sure that input FENs are correct,
/// this is assumed to be the responsibility of the GUI.

void Position::set(const string& fenString, bool isChess960, Thread* thread) {
/*
   A FEN string defines a particular position using only the ASCII character set.

   A FEN string contains six fields separated by a space. The fields are:

   1) Piece placement (from white's perspective). Each rank is described, starting
      with rank 8 and ending with rank 1. Within each rank, the contents of each
      square are described from file A through file H. Following the Standard
      Algebraic Notation (SAN), each piece is identified by a single letter taken
      from the standard English names. White pieces are designated using upper-case
      letters ("PNBRQK") whilst Black uses lowercase ("pnbrqk"). Blank squares are
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

  unsigned char column, row, token;
  size_t charIndex;
  Square square = SQ_A8;
  std::istringstream ss(fenString);

  clear();
  ss >> std::noskipws;

  // 1. Piece placement
  while ((ss >> token) && !isspace(token))
  {
      if (isdigit(token))
          square += Square(token - '0'); // Advance the given number of files

      else if (token == '/')
          square -= Square(16);

      else if ((charIndex = PieceToChar.find(token)) != string::npos)
      {
          put_piece(square, color_of(Piece(charIndex)), type_of(Piece(charIndex)));
          ++square;
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
      Square relativeSquare;
      Color color = islower(token) ? BLACK : WHITE;

      token = char(toupper(token));

      if (token == 'K')
          for (relativeSquare = relative_square(color, SQ_H1); type_of(piece_on(relativeSquare)) != ROOK; --relativeSquare) {}

      else if (token == 'Q')
          for (relativeSquare = relative_square(color, SQ_A1); type_of(piece_on(relativeSquare)) != ROOK; ++relativeSquare) {}

      else if (token >= 'A' && token <= 'H')
          relativeSquare = make_square(File(token - 'A'), relative_rank(color, RANK_1));

      else
          continue;

      set_castling_right(color, relativeSquare);
  }

  // 4. En passant square. Ignore if no pawn capture is possible
  if (   ((ss >> column) && (column >= 'a' && column <= 'h'))
      && ((ss >> row) && (row == '3' || row == '6')))
  {
      state->enpassantSquare = make_square(File(column - 'a'), Rank(row - '1'));

      if (!(attackers_to(state->enpassantSquare) & pieces(sideToMove, PAWN)))
          state->enpassantSquare = SQ_NONE;
  }

  // 5-6. Halfmove clock and fullmove number
  ss >> std::skipws >> state->rule50 >> gamePly;

  // Convert from fullmove starting from 1 to ply starting from 0,
  // handle also common incorrect FEN with fullmove = 0.
  gamePly = std::max(2 * (gamePly - 1), 0) + (sideToMove == BLACK);

  chess960 = isChess960;
  thisThread = thread;
  set_state(state);

  assert(pos_is_ok());
}


/// Position::set_castling_right() is a helper function used to set castling
/// rights given the corresponding color and the rook starting square.

void Position::set_castling_right(Color color, Square rookFrom) {

  Square kingFrom = king_square(color);
  CastlingSide castlingSide = kingFrom < rookFrom ? KING_SIDE : QUEEN_SIDE;
  CastlingRight castlingRight = (color | castlingSide);

  state->castlingRights |= castlingRight;
  castlingRightsMask[kingFrom] |= castlingRight;
  castlingRightsMask[rookFrom] |= castlingRight;
  castlingRookSquare[castlingRight] = rookFrom;

  Square kingTo = relative_square(color, castlingSide == KING_SIDE ? SQ_G1 : SQ_C1);
  Square rookTo = relative_square(color, castlingSide == KING_SIDE ? SQ_F1 : SQ_D1);

  for (Square square = std::min(rookFrom, rookTo); square <= std::max(rookFrom, rookTo); ++square)
      if (square != kingFrom && square != rookFrom)
          castlingPath[castlingRight] |= square;

  for (Square square = std::min(kingFrom, kingTo); square <= std::max(kingFrom, kingTo); ++square)
      if (square != kingFrom && square != rookFrom)
          castlingPath[castlingRight] |= square;
}


/// Position::set_state() computes the hash keys of the position, and other
/// data that once computed is updated incrementally as moves are made.
/// The function is only used when a new position is set up, and to verify
/// the correctness of the StateInfo data when running in debug mode.

void Position::set_state(StateInfo* state2) const {

  state2->key = state2->pawnKey = state2->materialKey = 0;
  state2->nonPawnMaterial[WHITE] = state2->nonPawnMaterial[BLACK] = VALUE_ZERO;
  state2->psq = SCORE_ZERO;

  state2->checkersBB = attackers_to(king_square(sideToMove)) & pieces(~sideToMove);

  for (Bitboard bitboard = pieces(); bitboard; )
  {
      Square square = pop_lsb(&bitboard);
      Piece piece = piece_on(square);
      state2->key ^= Zobrist::psq[color_of(piece)][type_of(piece)][square];
      state2->psq += psq[color_of(piece)][type_of(piece)][square];
  }

  if (ep_square() != SQ_NONE)
      state2->key ^= Zobrist::enpassant[file_of(ep_square())];

  if (sideToMove == BLACK)
      state2->key ^= Zobrist::side;

  state2->key ^= Zobrist::castling[state->castlingRights];

  for (Bitboard bitboard = pieces(PAWN); bitboard; )
  {
      Square square = pop_lsb(&bitboard);
      state2->pawnKey ^= Zobrist::psq[color_of(piece_on(square))][PAWN][square];
  }

  for (Color color = WHITE; color <= BLACK; ++color)
      for (PieceType pieceType = PAWN; pieceType <= KING; ++pieceType)
          for (int i = 0; i < pieceCount[color][pieceType]; ++i)
              state2->materialKey ^= Zobrist::psq[color][pieceType][i];

  for (Color color = WHITE; color <= BLACK; ++color)
      for (PieceType pieceType = KNIGHT; pieceType <= QUEEN; ++pieceType)
          state2->nonPawnMaterial[color] += pieceCount[color][pieceType] * PieceValue[MG][pieceType];
}


/// Position::fen() returns a FEN representation of the position. In case of
/// Chess960 the Shredder-FEN notation is used. This is mainly a debugging function.

const string Position::fen() const {

  int emptyCount;
  std::ostringstream ss;

  for (Rank rank = RANK_8; rank >= RANK_1; --rank)
  {
      for (File file = FILE_A; file <= FILE_H; ++file)
      {
          for (emptyCount = 0; file <= FILE_H && empty(make_square(file, rank)); ++file)
              ++emptyCount;

          if (emptyCount)
              ss << emptyCount;

          if (file <= FILE_H)
              ss << PieceToChar[piece_on(make_square(file, rank))];
      }

      if (rank > RANK_1)
          ss << '/';
  }

  ss << (sideToMove == WHITE ? " w " : " b ");

  if (can_castle(WHITE_OO))
      ss << (chess960 ? char('A' + file_of(castling_rook_square(WHITE |  KING_SIDE))) : 'K');

  if (can_castle(WHITE_OOO))
      ss << (chess960 ? char('A' + file_of(castling_rook_square(WHITE | QUEEN_SIDE))) : 'Q');

  if (can_castle(BLACK_OO))
      ss << (chess960 ? char('a' + file_of(castling_rook_square(BLACK |  KING_SIDE))) : 'k');

  if (can_castle(BLACK_OOO))
      ss << (chess960 ? char('a' + file_of(castling_rook_square(BLACK | QUEEN_SIDE))) : 'q');

  if (!can_castle(WHITE) && !can_castle(BLACK))
      ss << '-';

  ss << (ep_square() == SQ_NONE ? " - " : " " + UCI::format_square(ep_square()) + " ")
     << state->rule50 << " " << 1 + (gamePly - (sideToMove == BLACK)) / 2;

  return ss.str();
}


/// Position::game_phase() calculates the game phase interpolating total non-pawn
/// material between endgame and midgame limits.

Phase Position::game_phase() const {

  Value nonPawnMaterial = state->nonPawnMaterial[WHITE] + state->nonPawnMaterial[BLACK];

  nonPawnMaterial = std::max(EndgameLimit, std::min(nonPawnMaterial, MidgameLimit));

  return Phase(((nonPawnMaterial - EndgameLimit) * PHASE_MIDGAME) / (MidgameLimit - EndgameLimit));
}


/// Position::check_blockers() returns a bitboard of all the pieces with color
/// 'color' that are blocking check on the king with color 'kingColor'. A piece
/// blocks a check if removing that piece from the board would result in a
/// position where the king is in check. A check blocking piece can be either a
/// pinned or a discovered check piece, according if its color 'color' is the same
/// or the opposite of 'kingColor'.

Bitboard Position::check_blockers(Color color, Color kingColor) const {

  Bitboard bitboard, pinners, result = 0;
  Square kingSquare = king_square(kingColor);

  // Pinners are sliders that give check when a pinned piece is removed
  pinners = (  (pieces(  ROOK, QUEEN) & PseudoAttacks[ROOK  ][kingSquare])
             | (pieces(BISHOP, QUEEN) & PseudoAttacks[BISHOP][kingSquare])) & pieces(~kingColor);

  while (pinners)
  {
      bitboard = between_bb(kingSquare, pop_lsb(&pinners)) & pieces();

      if (!more_than_one(bitboard))
          result |= bitboard & pieces(color);
  }
  return result;
}


/// Position::attackers_to() computes a bitboard of all pieces which attack a
/// given square. Slider attacks use the occupied bitboard to indicate occupancy.

Bitboard Position::attackers_to(Square square, Bitboard occupied) const {

  return  (attacks_from<PAWN>(square, BLACK)    & pieces(WHITE, PAWN))
        | (attacks_from<PAWN>(square, WHITE)    & pieces(BLACK, PAWN))
        | (attacks_from<KNIGHT>(square)         & pieces(KNIGHT))
        | (attacks_bb<ROOK>(square, occupied)   & pieces(ROOK, QUEEN))
        | (attacks_bb<BISHOP>(square, occupied) & pieces(BISHOP, QUEEN))
        | (attacks_from<KING>(square)           & pieces(KING));
}


/// Position::legal() tests whether a pseudo-legal move is legal

bool Position::legal(Move move, Bitboard pinned) const {

  assert(is_ok(move));
  assert(pinned == pinned_pieces(sideToMove));

  Color us = sideToMove;
  Square from = from_sq(move);

  assert(color_of(moved_piece(move)) == us);
  assert(piece_on(king_square(us)) == make_piece(us, KING));

  // En passant captures are a tricky special case. Because they are rather
  // uncommon, we do it simply by testing whether the king is attacked after
  // the move is made.
  if (type_of(move) == ENPASSANT)
  {
      Square kingSquare = king_square(us);
      Square to = to_sq(move);
      Square captureSquare = to - pawn_push(us);
      Bitboard occupied = (pieces() ^ from ^ captureSquare) | to;

      assert(to == ep_square());
      assert(moved_piece(move) == make_piece(us, PAWN));
      assert(piece_on(captureSquare) == make_piece(~us, PAWN));
      assert(piece_on(to) == NO_PIECE);

      return   !(attacks_bb<  ROOK>(kingSquare, occupied) & pieces(~us, QUEEN, ROOK))
            && !(attacks_bb<BISHOP>(kingSquare, occupied) & pieces(~us, QUEEN, BISHOP));
  }

  // If the moving piece is a king, check whether the destination
  // square is attacked by the opponent. Castling moves are checked
  // for legality during move generation.
  if (type_of(piece_on(from)) == KING)
      return type_of(move) == CASTLING || !(attackers_to(to_sq(move)) & pieces(~us));

  // A non-king move is legal if and only if it is not pinned or it
  // is moving along the ray towards or away from the king.
  return   !pinned
        || !(pinned & from)
        ||  aligned(from, to_sq(move), king_square(us));
}


/// Position::pseudo_legal() takes a random move and tests whether the move is
/// pseudo legal. It is used to validate moves from TT that can be corrupted
/// due to SMP concurrent access or hash position key aliasing.

bool Position::pseudo_legal(const Move move) const {

  Color us = sideToMove;
  Square from = from_sq(move);
  Square to = to_sq(move);
  Piece piece = moved_piece(move);

  // Use a slower but simpler function for uncommon cases
  if (type_of(move) != NORMAL)
      return MoveList<LEGAL>(*this).contains(move);

  // Is not a promotion, so promotion piece must be empty
  if (promotion_type(move) - 2 != NO_PIECE_TYPE)
      return false;

  // If the 'from' square is not occupied by a piece belonging to the side to
  // move, the move is obviously not legal.
  if (piece == NO_PIECE || color_of(piece) != us)
      return false;

  // The destination square cannot be occupied by a friendly piece
  if (pieces(us) & to)
      return false;

  // Handle the special case of a pawn move
  if (type_of(piece) == PAWN)
  {
      // We have already handled promotion moves, so destination
      // cannot be on the 8th/1st rank.
      if (rank_of(to) == relative_rank(us, RANK_8))
          return false;

      if (   !(attacks_from<PAWN>(from, us) & pieces(~us) & to) // Not a capture

          && !((from + pawn_push(us) == to) && empty(to))       // Not a single push

          && !(   (from + 2 * pawn_push(us) == to)              // Not a double push
               && (rank_of(from) == relative_rank(us, RANK_2))
               && empty(to)
               && empty(to - pawn_push(us))))
          return false;
  }
  else if (!(attacks_from(piece, from) & to))
      return false;

  // Evasions generator already takes care to avoid some kind of illegal moves
  // and legal() relies on this. We therefore have to take care that the same
  // kind of moves are filtered out here.
  if (checkers())
  {
      if (type_of(piece) != KING)
      {
          // Double check? In this case a king move is required
          if (more_than_one(checkers()))
              return false;

          // Our move must be a blocking evasion or a capture of the checking piece
          if (!((between_bb(lsb(checkers()), king_square(us)) | checkers()) & to))
              return false;
      }
      // In case of king moves under check we have to remove king so as to catch
      // invalid moves like b1a1 when opposite queen is on c1.
      else if (attackers_to(to, pieces() ^ from) & pieces(~us))
          return false;
  }

  return true;
}


/// Position::gives_check() tests whether a pseudo-legal move gives a check

bool Position::gives_check(Move move, const CheckInfo& checkInfo) const {

  assert(is_ok(move));
  assert(checkInfo.discoveredCheckCandidates == discovered_check_candidates());
  assert(color_of(moved_piece(move)) == sideToMove);

  Square from = from_sq(move);
  Square to = to_sq(move);
  PieceType pieceType = type_of(piece_on(from));

  // Is there a direct check?
  if (checkInfo.checkSquares[pieceType] & to)
      return true;

  // Is there a discovered check?
  if (   unlikely(checkInfo.discoveredCheckCandidates)
      && (checkInfo.discoveredCheckCandidates & from)
      && !aligned(from, to, checkInfo.kingSquare))
      return true;

  switch (type_of(move))
  {
  case NORMAL:
      return false;

  case PROMOTION:
      return attacks_bb(Piece(promotion_type(move)), to, pieces() ^ from) & checkInfo.kingSquare;

  // En passant capture with check? We have already handled the case
  // of direct checks and ordinary discovered check, so the only case we
  // need to handle is the unusual case of a discovered check through
  // the captured pawn.
  case ENPASSANT:
  {
      Square captureSquare = make_square(file_of(to), rank_of(from));
      Bitboard bitboard = (pieces() ^ from ^ captureSquare) | to;

      return  (attacks_bb<  ROOK>(checkInfo.kingSquare, bitboard) & pieces(sideToMove, QUEEN, ROOK))
            | (attacks_bb<BISHOP>(checkInfo.kingSquare, bitboard) & pieces(sideToMove, QUEEN, BISHOP));
  }
  case CASTLING:
  {
      Square kingFrom = from;
      Square rookFrom = to; // Castling is encoded as 'King captures the rook'
      Square kingTo = relative_square(sideToMove, rookFrom > kingFrom ? SQ_G1 : SQ_C1);
      Square rookTo = relative_square(sideToMove, rookFrom > kingFrom ? SQ_F1 : SQ_D1);

      return   (PseudoAttacks[ROOK][rookTo] & checkInfo.kingSquare)
            && (attacks_bb<ROOK>(rookTo, (pieces() ^ kingFrom ^ rookFrom) | rookTo | kingTo) & checkInfo.kingSquare);
  }
  default:
      assert(false);
      return false;
  }
}


/// Position::do_move() makes a move, and saves all information necessary
/// to a StateInfo object. The move is assumed to be legal. Pseudo-legal
/// moves should be filtered out before this function is called.

void Position::do_move(Move move, StateInfo& newState) {

  CheckInfo checkInfo(*this);
  do_move(move, newState, checkInfo, gives_check(move, checkInfo));
}

void Position::do_move(Move move, StateInfo& newState, const CheckInfo& checkInfo, bool moveIsCheck) {

  assert(is_ok(move));
  assert(&newState != state);

  ++nodes;
  Key currentKey = state->key;

  // Copy some fields of the old state to our new StateInfo object except the
  // ones which are going to be recalculated from scratch anyway and then switch
  // our state pointer to point to the new (ready to be updated) state.
  std::memcpy(&newState, state, StateCopySize64 * sizeof(uint64_t));

  newState.previous = state;
  state = &newState;

  // Update side to move
  currentKey ^= Zobrist::side;

  // Increment ply counters. In particular, rule50 will be reset to zero later on
  // in case of a capture or a pawn move.
  ++gamePly;
  ++state->rule50;
  ++state->pliesFromNull;

  Color us = sideToMove;
  Color them = ~us;
  Square from = from_sq(move);
  Square to = to_sq(move);
  Piece piece = piece_on(from);
  PieceType pieceType = type_of(piece);
  PieceType captured = type_of(move) == ENPASSANT ? PAWN : type_of(piece_on(to));

  assert(color_of(piece) == us);
  assert(piece_on(to) == NO_PIECE || color_of(piece_on(to)) == them || type_of(move) == CASTLING);
  assert(captured != KING);

  if (type_of(move) == CASTLING)
  {
      assert(piece == make_piece(us, KING));

      Square rookFrom, rookTo;
      do_castling<true>(from, to, rookFrom, rookTo);

      captured = NO_PIECE_TYPE;
      state->psq += psq[us][ROOK][rookTo] - psq[us][ROOK][rookFrom];
      currentKey ^= Zobrist::psq[us][ROOK][rookFrom] ^ Zobrist::psq[us][ROOK][rookTo];
  }

  if (captured)
  {
      Square captureSquare = to;

      // If the captured piece is a pawn, update pawn hash key, otherwise
      // update non-pawn material.
      if (captured == PAWN)
      {
          if (type_of(move) == ENPASSANT)
          {
              captureSquare += pawn_push(them);

              assert(pieceType == PAWN);
              assert(to == state->enpassantSquare);
              assert(relative_rank(us, to) == RANK_6);
              assert(piece_on(to) == NO_PIECE);
              assert(piece_on(captureSquare) == make_piece(them, PAWN));

              board[captureSquare] = NO_PIECE;
          }

          state->pawnKey ^= Zobrist::psq[them][PAWN][captureSquare];
      }
      else
          state->nonPawnMaterial[them] -= PieceValue[MG][captured];

      // Update board and piece lists
      remove_piece(captureSquare, them, captured);

      // Update material hash key and prefetch access to materialTable
      currentKey ^= Zobrist::psq[them][captured][captureSquare];
      state->materialKey ^= Zobrist::psq[them][captured][pieceCount[them][captured]];
      prefetch((char*)thisThread->materialTable[state->materialKey]);

      // Update incremental scores
      state->psq -= psq[them][captured][captureSquare];

      // Reset rule 50 counter
      state->rule50 = 0;
  }

  // Update hash key
  currentKey ^= Zobrist::psq[us][pieceType][from] ^ Zobrist::psq[us][pieceType][to];

  // Reset en passant square
  if (state->enpassantSquare != SQ_NONE)
  {
      currentKey ^= Zobrist::enpassant[file_of(state->enpassantSquare)];
      state->enpassantSquare = SQ_NONE;
  }

  // Update castling rights if needed
  if (state->castlingRights && (castlingRightsMask[from] | castlingRightsMask[to]))
  {
      int castlingRight = castlingRightsMask[from] | castlingRightsMask[to];
      currentKey ^= Zobrist::castling[state->castlingRights & castlingRight];
      state->castlingRights &= ~castlingRight;
  }

  // Move the piece. The tricky Chess960 castling is handled earlier
  if (type_of(move) != CASTLING)
      move_piece(from, to, us, pieceType);

  // If the moving piece is a pawn do some special extra work
  if (pieceType == PAWN)
  {
      // Set en-passant square if the moved pawn can be captured
      if (   (int(to) ^ int(from)) == 16
          && (attacks_from<PAWN>(from + pawn_push(us), us) & pieces(them, PAWN)))
      {
          state->enpassantSquare = Square((from + to) / 2);
          currentKey ^= Zobrist::enpassant[file_of(state->enpassantSquare)];
      }

      else if (type_of(move) == PROMOTION)
      {
          PieceType promotion = promotion_type(move);

          assert(relative_rank(us, to) == RANK_8);
          assert(promotion >= KNIGHT && promotion <= QUEEN);

          remove_piece(to, us, PAWN);
          put_piece(to, us, promotion);

          // Update hash keys
          currentKey ^= Zobrist::psq[us][PAWN][to] ^ Zobrist::psq[us][promotion][to];
          state->pawnKey ^= Zobrist::psq[us][PAWN][to];
          state->materialKey ^=  Zobrist::psq[us][promotion][pieceCount[us][promotion]-1]
                            ^ Zobrist::psq[us][PAWN][pieceCount[us][PAWN]];

          // Update incremental score
          state->psq += psq[us][promotion][to] - psq[us][PAWN][to];

          // Update material
          state->nonPawnMaterial[us] += PieceValue[MG][promotion];
      }

      // Update pawn hash key and prefetch access to pawnsTable
      state->pawnKey ^= Zobrist::psq[us][PAWN][from] ^ Zobrist::psq[us][PAWN][to];
      prefetch((char*)thisThread->pawnsTable[state->pawnKey]);

      // Reset rule 50 draw counter
      state->rule50 = 0;
  }

  // Update incremental scores
  state->psq += psq[us][pieceType][to] - psq[us][pieceType][from];

  // Set capture piece
  state->capturedType = captured;

  // Update the key with the final value
  state->key = currentKey;

  // Update checkers bitboard: piece must be already moved due to attacks_from()
  state->checkersBB = 0;

  if (moveIsCheck)
  {
      if (type_of(move) != NORMAL)
          state->checkersBB = attackers_to(king_square(them)) & pieces(us);
      else
      {
          // Direct checks
          if (checkInfo.checkSquares[pieceType] & to)
              state->checkersBB |= to;

          // Discovered checks
          if (unlikely(checkInfo.discoveredCheckCandidates) && (checkInfo.discoveredCheckCandidates & from))
          {
              if (pieceType != ROOK)
                  state->checkersBB |= attacks_from<ROOK>(king_square(them)) & pieces(us, QUEEN, ROOK);

              if (pieceType != BISHOP)
                  state->checkersBB |= attacks_from<BISHOP>(king_square(them)) & pieces(us, QUEEN, BISHOP);
          }
      }
  }

  sideToMove = ~sideToMove;

  assert(pos_is_ok());
}


/// Position::undo_move() unmakes a move. When it returns, the position should
/// be restored to exactly the same state as before the move was made.

void Position::undo_move(Move move) {

  assert(is_ok(move));

  sideToMove = ~sideToMove;

  Color us = sideToMove;
  Square from = from_sq(move);
  Square to = to_sq(move);
  PieceType pieceType = type_of(piece_on(to));

  assert(empty(from) || type_of(move) == CASTLING);
  assert(state->capturedType != KING);

  if (type_of(move) == PROMOTION)
  {
      assert(pieceType == promotion_type(move));
      assert(relative_rank(us, to) == RANK_8);
      assert(promotion_type(move) >= KNIGHT && promotion_type(move) <= QUEEN);

      remove_piece(to, us, promotion_type(move));
      put_piece(to, us, PAWN);
      pieceType = PAWN;
  }

  if (type_of(move) == CASTLING)
  {
      Square rookFrom, rookTo;
      do_castling<false>(from, to, rookFrom, rookTo);
  }
  else
  {
      move_piece(to, from, us, pieceType); // Put the piece back at the source square

      if (state->capturedType)
      {
          Square captureSquare = to;

          if (type_of(move) == ENPASSANT)
          {
              captureSquare -= pawn_push(us);

              assert(pieceType == PAWN);
              assert(to == state->previous->enpassantSquare);
              assert(relative_rank(us, to) == RANK_6);
              assert(piece_on(captureSquare) == NO_PIECE);
          }

          put_piece(captureSquare, ~us, state->capturedType); // Restore the captured piece
      }
  }

  // Finally point our state pointer back to the previous state
  state = state->previous;
  --gamePly;

  assert(pos_is_ok());
}


/// Position::do_castling() is a helper used to do/undo a castling move. This
/// is a bit tricky, especially in Chess960.
template<bool Do>
void Position::do_castling(Square from, Square& to, Square& rookFrom, Square& rookTo) {

  bool kingSide = to > from;
  rookFrom = to; // Castling is encoded as "king captures friendly rook"
  rookTo = relative_square(sideToMove, kingSide ? SQ_F1 : SQ_D1);
  to  = relative_square(sideToMove, kingSide ? SQ_G1 : SQ_C1);

  // Remove both pieces first since squares could overlap in Chess960
  remove_piece(Do ?  from :  to, sideToMove, KING);
  remove_piece(Do ? rookFrom : rookTo, sideToMove, ROOK);
  board[Do ? from : to] = board[Do ? rookFrom : rookTo] = NO_PIECE; // Since remove_piece doesn't do it for us
  put_piece(Do ?  to :  from, sideToMove, KING);
  put_piece(Do ? rookTo : rookFrom, sideToMove, ROOK);
}


/// Position::do(undo)_null_move() is used to do(undo) a "null move": It flips
/// the side to move without executing any move on the board.

void Position::do_null_move(StateInfo& newState) {

  assert(!checkers());

  std::memcpy(&newState, state, sizeof(StateInfo)); // Fully copy here

  newState.previous = state;
  state = &newState;

  if (state->enpassantSquare != SQ_NONE)
  {
      state->key ^= Zobrist::enpassant[file_of(state->enpassantSquare)];
      state->enpassantSquare = SQ_NONE;
  }

  state->key ^= Zobrist::side;
  prefetch((char*)TT.first_entry(state->key));

  ++state->rule50;
  state->pliesFromNull = 0;

  sideToMove = ~sideToMove;

  assert(pos_is_ok());
}

void Position::undo_null_move() {

  assert(!checkers());

  state = state->previous;
  sideToMove = ~sideToMove;
}


/// Position::key_after() computes the new hash key after the given move. Needed
/// for speculative prefetch. It doesn't recognize special moves like castling,
/// en-passant and promotions.

Key Position::key_after(Move move) const {

  Color us = sideToMove;
  Square from = from_sq(move);
  Square to = to_sq(move);
  PieceType pieceType = type_of(piece_on(from));
  PieceType captured = type_of(piece_on(to));
  Key currentKey = state->key ^ Zobrist::side;

  if (captured)
      currentKey ^= Zobrist::psq[~us][captured][to];

  return currentKey ^ Zobrist::psq[us][pieceType][to] ^ Zobrist::psq[us][pieceType][from];
}


/// Position::see() is a static exchange evaluator: It tries to estimate the
/// material gain or loss resulting from a move.

Value Position::see_sign(Move move) const {

  assert(is_ok(move));

  // Early return if SEE cannot be negative because captured piece value
  // is not less then capturing one. Note that king moves always return
  // here because king midgame value is set to 0.
  if (PieceValue[MG][moved_piece(move)] <= PieceValue[MG][piece_on(to_sq(move))])
      return VALUE_KNOWN_WIN;

  return see(move);
}

Value Position::see(Move move) const {

  Square from, to;
  Bitboard occupied, attackers, sideToMoveAttackers;
  Value swapList[32];
  int swapListIndex = 1;
  PieceType captured;
  Color currentSideToMove;

  assert(is_ok(move));

  from = from_sq(move);
  to = to_sq(move);
  swapList[0] = PieceValue[MG][piece_on(to)];
  currentSideToMove = color_of(piece_on(from));
  occupied = pieces() ^ from;

  // Castling moves are implemented as king capturing the rook so cannot be
  // handled correctly. Simply return 0 that is always the correct value
  // unless in the rare case the rook ends up under attack.
  if (type_of(move) == CASTLING)
      return VALUE_ZERO;

  if (type_of(move) == ENPASSANT)
  {
      occupied ^= to - pawn_push(currentSideToMove); // Remove the captured pawn
      swapList[0] = PieceValue[MG][PAWN];
  }

  // Find all attackers to the destination square, with the moving piece
  // removed, but possibly an X-ray attacker added behind it.
  attackers = attackers_to(to, occupied) & occupied;

  // If the opponent has no attackers we are finished
  currentSideToMove = ~currentSideToMove;
  sideToMoveAttackers = attackers & pieces(currentSideToMove);
  if (!sideToMoveAttackers)
      return swapList[0];

  // The destination square is defended, which makes things rather more
  // difficult to compute. We proceed by building up a "swap list" containing
  // the material gain or loss at each stop in a sequence of captures to the
  // destination square, where the sides alternately capture, and always
  // capture with the least valuable piece. After each capture, we look for
  // new X-ray attacks from behind the capturing piece.
  captured = type_of(piece_on(from));

  do {
      assert(swapListIndex < 32);

      // Add the new entry to the swap list
      swapList[swapListIndex] = -swapList[swapListIndex - 1] + PieceValue[MG][captured];

      // Locate and remove the next least valuable attacker
      captured = min_attacker<PAWN>(byTypeBB, to, sideToMoveAttackers, occupied, attackers);

      // Stop before processing a king capture
      if (captured == KING)
      {
          if (sideToMoveAttackers == attackers)
              ++swapListIndex;

          break;
      }

      currentSideToMove = ~currentSideToMove;
      sideToMoveAttackers = attackers & pieces(currentSideToMove);
      ++swapListIndex;

  } while (sideToMoveAttackers);

  // Having built the swap list, we negamax through it to find the best
  // achievable score from the point of view of the side to move.
  while (--swapListIndex)
      swapList[swapListIndex - 1] = std::min(-swapList[swapListIndex], swapList[swapListIndex - 1]);

  return swapList[0];
}


/// Position::is_draw() tests whether the position is drawn by material, 50 moves
/// rule or repetition. It does not detect stalemates.

bool Position::is_draw() const {

  if (state->rule50 > 99 && (!checkers() || MoveList<LEGAL>(*this).size()))
      return true;

  StateInfo* statePointer = state;
  for (int i = 2, e = std::min(state->rule50, state->pliesFromNull); i <= e; i += 2)
  {
      statePointer = statePointer->previous->previous;

      if (statePointer->key == state->key)
          return true; // Draw at first repetition
  }

  return false;
}


/// Position::flip() flips position with the white and black sides reversed. This
/// is only useful for debugging e.g. for finding evaluation symmetry bugs.

static char toggle_case(char c) {
  return char(islower(c) ? toupper(c) : tolower(c));
}

void Position::flip() {

  string fenString, token;
  std::stringstream ss(fen());

  for (Rank rank = RANK_8; rank >= RANK_1; --rank) // Piece placement
  {
      std::getline(ss, token, rank > RANK_1 ? '/' : ' ');
      fenString.insert(0, token + (fenString.empty() ? " " : "/"));
  }

  ss >> token; // Active color
  fenString += (token == "w" ? "B " : "W "); // Will be lowercased later

  ss >> token; // Castling availability
  fenString += token + " ";

  std::transform(fenString.begin(), fenString.end(), fenString.begin(), toggle_case);

  ss >> token; // En passant square
  fenString += (token == "-" ? token : token.replace(1, 1, token[1] == '3' ? "6" : "3"));

  std::getline(ss, token); // Half and full moves
  fenString += token;

  set(fenString, is_chess960(), this_thread());

  assert(pos_is_ok());
}


/// Position::pos_is_ok() performs some consistency checks for the position object.
/// This is meant to be helpful when debugging.

bool Position::pos_is_ok(int* step) const {

  // Which parts of the position should be verified?
  const bool all = false;

  const bool testBitboards       = all || false;
  const bool testState           = all || false;
  const bool testKingCount       = all || false;
  const bool testKingCapture     = all || false;
  const bool testPieceCounts     = all || false;
  const bool testPieceList       = all || false;
  const bool testCastlingSquares = all || false;

  if (step)
      *step = 1;

  if (   (sideToMove != WHITE && sideToMove != BLACK)
      || piece_on(king_square(WHITE)) != W_KING
      || piece_on(king_square(BLACK)) != B_KING
      || (   ep_square() != SQ_NONE
          && relative_rank(sideToMove, ep_square()) != RANK_6))
      return false;

  if (step && ++*step, testBitboards)
  {
      // The intersection of the white and black pieces must be empty
      if (pieces(WHITE) & pieces(BLACK))
          return false;

      // The union of the white and black pieces must be equal to all
      // occupied squares
      if ((pieces(WHITE) | pieces(BLACK)) != pieces())
          return false;

      // Separate piece type bitboards must have empty intersections
      for (PieceType pieceType1 = PAWN; pieceType1 <= KING; ++pieceType1)
          for (PieceType pieceType2 = PAWN; pieceType2 <= KING; ++pieceType2)
              if (pieceType1 != pieceType2 && (pieces(pieceType1) & pieces(pieceType2)))
                  return false;
  }

  if (step && ++*step, testState)
  {
      StateInfo state2;
      set_state(&state2);
      if (   state->key != state2.key
          || state->pawnKey != state2.pawnKey
          || state->materialKey != state2.materialKey
          || state->nonPawnMaterial[WHITE] != state2.nonPawnMaterial[WHITE]
          || state->nonPawnMaterial[BLACK] != state2.nonPawnMaterial[BLACK]
          || state->psq != state2.psq
          || state->checkersBB != state2.checkersBB)
          return false;
  }

  if (step && ++*step, testKingCount)
      if (   std::count(board, board + SQUARE_NB, W_KING) != 1
          || std::count(board, board + SQUARE_NB, B_KING) != 1)
          return false;

  if (step && ++*step, testKingCapture)
      if (attackers_to(king_square(~sideToMove)) & pieces(sideToMove))
          return false;

  if (step && ++*step, testPieceCounts)
      for (Color color = WHITE; color <= BLACK; ++color)
          for (PieceType pieceType = PAWN; pieceType <= KING; ++pieceType)
              if (pieceCount[color][pieceType] != popcount<Full>(pieces(color, pieceType)))
                  return false;

  if (step && ++*step, testPieceList)
      for (Color color = WHITE; color <= BLACK; ++color)
          for (PieceType pieceType = PAWN; pieceType <= KING; ++pieceType)
              for (int i = 0; i < pieceCount[color][pieceType];  ++i)
                  if (   board[pieceList[color][pieceType][i]] != make_piece(color, pieceType)
                      || index[pieceList[color][pieceType][i]] != i)
                      return false;

  if (step && ++*step, testCastlingSquares)
      for (Color color = WHITE; color <= BLACK; ++color)
          for (CastlingSide castlingSide = KING_SIDE; castlingSide <= QUEEN_SIDE; castlingSide = CastlingSide(castlingSide + 1))
          {
              if (!can_castle(color | castlingSide))
                  continue;

              if (  (castlingRightsMask[king_square(color)] & (color | castlingSide)) != (color | castlingSide)
                  || piece_on(castlingRookSquare[color | castlingSide]) != make_piece(color, ROOK)
                  || castlingRightsMask[castlingRookSquare[color | castlingSide]] != (color | castlingSide))
                  return false;
          }

  return true;
}
