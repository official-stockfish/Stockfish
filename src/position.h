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

#ifndef POSITION_H_INCLUDED
#define POSITION_H_INCLUDED

#include <cassert>
#include <cstddef>

#include "bitboard.h"
#include "types.h"


/// The checkInfo struct is initialized at c'tor time and keeps info used
/// to detect if a move gives check.
class Position;
struct Thread;

struct CheckInfo {

  explicit CheckInfo(const Position&);

  Bitboard discoveredCheckCandidates;
  Bitboard pinned;
  Bitboard checkSquares[PIECE_TYPE_NB];
  Square kingSquare;
};


/// The StateInfo struct stores information needed to restore a Position
/// object to its previous state when we retract a move. Whenever a move
/// is made on the board (by calling Position::do_move), a StateInfo
/// object must be passed as a parameter.

struct StateInfo {
  Key pawnKey, materialKey;
  Value nonPawnMaterial[COLOR_NB];
  int castlingRights, rule50, pliesFromNull;
  Score psq;
  Square enpassantSquare;

  Key key;
  Bitboard checkersBB;
  PieceType capturedType;
  StateInfo* previous;
};


/// When making a move the current StateInfo up to 'key' excluded is copied to
/// the new one. Here we calculate the quad words (64bits) needed to be copied.
const size_t StateCopySize64 = offsetof(StateInfo, key) / sizeof(uint64_t) + 1;


/// The Position class stores the information regarding the board representation
/// like pieces, side to move, hash keys, castling info, etc. The most important
/// methods are do_move() and undo_move(), used by the search to update node info
/// when traversing the search tree.

class Position {

  friend std::ostream& operator<<(std::ostream&, const Position&);

public:
  Position() {}
  Position(const Position& pos, Thread* thread) { *this = pos; thisThread = thread; }
  Position(const std::string& f, bool c960, Thread* thread) { set(f, c960, thread); }
  Position& operator=(const Position&);
  static void init();

  // FEN string input/output
  void set(const std::string& fenString, bool isChess960, Thread* thread);
  const std::string fen() const;

  // Position representation
  Bitboard pieces() const;
  Bitboard pieces(PieceType pieceType) const;
  Bitboard pieces(PieceType pieceType1, PieceType pieceType2) const;
  Bitboard pieces(Color color) const;
  Bitboard pieces(Color color, PieceType pieceType) const;
  Bitboard pieces(Color color, PieceType pieceType1, PieceType pieceType2) const;
  Piece piece_on(Square square) const;
  Square king_square(Color color) const;
  Square ep_square() const;
  bool empty(Square square) const;
  template<PieceType Pt> int count(Color color) const;
  template<PieceType Pt> const Square* list(Color color) const;

  // Castling
  int can_castle(Color color) const;
  int can_castle(CastlingRight castlingRight) const;
  bool castling_impeded(CastlingRight castlingRight) const;
  Square castling_rook_square(CastlingRight castlingRight) const;

  // Checking
  Bitboard checkers() const;
  Bitboard discovered_check_candidates() const;
  Bitboard pinned_pieces(Color color) const;

  // Attacks to/from a given square
  Bitboard attackers_to(Square square) const;
  Bitboard attackers_to(Square square, Bitboard occupied) const;
  Bitboard attacks_from(Piece piece, Square square) const;
  template<PieceType> Bitboard attacks_from(Square square) const;
  template<PieceType> Bitboard attacks_from(Square square, Color color) const;

  // Properties of moves
  bool legal(Move move, Bitboard pinned) const;
  bool pseudo_legal(const Move move) const;
  bool capture(Move move) const;
  bool capture_or_promotion(Move move) const;
  bool gives_check(Move move, const CheckInfo& checkInfo) const;
  bool advanced_pawn_push(Move move) const;
  Piece moved_piece(Move move) const;
  PieceType captured_piece_type() const;

  // Piece specific
  bool pawn_passed(Color color, Square square) const;
  bool pawn_on_7th(Color color) const;
  bool bishop_pair(Color color) const;
  bool opposite_bishops() const;

  // Doing and undoing moves
  void do_move(Move move, StateInfo& state);
  void do_move(Move move, StateInfo& state, const CheckInfo& checkInfo, bool moveIsCheck);
  void undo_move(Move move);
  void do_null_move(StateInfo& state);
  void undo_null_move();

  // Static exchange evaluation
  Value see(Move move) const;
  Value see_sign(Move move) const;

  // Accessing hash keys
  Key key() const;
  Key key_after(Move move) const;
  Key exclusion_key() const;
  Key pawn_key() const;
  Key material_key() const;

  // Incremental piece-square evaluation
  Score psq_score() const;
  Value non_pawn_material(Color color) const;

  // Other properties of the position
  Color side_to_move() const;
  Phase game_phase() const;
  int game_ply() const;
  bool is_chess960() const;
  Thread* this_thread() const;
  uint64_t nodes_searched() const;
  void set_nodes_searched(uint64_t n);
  bool is_draw() const;

  // Position consistency check, for debugging
  bool pos_is_ok(int* step = NULL) const;
  void flip();

private:
  // Initialization helpers (used while setting up a position)
  void clear();
  void set_castling_right(Color color, Square rookFrom);
  void set_state(StateInfo* state) const;

  // Helper functions
  Bitboard check_blockers(Color color, Color kingColor) const;
  void put_piece(Square square, Color color, PieceType pieceType);
  void remove_piece(Square square, Color color, PieceType pieceType);
  void move_piece(Square from, Square to, Color color, PieceType pieceType);
  template<bool Do>
  void do_castling(Square from, Square& to, Square& rookFrom, Square& rookTo);

  // Board and pieces
  Piece board[SQUARE_NB];
  Bitboard byTypeBB[PIECE_TYPE_NB];
  Bitboard byColorBB[COLOR_NB];
  int pieceCount[COLOR_NB][PIECE_TYPE_NB];
  Square pieceList[COLOR_NB][PIECE_TYPE_NB][16];
  int index[SQUARE_NB];

  // Other info
  int castlingRightsMask[SQUARE_NB];
  Square castlingRookSquare[CASTLING_RIGHT_NB];
  Bitboard castlingPath[CASTLING_RIGHT_NB];
  StateInfo startState;
  uint64_t nodes;
  int gamePly;
  Color sideToMove;
  Thread* thisThread;
  StateInfo* state;
  bool chess960;
};

inline uint64_t Position::nodes_searched() const {
  return nodes;
}

inline void Position::set_nodes_searched(uint64_t n) {
  nodes = n;
}

inline Piece Position::piece_on(Square square) const {
  return board[square];
}

inline Piece Position::moved_piece(Move move) const {
  return board[from_sq(move)];
}

inline bool Position::empty(Square square) const {
  return board[square] == NO_PIECE;
}

inline Color Position::side_to_move() const {
  return sideToMove;
}

inline Bitboard Position::pieces() const {
  return byTypeBB[ALL_PIECES];
}

inline Bitboard Position::pieces(PieceType pieceType) const {
  return byTypeBB[pieceType];
}

inline Bitboard Position::pieces(PieceType pieceType1, PieceType pieceType2) const {
  return byTypeBB[pieceType1] | byTypeBB[pieceType2];
}

inline Bitboard Position::pieces(Color color) const {
  return byColorBB[color];
}

inline Bitboard Position::pieces(Color color, PieceType pieceType) const {
  return byColorBB[color] & byTypeBB[pieceType];
}

inline Bitboard Position::pieces(Color color, PieceType pieceType1, PieceType pieceType2) const {
  return byColorBB[color] & (byTypeBB[pieceType1] | byTypeBB[pieceType2]);
}

template<PieceType Pt> inline int Position::count(Color color) const {
  return pieceCount[color][Pt];
}

template<PieceType Pt> inline const Square* Position::list(Color color) const {
  return pieceList[color][Pt];
}

inline Square Position::ep_square() const {
  return state->enpassantSquare;
}

inline Square Position::king_square(Color color) const {
  return pieceList[color][KING][0];
}

inline int Position::can_castle(CastlingRight castlingRight) const {
  return state->castlingRights & castlingRight;
}

inline int Position::can_castle(Color color) const {
  return state->castlingRights & ((WHITE_OO | WHITE_OOO) << (2 * color));
}

inline bool Position::castling_impeded(CastlingRight castlingRight) const {
  return byTypeBB[ALL_PIECES] & castlingPath[castlingRight];
}

inline Square Position::castling_rook_square(CastlingRight castlingRight) const {
  return castlingRookSquare[castlingRight];
}

template<PieceType Pt>
inline Bitboard Position::attacks_from(Square square) const {

  return  Pt == BISHOP || Pt == ROOK ? attacks_bb<Pt>(square, byTypeBB[ALL_PIECES])
        : Pt == QUEEN  ? attacks_from<ROOK>(square) | attacks_from<BISHOP>(square)
        : StepAttacksBB[Pt][square];
}

template<>
inline Bitboard Position::attacks_from<PAWN>(Square square, Color color) const {
  return StepAttacksBB[make_piece(color, PAWN)][square];
}

inline Bitboard Position::attacks_from(Piece piece, Square square) const {
  return attacks_bb(piece, square, byTypeBB[ALL_PIECES]);
}

inline Bitboard Position::attackers_to(Square square) const {
  return attackers_to(square, byTypeBB[ALL_PIECES]);
}

inline Bitboard Position::checkers() const {
  return state->checkersBB;
}

inline Bitboard Position::discovered_check_candidates() const {
  return check_blockers(sideToMove, ~sideToMove);
}

inline Bitboard Position::pinned_pieces(Color color) const {
  return check_blockers(color, color);
}

inline bool Position::pawn_passed(Color color, Square square) const {
  return !(pieces(~color, PAWN) & passed_pawn_mask(color, square));
}

inline bool Position::advanced_pawn_push(Move move) const {
  return   type_of(moved_piece(move)) == PAWN
        && relative_rank(sideToMove, from_sq(move)) > RANK_4;
}

inline Key Position::key() const {
  return state->key;
}

inline Key Position::pawn_key() const {
  return state->pawnKey;
}

inline Key Position::material_key() const {
  return state->materialKey;
}

inline Score Position::psq_score() const {
  return state->psq;
}

inline Value Position::non_pawn_material(Color color) const {
  return state->nonPawnMaterial[color];
}

inline int Position::game_ply() const {
  return gamePly;
}

inline bool Position::opposite_bishops() const {

  return   pieceCount[WHITE][BISHOP] == 1
        && pieceCount[BLACK][BISHOP] == 1
        && opposite_colors(pieceList[WHITE][BISHOP][0], pieceList[BLACK][BISHOP][0]);
}

inline bool Position::bishop_pair(Color color) const {

  return   pieceCount[color][BISHOP] >= 2
        && opposite_colors(pieceList[color][BISHOP][0], pieceList[color][BISHOP][1]);
}

inline bool Position::pawn_on_7th(Color color) const {
  return pieces(color, PAWN) & rank_bb(relative_rank(color, RANK_7));
}

inline bool Position::is_chess960() const {
  return chess960;
}

inline bool Position::capture_or_promotion(Move move) const {

  assert(is_ok(move));
  return type_of(move) != NORMAL ? type_of(move) != CASTLING : !empty(to_sq(move));
}

inline bool Position::capture(Move move) const {

  // Note that castling is encoded as "king captures the rook"
  assert(is_ok(move));
  return (!empty(to_sq(move)) && type_of(move) != CASTLING) || type_of(move) == ENPASSANT;
}

inline PieceType Position::captured_piece_type() const {
  return state->capturedType;
}

inline Thread* Position::this_thread() const {
  return thisThread;
}

inline void Position::put_piece(Square square, Color color, PieceType pieceType) {

  board[square] = make_piece(color, pieceType);
  byTypeBB[ALL_PIECES] |= square;
  byTypeBB[pieceType] |= square;
  byColorBB[color] |= square;
  index[square] = pieceCount[color][pieceType]++;
  pieceList[color][pieceType][index[square]] = square;
}

inline void Position::move_piece(Square from, Square to, Color color, PieceType pieceType) {

  // index[from] is not updated and becomes stale. This works as long
  // as index[] is accessed just by known occupied squares.
  Bitboard from_to_bb = SquareBB[from] ^ SquareBB[to];
  byTypeBB[ALL_PIECES] ^= from_to_bb;
  byTypeBB[pieceType] ^= from_to_bb;
  byColorBB[color] ^= from_to_bb;
  board[from] = NO_PIECE;
  board[to] = make_piece(color, pieceType);
  index[to] = index[from];
  pieceList[color][pieceType][index[to]] = to;
}

inline void Position::remove_piece(Square square, Color color, PieceType pieceType) {

  // WARNING: This is not a reversible operation. If we remove a piece in
  // do_move() and then replace it in undo_move() we will put it at the end of
  // the list and not in its original place, it means index[] and pieceList[]
  // are not guaranteed to be invariant to a do_move() + undo_move() sequence.
  byTypeBB[ALL_PIECES] ^= square;
  byTypeBB[pieceType] ^= square;
  byColorBB[color] ^= square;
  /* board[square] = NO_PIECE; */ // Not needed, will be overwritten by capturing
  Square lastSquare = pieceList[color][pieceType][--pieceCount[color][pieceType]];
  index[lastSquare] = index[square];
  pieceList[color][pieceType][index[lastSquare]] = lastSquare;
  pieceList[color][pieceType][pieceCount[color][pieceType]] = SQ_NONE;
}

#endif // #ifndef POSITION_H_INCLUDED
