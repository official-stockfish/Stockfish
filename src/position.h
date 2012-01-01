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

#if !defined(POSITION_H_INCLUDED)
#define POSITION_H_INCLUDED

#include <cassert>

#include "bitboard.h"
#include "types.h"


/// The checkInfo struct is initialized at c'tor time and keeps info used
/// to detect if a move gives check.
class Position;

struct CheckInfo {

  explicit CheckInfo(const Position&);

  Bitboard dcCandidates;
  Bitboard pinned;
  Bitboard checkSq[8];
};


/// The StateInfo struct stores information we need to restore a Position
/// object to its previous state when we retract a move. Whenever a move
/// is made on the board (by calling Position::do_move), an StateInfo object
/// must be passed as a parameter.

struct StateInfo {
  Key pawnKey, materialKey;
  Value npMaterial[2];
  int castleRights, rule50, pliesFromNull;
  Score value;
  Square epSquare;

  Key key;
  Bitboard checkersBB;
  PieceType capturedType;
  StateInfo* previous;
};


/// The position data structure. A position consists of the following data:
///
///    * For each piece type, a bitboard representing the squares occupied
///      by pieces of that type.
///    * For each color, a bitboard representing the squares occupied by
///      pieces of that color.
///    * A bitboard of all occupied squares.
///    * A bitboard of all checking pieces.
///    * A 64-entry array of pieces, indexed by the squares of the board.
///    * The current side to move.
///    * Information about the castling rights for both sides.
///    * The initial files of the kings and both pairs of rooks. This is
///      used to implement the Chess960 castling rules.
///    * The en passant square (which is SQ_NONE if no en passant capture is
///      possible).
///    * The squares of the kings for both sides.
///    * Hash keys for the position itself, the current pawn structure, and
///      the current material situation.
///    * Hash keys for all previous positions in the game for detecting
///      repetition draws.
///    * A counter for detecting 50 move rule draws.

class Position {

  // No copy c'tor or assignment operator allowed
  Position(const Position&);
  Position& operator=(const Position&);

public:
  Position() {}
  Position(const Position& pos, int th) { copy(pos, th); }
  Position(const std::string& fen, bool isChess960, int th);

  // Text input/output
  void copy(const Position& pos, int th);
  void from_fen(const std::string& fen, bool isChess960);
  const std::string to_fen() const;
  void print(Move m = MOVE_NONE) const;

  // The piece on a given square
  Piece piece_on(Square s) const;
  bool square_is_empty(Square s) const;

  // Side to move
  Color side_to_move() const;

  // Bitboard representation of the position
  Bitboard empty_squares() const;
  Bitboard occupied_squares() const;
  Bitboard pieces(Color c) const;
  Bitboard pieces(PieceType pt) const;
  Bitboard pieces(PieceType pt, Color c) const;
  Bitboard pieces(PieceType pt1, PieceType pt2) const;
  Bitboard pieces(PieceType pt1, PieceType pt2, Color c) const;

  // Number of pieces of each color and type
  int piece_count(Color c, PieceType pt) const;

  // The en passant square
  Square ep_square() const;

  // Current king position for each color
  Square king_square(Color c) const;

  // Castling rights
  bool can_castle(CastleRight f) const;
  bool can_castle(Color c) const;
  Square castle_rook_square(CastleRight f) const;

  // Bitboards for pinned pieces and discovered check candidates
  Bitboard discovered_check_candidates() const;
  Bitboard pinned_pieces() const;

  // Checking pieces and under check information
  Bitboard checkers() const;
  bool in_check() const;

  // Piece lists
  const Square* piece_list(Color c, PieceType pt) const;

  // Information about attacks to or from a given square
  Bitboard attackers_to(Square s) const;
  Bitboard attackers_to(Square s, Bitboard occ) const;
  Bitboard attacks_from(Piece p, Square s) const;
  static Bitboard attacks_from(Piece p, Square s, Bitboard occ);
  template<PieceType> Bitboard attacks_from(Square s) const;
  template<PieceType> Bitboard attacks_from(Square s, Color c) const;

  // Properties of moves
  bool move_gives_check(Move m, const CheckInfo& ci) const;
  bool move_attacks_square(Move m, Square s) const;
  bool pl_move_is_legal(Move m, Bitboard pinned) const;
  bool is_pseudo_legal(const Move m) const;
  bool is_capture(Move m) const;
  bool is_capture_or_promotion(Move m) const;
  bool is_passed_pawn_push(Move m) const;

  // Piece captured with previous moves
  PieceType captured_piece_type() const;

  // Information about pawns
  bool pawn_is_passed(Color c, Square s) const;

  // Doing and undoing moves
  void do_move(Move m, StateInfo& st);
  void do_move(Move m, StateInfo& st, const CheckInfo& ci, bool moveIsCheck);
  void undo_move(Move m);
  template<bool Do> void do_null_move(StateInfo& st);

  // Static exchange evaluation
  int see(Move m) const;
  int see_sign(Move m) const;

  // Accessing hash keys
  Key key() const;
  Key exclusion_key() const;
  Key pawn_key() const;
  Key material_key() const;

  // Incremental evaluation
  Score value() const;
  Value non_pawn_material(Color c) const;
  Score pst_delta(Piece piece, Square from, Square to) const;

  // Game termination checks
  bool is_mate() const;
  template<bool SkipRepetition> bool is_draw() const;

  // Plies from start position to the beginning of search
  int startpos_ply_counter() const;

  // Other properties of the position
  bool opposite_colored_bishops() const;
  bool has_pawn_on_7th(Color c) const;
  bool is_chess960() const;

  // Current thread ID searching on the position
  int thread() const;

  int64_t nodes_searched() const;
  void set_nodes_searched(int64_t n);

  // Position consistency check, for debugging
  bool pos_is_ok(int* failedStep = NULL) const;
  void flip_me();

  // Global initialization
  static void init();

private:

  // Initialization helper functions (used while setting up a position)
  void clear();
  void put_piece(Piece p, Square s);
  void set_castle_right(Square ksq, Square rsq);
  bool move_is_legal(const Move m) const;

  // Helper template functions
  template<bool Do> void do_castle_move(Move m);
  template<bool FindPinned> Bitboard hidden_checkers() const;

  // Computing hash keys from scratch (for initialization and debugging)
  Key compute_key() const;
  Key compute_pawn_key() const;
  Key compute_material_key() const;

  // Computing incremental evaluation scores and material counts
  Score pst(Piece p, Square s) const;
  Score compute_value() const;
  Value compute_non_pawn_material(Color c) const;

  // Board
  Piece board[64];             // [square]

  // Bitboards
  Bitboard byTypeBB[8];        // [pieceType]
  Bitboard byColorBB[2];       // [color]
  Bitboard occupied;

  // Piece counts
  int pieceCount[2][8];        // [color][pieceType]

  // Piece lists
  Square pieceList[2][8][16];  // [color][pieceType][index]
  int index[64];               // [square]

  // Other info
  int castleRightsMask[64];    // [square]
  Square castleRookSquare[16]; // [castleRight]
  StateInfo startState;
  int64_t nodes;
  int startPosPly;
  Color sideToMove;
  int threadID;
  StateInfo* st;
  int chess960;

  // Static variables
  static Score pieceSquareTable[16][64]; // [piece][square]
  static Key zobrist[2][8][64];          // [color][pieceType][square]/[piece count]
  static Key zobEp[64];                  // [square]
  static Key zobCastle[16];              // [castleRight]
  static Key zobSideToMove;
  static Key zobExclusion;
};

inline int64_t Position::nodes_searched() const {
  return nodes;
}

inline void Position::set_nodes_searched(int64_t n) {
  nodes = n;
}

inline Piece Position::piece_on(Square s) const {
  return board[s];
}

inline bool Position::square_is_empty(Square s) const {
  return board[s] == NO_PIECE;
}

inline Color Position::side_to_move() const {
  return sideToMove;
}

inline Bitboard Position::occupied_squares() const {
  return occupied;
}

inline Bitboard Position::empty_squares() const {
  return ~occupied;
}

inline Bitboard Position::pieces(Color c) const {
  return byColorBB[c];
}

inline Bitboard Position::pieces(PieceType pt) const {
  return byTypeBB[pt];
}

inline Bitboard Position::pieces(PieceType pt, Color c) const {
  return byTypeBB[pt] & byColorBB[c];
}

inline Bitboard Position::pieces(PieceType pt1, PieceType pt2) const {
  return byTypeBB[pt1] | byTypeBB[pt2];
}

inline Bitboard Position::pieces(PieceType pt1, PieceType pt2, Color c) const {
  return (byTypeBB[pt1] | byTypeBB[pt2]) & byColorBB[c];
}

inline int Position::piece_count(Color c, PieceType pt) const {
  return pieceCount[c][pt];
}

inline const Square* Position::piece_list(Color c, PieceType pt) const {
  return pieceList[c][pt];
}

inline Square Position::ep_square() const {
  return st->epSquare;
}

inline Square Position::king_square(Color c) const {
  return pieceList[c][KING][0];
}

inline bool Position::can_castle(CastleRight f) const {
  return st->castleRights & f;
}

inline bool Position::can_castle(Color c) const {
  return st->castleRights & ((WHITE_OO | WHITE_OOO) << c);
}

inline Square Position::castle_rook_square(CastleRight f) const {
  return castleRookSquare[f];
}

template<>
inline Bitboard Position::attacks_from<PAWN>(Square s, Color c) const {
  return StepAttacksBB[make_piece(c, PAWN)][s];
}

template<PieceType Piece> // Knight and King and white pawns
inline Bitboard Position::attacks_from(Square s) const {
  return StepAttacksBB[Piece][s];
}

template<>
inline Bitboard Position::attacks_from<BISHOP>(Square s) const {
  return bishop_attacks_bb(s, occupied_squares());
}

template<>
inline Bitboard Position::attacks_from<ROOK>(Square s) const {
  return rook_attacks_bb(s, occupied_squares());
}

template<>
inline Bitboard Position::attacks_from<QUEEN>(Square s) const {
  return attacks_from<ROOK>(s) | attacks_from<BISHOP>(s);
}

inline Bitboard Position::attacks_from(Piece p, Square s) const {
  return attacks_from(p, s, occupied_squares());
}

inline Bitboard Position::attackers_to(Square s) const {
  return attackers_to(s, occupied_squares());
}

inline Bitboard Position::checkers() const {
  return st->checkersBB;
}

inline bool Position::in_check() const {
  return st->checkersBB != 0;
}

inline Bitboard Position::discovered_check_candidates() const {
  return hidden_checkers<false>();
}

inline Bitboard Position::pinned_pieces() const {
  return hidden_checkers<true>();
}

inline bool Position::pawn_is_passed(Color c, Square s) const {
  return !(pieces(PAWN, flip(c)) & passed_pawn_mask(c, s));
}

inline Key Position::key() const {
  return st->key;
}

inline Key Position::exclusion_key() const {
  return st->key ^ zobExclusion;
}

inline Key Position::pawn_key() const {
  return st->pawnKey;
}

inline Key Position::material_key() const {
  return st->materialKey;
}

inline Score Position::pst(Piece p, Square s) const {
  return pieceSquareTable[p][s];
}

inline Score Position::pst_delta(Piece piece, Square from, Square to) const {
  return pieceSquareTable[piece][to] - pieceSquareTable[piece][from];
}

inline Score Position::value() const {
  return st->value;
}

inline Value Position::non_pawn_material(Color c) const {
  return st->npMaterial[c];
}

inline bool Position::is_passed_pawn_push(Move m) const {

  return   board[from_sq(m)] == make_piece(sideToMove, PAWN)
        && pawn_is_passed(sideToMove, to_sq(m));
}

inline int Position::startpos_ply_counter() const {
  return startPosPly + st->pliesFromNull; // HACK
}

inline bool Position::opposite_colored_bishops() const {

  return   pieceCount[WHITE][BISHOP] == 1
        && pieceCount[BLACK][BISHOP] == 1
        && opposite_colors(pieceList[WHITE][BISHOP][0], pieceList[BLACK][BISHOP][0]);
}

inline bool Position::has_pawn_on_7th(Color c) const {
  return pieces(PAWN, c) & rank_bb(relative_rank(c, RANK_7));
}

inline bool Position::is_chess960() const {
  return chess960;
}

inline bool Position::is_capture_or_promotion(Move m) const {

  assert(is_ok(m));
  return is_special(m) ? !is_castle(m) : !square_is_empty(to_sq(m));
}

inline bool Position::is_capture(Move m) const {

  // Note that castle is coded as "king captures the rook"
  assert(is_ok(m));
  return (!square_is_empty(to_sq(m)) && !is_castle(m)) || is_enpassant(m);
}

inline PieceType Position::captured_piece_type() const {
  return st->capturedType;
}

inline int Position::thread() const {
  return threadID;
}

#endif // !defined(POSITION_H_INCLUDED)
