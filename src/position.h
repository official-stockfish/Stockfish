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


#if !defined(POSITION_H_INCLUDED)
#define POSITION_H_INCLUDED

////
//// Includes
////

#include "bitboard.h"
#include "color.h"
#include "direction.h"
#include "move.h"
#include "piece.h"
#include "phase.h"
#include "square.h"
#include "value.h"


////
//// Constants
////

/// FEN string for the initial position:
const std::string StartPosition = 
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

/// Maximum number of plies per game (220 should be enough, because the
/// maximum search depth is 100, and during position setup we reset the
/// move counter for every non-reversible move):
const int MaxGameLength = 220;


////
//// Types
////

/// Castle rights, encoded as bit fields:

enum CastleRights {
  NO_CASTLES = 0, 
  WHITE_OO = 1, BLACK_OO = 2, 
  WHITE_OOO = 4, BLACK_OOO = 8,
  ALL_CASTLES = 15
};


/// The UndoInfo struct stores information we need to restore a Position
/// object to its previous state when we retract a move.  Whenever a move
/// is made on the board (by calling Position::do_move), an UndoInfo object
/// must be passed as a parameter.  When the move is unmade (by calling
/// Position::undo_move), the same UndoInfo object must be passed again.

struct UndoInfo {
  int castleRights;
  Square epSquare;
  Bitboard checkersBB;
  Key key, pawnKey, materialKey;
  int rule50;
  Move lastMove;
  PieceType capture;
  Value mgValue, egValue;
};


/// The position data structure.  A position consists of the following data:
///    
///    * For each piece type, a bitboard representing the squares occupied
///      by pieces of that type.
///    * For each color, a bitboard representing the squares occupiecd by
///      pieces of that color.
///    * A bitboard of all occupied squares.
///    * A bitboard of all checking pieces.
///    * A 64-entry array of pieces, indexed by the squares of the board.
///    * The current side to move.
///    * Information about the castling rights for both sides.
///    * The initial files of the kings and both pairs of rooks.  This is
///      used to implement the Chess960 castling rules.
///    * The en passant square (which is SQ_NONE if no en passant capture is
///      possible).
///    * The squares of the kings for both sides.
///    * The last move played.
///    * Hash keys for the position itself, the current pawn structure, and
///      the current material situation.
///    * Hash keys for all previous positions in the game (for detecting
///      repetition draws.
///    * A counter for detecting 50 move rule draws.

class Position {

  friend class MaterialInfo;

public:
  // Constructors
  Position();
  Position(const Position &pos);
  Position(const std::string &fen);

  // Text input/output
  void from_fen(const std::string &fen);
  const std::string to_fen() const;
  void print() const;

  // Copying
  void copy(const Position &pos);
  void flipped_copy(const Position &pos);

  // The piece on a given square
  Piece piece_on(Square s) const;
  PieceType type_of_piece_on(Square s) const;
  Color color_of_piece_on(Square s) const;
  bool square_is_empty(Square s) const;
  bool square_is_occupied(Square s) const;
  Value midgame_value_of_piece_on(Square s) const;
  Value endgame_value_of_piece_on(Square s) const;

  // Side to move
  Color side_to_move() const;

  // Bitboard representation of the position
  Bitboard empty_squares() const;
  Bitboard occupied_squares() const;
  Bitboard pieces_of_color(Color c) const;
  Bitboard pieces_of_type(PieceType pt) const;
  Bitboard pieces_of_color_and_type(Color c, PieceType pt) const;
  Bitboard pawns() const;
  Bitboard knights() const;
  Bitboard bishops() const;
  Bitboard rooks() const;
  Bitboard queens() const;
  Bitboard kings() const;
  Bitboard rooks_and_queens() const;
  Bitboard bishops_and_queens() const;
  Bitboard sliders() const;
  Bitboard pawns(Color c) const;
  Bitboard knights(Color c) const;
  Bitboard bishops(Color c) const;
  Bitboard rooks(Color c) const;
  Bitboard queens(Color c) const;
  Bitboard kings(Color c) const;
  Bitboard rooks_and_queens(Color c) const;
  Bitboard bishops_and_queens(Color c) const;
  Bitboard sliders_of_color(Color c) const;

  // Number of pieces of each color and type
  int piece_count(Color c, PieceType pt) const;
  int pawn_count(Color c) const;
  int knight_count(Color c) const;
  int bishop_count(Color c) const;
  int rook_count(Color c) const;
  int queen_count(Color c) const;

  // The en passant square:
  Square ep_square() const;

  // Current king position for each color
  Square king_square(Color c) const;

  // Castling rights.
  bool can_castle_kingside(Color c) const;
  bool can_castle_queenside(Color c) const;
  bool can_castle(Color c) const;
  Square initial_kr_square(Color c) const;
  Square initial_qr_square(Color c) const;

  // Attack bitboards
  Bitboard sliding_attacks(Square s, Direction d) const;
  Bitboard ray_attacks(Square s, SignedDirection d) const;
  Bitboard pawn_attacks(Color c, Square s) const;
  Bitboard white_pawn_attacks(Square s) const;
  Bitboard black_pawn_attacks(Square s) const;
  Bitboard knight_attacks(Square s) const;
  Bitboard bishop_attacks(Square s) const;
  Bitboard rook_attacks(Square s) const;
  Bitboard queen_attacks(Square s) const;
  Bitboard king_attacks(Square s) const;

  // Bitboards for pinned pieces and discovered check candidates
  Bitboard discovered_check_candidates(Color c) const;
  Bitboard pinned_pieces(Color c) const;

  // Checking pieces
  Bitboard checkers() const;

  // Piece lists:
  Square piece_list(Color c, PieceType pt, int index) const;
  Square pawn_list(Color c, int index) const;
  Square knight_list(Color c, int index) const;
  Square bishop_list(Color c, int index) const;
  Square rook_list(Color c, int index) const;
  Square queen_list(Color c, int index) const;

  // Attack information for a given square
  bool square_is_attacked(Square s, Color c) const;
  Bitboard attacks_to(Square s) const;
  Bitboard attacks_to(Square s, Color c) const;
  bool is_check() const;
  bool piece_attacks_square(Square f, Square t) const;
  bool white_pawn_attacks_square(Square f, Square t) const;
  bool black_pawn_attacks_square(Square f, Square t) const;
  bool knight_attacks_square(Square f, Square t) const;
  bool bishop_attacks_square(Square f, Square t) const;
  bool rook_attacks_square(Square f, Square t) const;
  bool queen_attacks_square(Square f, Square t) const;
  bool king_attacks_square(Square f, Square t) const;

  // Properties of moves
  bool move_is_legal(Move m) const;
  bool move_is_legal(Move m, Bitboard pinned) const;
  bool move_is_check(Move m) const;
  bool move_is_check(Move m, Bitboard dcCandidates) const;
  bool move_is_capture(Move m) const;
  bool move_is_pawn_push_to_7th(Move m) const;
  bool move_is_passed_pawn_push(Move m) const;
  bool move_was_passed_pawn_push(Move m) const;
  bool move_attacks_square(Move m, Square s) const;

  // Information about pawns
  bool pawn_is_passed(Color c, Square s) const;
  bool pawn_is_isolated(Color c, Square s) const;
  bool pawn_is_doubled(Color c, Square s) const;

  // Open and half-open files
  bool file_is_open(File f) const;
  bool file_is_half_open(Color c, File f) const;

  // Weak squares
  bool square_is_weak(Square s, Color c) const;

  // Doing and undoing moves
  void backup(UndoInfo &u) const;
  void restore(const UndoInfo &u);
  void do_move(Move m, UndoInfo &u);
  void do_move(Move m, UndoInfo &u, Bitboard dcCandidates);
  void undo_move(Move m, const UndoInfo &u);
  void do_null_move(UndoInfo &u);
  void undo_null_move(const UndoInfo &u);

  // Static exchange evaluation
  int see(Square from, Square to) const;
  int see(Move m) const;

  // Accessing hash keys
  Key get_key() const;
  Key get_pawn_key() const;
  Key get_material_key() const;

  // Incremental evaluation
  Value mg_value() const;
  Value eg_value() const;
  Value non_pawn_material(Color c) const;
  Phase game_phase() const;

  // Game termination checks
  bool is_mate();
  bool is_draw() const;

  // Check if one side threatens a mate in one
  bool has_mate_threat(Color c);

  // Number of plies since the last non-reversible move
  int rule_50_counter() const;

  // Other properties of the position
  bool opposite_colored_bishops() const;
  bool has_pawn_on_7th(Color c) const;

  // Reset the gamePly variable to 0
  void reset_game_ply();
  
  // Position consistency check, for debugging
  bool is_ok() const;

  // Static member functions:
  static void init_zobrist();
  static void init_piece_square_tables();

private:
  // Initialization helper functions (used while setting up a position)
  void clear();
  void put_piece(Piece p, Square s);
  void allow_oo(Color c);
  void allow_ooo(Color c);

  // Helper functions for doing and undoing moves
  void do_castle_move(Move m);
  void do_promotion_move(Move m, UndoInfo &u);
  void do_ep_move(Move m);
  void undo_castle_move(Move m);
  void undo_promotion_move(Move m, const UndoInfo &u);
  void undo_ep_move(Move m);
  void find_checkers();

  // Computing hash keys from scratch (for initialization and debugging)
  Key compute_key() const;
  Key compute_pawn_key() const;
  Key compute_material_key() const;

  // Computing incremental evaluation scores and material counts
  Value mg_pst(Color c, PieceType pt, Square s) const;
  Value eg_pst(Color c, PieceType pt, Square s) const;
  Value compute_mg_value() const;
  Value compute_eg_value() const;
  Value compute_non_pawn_material(Color c) const;

  // Bitboards
  Bitboard byColorBB[2], byTypeBB[8];
  Bitboard checkersBB;

  // Board
  Piece board[64];

  // Piece counts
  int pieceCount[2][8]; // [color][pieceType]
  
  // Piece lists
  Square pieceList[2][8][16]; // [color][pieceType][index]
  int index[64];

  // Other info
  Color sideToMove;
  int castleRights;
  File initialKFile, initialKRFile, initialQRFile;
  Square epSquare;
  Square kingSquare[2];
  Move lastMove;
  Key key, pawnKey, materialKey, history[MaxGameLength];
  int rule50, gamePly;
  Value mgValue, egValue;
  Value npMaterial[2];

  // Static variables
  static int castleRightsMask[64];
  static Key zobrist[2][8][64];
  static Key zobEp[64];
  static Key zobCastle[16];
  static Key zobMaterial[2][8][16];
  static Key zobSideToMove;
  static Value MgPieceSquareTable[16][64];
  static Value EgPieceSquareTable[16][64];
};


////
//// Inline functions
////

inline Piece Position::piece_on(Square s) const {
  return board[s];
}

inline Color Position::color_of_piece_on(Square s) const {
  return color_of_piece(this->piece_on(s));
}

inline PieceType Position::type_of_piece_on(Square s) const {
  return type_of_piece(this->piece_on(s));
}

inline bool Position::square_is_empty(Square s) const {
  return this->piece_on(s) == EMPTY;
}

inline bool Position::square_is_occupied(Square s) const {
  return !this->square_is_empty(s);
}

inline Value Position::midgame_value_of_piece_on(Square s) const {
  return piece_value_midgame(this->piece_on(s));
}

inline Value Position::endgame_value_of_piece_on(Square s) const {
  return piece_value_endgame(this->piece_on(s));
}

inline Color Position::side_to_move() const {
  return sideToMove;
}

inline Bitboard Position::occupied_squares() const {
  return byTypeBB[0];
}

inline Bitboard Position::empty_squares() const {
  return ~(this->occupied_squares());
}

inline Bitboard Position::pieces_of_color(Color c) const {
  return byColorBB[c];
}

inline Bitboard Position::pieces_of_type(PieceType pt) const {
  return byTypeBB[pt];
}

inline Bitboard Position::pieces_of_color_and_type(Color c, PieceType pt)
  const {
  return this->pieces_of_color(c) & this->pieces_of_type(pt);
}

inline Bitboard Position::pawns() const {
  return this->pieces_of_type(PAWN);
}

inline Bitboard Position::knights() const {
  return this->pieces_of_type(KNIGHT);
}

inline Bitboard Position::bishops() const {
  return this->pieces_of_type(BISHOP);
}

inline Bitboard Position::rooks() const {
  return this->pieces_of_type(ROOK);
}

inline Bitboard Position::queens() const {
  return this->pieces_of_type(QUEEN);
}

inline Bitboard Position::kings() const {
  return this->pieces_of_type(KING);
}

inline Bitboard Position::rooks_and_queens() const {
  return this->rooks() | this->queens();
}

inline Bitboard Position::bishops_and_queens() const {
  return this->bishops() | this->queens();
}

inline Bitboard Position::sliders() const {
  return this->bishops() | this->queens() | this->rooks();
}

inline Bitboard Position::pawns(Color c) const {
  return this->pieces_of_color_and_type(c, PAWN);
}

inline Bitboard Position::knights(Color c) const {
  return this->pieces_of_color_and_type(c, KNIGHT);
}

inline Bitboard Position::bishops(Color c) const {
  return this->pieces_of_color_and_type(c, BISHOP);
}

inline Bitboard Position::rooks(Color c) const {
  return this->pieces_of_color_and_type(c, ROOK);
}

inline Bitboard Position::queens(Color c) const {
  return this->pieces_of_color_and_type(c, QUEEN);
}

inline Bitboard Position::kings(Color c) const {
  return this->pieces_of_color_and_type(c, KING);
}

inline Bitboard Position::rooks_and_queens(Color c) const {
  return this->rooks_and_queens() & this->pieces_of_color(c);
}

inline Bitboard Position::bishops_and_queens(Color c) const {
  return this->bishops_and_queens() & this->pieces_of_color(c);
}

inline Bitboard Position::sliders_of_color(Color c) const {
  return this->sliders() & this->pieces_of_color(c);
}

inline int Position::piece_count(Color c, PieceType pt) const {
  return pieceCount[c][pt];
}

inline int Position::pawn_count(Color c) const {
  return this->piece_count(c, PAWN);
}

inline int Position::knight_count(Color c) const {
  return this->piece_count(c, KNIGHT);
}

inline int Position::bishop_count(Color c) const {
  return this->piece_count(c, BISHOP);
}

inline int Position::rook_count(Color c) const {
  return this->piece_count(c, ROOK);
}

inline int Position::queen_count(Color c) const {
  return this->piece_count(c, QUEEN);
}

inline Square Position::piece_list(Color c, PieceType pt, int index) const {
  return pieceList[c][pt][index];
}

inline Square Position::pawn_list(Color c, int index) const {
  return this->piece_list(c, PAWN, index);
}

inline Square Position::knight_list(Color c, int index) const {
  return this->piece_list(c, KNIGHT, index);
}

inline Square Position::bishop_list(Color c, int index) const {
  return this->piece_list(c, BISHOP, index);
}

inline Square Position::rook_list(Color c, int index) const {
  return this->piece_list(c, ROOK, index);
}

inline Square Position::queen_list(Color c, int index) const {
  return this->piece_list(c, QUEEN, index);
}

inline Square Position::ep_square() const {
  return epSquare;
}

inline Square Position::king_square(Color c) const {
  return kingSquare[c];
}

inline bool Position::can_castle_kingside(Color side) const {
  return castleRights & (1+int(side));
}

inline bool Position::can_castle_queenside(Color side) const {
  return castleRights & (4+4*int(side));
}

inline bool Position::can_castle(Color side) const {
  return can_castle_kingside(side) || can_castle_queenside(side);
}

inline Square Position::initial_kr_square(Color c) const {
  return relative_square(c, make_square(initialKRFile, RANK_1));
}

inline Square Position::initial_qr_square(Color c) const {
  return relative_square(c, make_square(initialQRFile, RANK_1));
}

inline Bitboard Position::pawn_attacks(Color c, Square s) const {
  return StepAttackBB[pawn_of_color(c)][s];
}

inline Bitboard Position::white_pawn_attacks(Square s) const {
  return this->pawn_attacks(WHITE, s);
}

inline Bitboard Position::black_pawn_attacks(Square s) const {
  return this->pawn_attacks(BLACK, s);
}

inline Bitboard Position::knight_attacks(Square s) const {
  return StepAttackBB[KNIGHT][s];
}

inline Bitboard Position::rook_attacks(Square s) const {
  return rook_attacks_bb(s, this->occupied_squares());
}

inline Bitboard Position::bishop_attacks(Square s) const {
  return bishop_attacks_bb(s, this->occupied_squares());
}

inline Bitboard Position::queen_attacks(Square s) const {
  return this->rook_attacks(s) | this->bishop_attacks(s);
}

inline Bitboard Position::king_attacks(Square s) const {
  return StepAttackBB[KING][s];
}

inline Bitboard Position::checkers() const {
  return checkersBB;
}

inline bool Position::is_check() const {
  return this->checkers() != EmptyBoardBB;
}

inline bool Position::white_pawn_attacks_square(Square f, Square t) const {
  return bit_is_set(this->white_pawn_attacks(f), t);
}

inline bool Position::black_pawn_attacks_square(Square f, Square t) const {
  return bit_is_set(this->black_pawn_attacks(f), t);
}

inline bool Position::knight_attacks_square(Square f, Square t) const {
  return bit_is_set(this->knight_attacks(f), t);
}

inline bool Position::bishop_attacks_square(Square f, Square t) const {
  return bit_is_set(this->bishop_attacks(f), t);
}

inline bool Position::rook_attacks_square(Square f, Square t) const {
  return bit_is_set(this->rook_attacks(f), t);
}

inline bool Position::queen_attacks_square(Square f, Square t) const {
  return bit_is_set(this->queen_attacks(f), t);
}

inline bool Position::king_attacks_square(Square f, Square t) const {
  return bit_is_set(this->king_attacks(f), t);
}

inline bool Position::pawn_is_passed(Color c, Square s) const {
  return !(this->pawns(opposite_color(c)) & passed_pawn_mask(c, s));
}

inline bool Position::pawn_is_isolated(Color c, Square s) const {
  return !(this->pawns(c) & neighboring_files_bb(s));
}

inline bool Position::pawn_is_doubled(Color c, Square s) const {
  return this->pawns(c) & squares_behind(c, s);
}

inline bool Position::file_is_open(File f) const {
  return !(this->pawns() & file_bb(f));
}

inline bool Position::file_is_half_open(Color c, File f) const {
  return !(this->pawns(c) & file_bb(f));
}

inline bool Position::square_is_weak(Square s, Color c) const {
  return !(this->pawns(c) & outpost_mask(opposite_color(c), s));
}
                                
inline Key Position::get_key() const {
  return key;
}

inline Key Position::get_pawn_key() const {
  return pawnKey;
}

inline Key Position::get_material_key() const {
  return materialKey;
}

inline Value Position::mg_pst(Color c, PieceType pt, Square s) const {
  return MgPieceSquareTable[piece_of_color_and_type(c, pt)][s];
}

inline Value Position::eg_pst(Color c, PieceType pt, Square s) const {
  return EgPieceSquareTable[piece_of_color_and_type(c, pt)][s];
}

inline Value Position::mg_value() const {
  return mgValue;
}

inline Value Position::eg_value() const {
  return egValue;
}

inline Value Position::non_pawn_material(Color c) const {
  return npMaterial[c];
}

inline Phase Position::game_phase() const {

  // The purpose of the Value(325) terms below is to make sure the difference
  // between MidgameLimit and EndgameLimit is a power of 2, which should make
  // the division at the end of the function a bit faster.

  static const Value MidgameLimit =
    2*QueenValueMidgame+2*RookValueMidgame+6*BishopValueMidgame+Value(325);
  static const Value EndgameLimit = 4*RookValueMidgame-Value(325);
  Value npm = this->non_pawn_material(WHITE) + this->non_pawn_material(BLACK);
  
  if(npm >= MidgameLimit)
    return PHASE_MIDGAME;
  else if(npm <= EndgameLimit)
    return PHASE_ENDGAME;
  else
    return Phase(((npm - EndgameLimit) * 128) / (MidgameLimit - EndgameLimit));
}

inline bool Position::move_is_pawn_push_to_7th(Move m) const {
  Color c = this->side_to_move();
  return 
    this->piece_on(move_from(m)) == pawn_of_color(c) &&
    pawn_rank(c, move_to(m)) == RANK_7;
}

inline bool Position::move_is_passed_pawn_push(Move m) const {
  Color c = this->side_to_move();
  return 
    this->piece_on(move_from(m)) == pawn_of_color(c) &&
    this->pawn_is_passed(c, move_to(m));
}
 
inline bool Position::move_was_passed_pawn_push(Move m) const {
  Color c = opposite_color(this->side_to_move());
  return 
    this->piece_on(move_to(m)) == pawn_of_color(c) &&
    this->pawn_is_passed(c, move_to(m));
}

inline int Position::rule_50_counter() const {
  return rule50;
}

inline bool Position::opposite_colored_bishops() const {
  return
    this->bishop_count(WHITE) == 1 && this->bishop_count(BLACK) == 1 &&
    square_color(this->bishop_list(WHITE, 0)) !=
    square_color(this->bishop_list(BLACK, 0));
}

inline bool Position::has_pawn_on_7th(Color c) const {
  return this->pawns(c) & relative_rank_bb(c, RANK_7);
}
                        

#endif // !defined(POSITION_H_INCLUDED)
