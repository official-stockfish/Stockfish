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

#if !defined(POSITION_H_INCLUDED)
#define POSITION_H_INCLUDED

#include "bitboard.h"
#include "move.h"
#include "types.h"

/// Maximum number of plies per game (220 should be enough, because the
/// maximum search depth is 100, and during position setup we reset the
/// move counter for every non-reversible move).
const int MaxGameLength = 220;

class Position;

/// struct checkInfo is initialized at c'tor time and keeps
/// info used to detect if a move gives check.

struct CheckInfo {

    explicit CheckInfo(const Position&);

    Bitboard dcCandidates;
    Bitboard checkSq[8];
    Square ksq;
};

/// Castle rights, encoded as bit fields

enum CastleRights {
  CASTLES_NONE = 0,
  WHITE_OO     = 1,
  BLACK_OO     = 2,
  WHITE_OOO    = 4,
  BLACK_OOO    = 8,
  ALL_CASTLES  = 15
};

/// Game phase
enum Phase {
  PHASE_ENDGAME = 0,
  PHASE_MIDGAME = 128
};


/// The StateInfo struct stores information we need to restore a Position
/// object to its previous state when we retract a move. Whenever a move
/// is made on the board (by calling Position::do_move), an StateInfo object
/// must be passed as a parameter.

struct StateInfo {
  Key pawnKey, materialKey;
  int castleRights, rule50, gamePly, pliesFromNull;
  Square epSquare;
  Score value;
  Value npMaterial[2];

  PieceType capturedType;
  Key key;
  Bitboard checkersBB;
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

  Position(); // No default or copy c'tor allowed
  Position(const Position& pos);

public:
  enum GamePhase {
      MidGame,
      EndGame
  };

  // Constructors
  Position(const Position& pos, int threadID);
  Position(const std::string& fen, bool isChess960, int threadID);

  // Text input/output
  void from_fen(const std::string& fen, bool isChess960);
  const std::string to_fen() const;
  void print(Move m = MOVE_NONE) const;

  // Copying
  void flip();

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
  bool can_castle_kingside(Color c) const;
  bool can_castle_queenside(Color c) const;
  bool can_castle(Color c) const;
  Square initial_kr_square(Color c) const;
  Square initial_qr_square(Color c) const;

  // Bitboards for pinned pieces and discovered check candidates
  Bitboard discovered_check_candidates(Color c) const;
  Bitboard pinned_pieces(Color c) const;

  // Checking pieces and under check information
  Bitboard checkers() const;
  bool in_check() const;

  // Piece lists
  Square piece_list(Color c, PieceType pt, int index) const;
  const Square* piece_list_begin(Color c, PieceType pt) const;

  // Information about attacks to or from a given square
  Bitboard attackers_to(Square s) const;
  Bitboard attacks_from(Piece p, Square s) const;
  static Bitboard attacks_from(Piece p, Square s, Bitboard occ);
  template<PieceType> Bitboard attacks_from(Square s) const;
  template<PieceType> Bitboard attacks_from(Square s, Color c) const;

  // Properties of moves
  bool pl_move_is_legal(Move m, Bitboard pinned) const;
  bool pl_move_is_evasion(Move m, Bitboard pinned) const;
  bool move_is_legal(const Move m) const;
  bool move_is_legal(const Move m, Bitboard pinned) const;
  bool move_gives_check(Move m) const;
  bool move_gives_check(Move m, const CheckInfo& ci) const;
  bool move_is_capture(Move m) const;
  bool move_is_capture_or_promotion(Move m) const;
  bool move_is_passed_pawn_push(Move m) const;
  bool move_attacks_square(Move m, Square s) const;

  // Piece captured with previous moves
  PieceType captured_piece_type() const;

  // Information about pawns
  bool pawn_is_passed(Color c, Square s) const;

  // Weak squares
  bool square_is_weak(Square s, Color c) const;

  // Doing and undoing moves
  void do_setup_move(Move m);
  void do_move(Move m, StateInfo& st);
  void do_move(Move m, StateInfo& st, const CheckInfo& ci, bool moveIsCheck);
  void undo_move(Move m);
  void do_null_move(StateInfo& st);
  void undo_null_move();

  // Static exchange evaluation
  int see(Square from, Square to) const;
  int see(Move m) const;
  int see_sign(Move m) const;

  // Accessing hash keys
  Key get_key() const;
  Key get_exclusion_key() const;
  Key get_pawn_key() const;
  Key get_material_key() const;

  // Incremental evaluation
  Score value() const;
  Value non_pawn_material(Color c) const;
  static Score pst_delta(Piece piece, Square from, Square to);

  // Game termination checks
  bool is_mate() const;
  bool is_draw() const;

  // Number of plies from starting position
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
  bool is_ok(int* failedStep = NULL) const;

  // Static member functions
  static void init_zobrist();
  static void init_piece_square_tables();

private:

  // Initialization helper functions (used while setting up a position)
  void clear();
  void detach();
  void put_piece(Piece p, Square s);
  void do_allow_oo(Color c);
  void do_allow_ooo(Color c);
  bool set_castling_rights(char token);

  // Helper functions for doing and undoing moves
  void do_capture_move(Key& key, PieceType capture, Color them, Square to, bool ep);
  void do_castle_move(Move m);
  void undo_castle_move(Move m);
  void find_checkers();

  template<bool FindPinned>
  Bitboard hidden_checkers(Color c) const;

  // Computing hash keys from scratch (for initialization and debugging)
  Key compute_key() const;
  Key compute_pawn_key() const;
  Key compute_material_key() const;

  // Computing incremental evaluation scores and material counts
  static Score pst(Color c, PieceType pt, Square s);
  Score compute_value() const;
  Value compute_non_pawn_material(Color c) const;

  // Board
  Piece board[64];

  // Bitboards
  Bitboard byTypeBB[8], byColorBB[2];

  // Piece counts
  int pieceCount[2][8]; // [color][pieceType]

  // Piece lists
  Square pieceList[2][8][16]; // [color][pieceType][index]
  int index[64]; // [square]

  // Other info
  Color sideToMove;
  Key history[MaxGameLength];
  int castleRightsMask[64];
  StateInfo startState;
  File initialKFile, initialKRFile, initialQRFile;
  bool chess960;
  int startPosPlyCounter;
  int threadID;
  int64_t nodes;
  StateInfo* st;

  // Static variables
  static Key zobrist[2][8][64];
  static Key zobEp[64];
  static Key zobCastle[16];
  static Key zobSideToMove;
  static Score PieceSquareTable[16][64];
  static Key zobExclusion;
  static const Value seeValues[8];
  static const Value PieceValueMidgame[17];
  static const Value PieceValueEndgame[17];
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

inline Color Position::color_of_piece_on(Square s) const {
  return color_of_piece(piece_on(s));
}

inline PieceType Position::type_of_piece_on(Square s) const {
  return type_of_piece(piece_on(s));
}

inline bool Position::square_is_empty(Square s) const {
  return piece_on(s) == PIECE_NONE;
}

inline bool Position::square_is_occupied(Square s) const {
  return !square_is_empty(s);
}

inline Value Position::midgame_value_of_piece_on(Square s) const {
  return PieceValueMidgame[piece_on(s)];
}

inline Value Position::endgame_value_of_piece_on(Square s) const {
  return PieceValueEndgame[piece_on(s)];
}

inline Color Position::side_to_move() const {
  return sideToMove;
}

inline Bitboard Position::occupied_squares() const {
  return byTypeBB[0];
}

inline Bitboard Position::empty_squares() const {
  return ~occupied_squares();
}

inline Bitboard Position::pieces_of_color(Color c) const {
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

inline Square Position::piece_list(Color c, PieceType pt, int idx) const {
  return pieceList[c][pt][idx];
}

inline const Square* Position::piece_list_begin(Color c, PieceType pt) const {
  return pieceList[c][pt];
}

inline Square Position::ep_square() const {
  return st->epSquare;
}

inline Square Position::king_square(Color c) const {
  return pieceList[c][KING][0];
}

inline bool Position::can_castle_kingside(Color side) const {
  return st->castleRights & (1+int(side));
}

inline bool Position::can_castle_queenside(Color side) const {
  return st->castleRights & (4+4*int(side));
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

inline Bitboard Position::checkers() const {
  return st->checkersBB;
}

inline bool Position::in_check() const {
  return st->checkersBB != EmptyBoardBB;
}

inline bool Position::pawn_is_passed(Color c, Square s) const {
  return !(pieces(PAWN, opposite_color(c)) & passed_pawn_mask(c, s));
}

inline bool Position::square_is_weak(Square s, Color c) const {
  return !(pieces(PAWN, opposite_color(c)) & attack_span_mask(c, s));
}

inline Key Position::get_key() const {
  return st->key;
}

inline Key Position::get_exclusion_key() const {
  return st->key ^ zobExclusion;
}

inline Key Position::get_pawn_key() const {
  return st->pawnKey;
}

inline Key Position::get_material_key() const {
  return st->materialKey;
}

inline Score Position::pst(Color c, PieceType pt, Square s) {
  return PieceSquareTable[make_piece(c, pt)][s];
}

inline Score Position::pst_delta(Piece piece, Square from, Square to) {
  return PieceSquareTable[piece][to] - PieceSquareTable[piece][from];
}

inline Score Position::value() const {
  return st->value;
}

inline Value Position::non_pawn_material(Color c) const {
  return st->npMaterial[c];
}

inline bool Position::move_is_passed_pawn_push(Move m) const {

  Color c = side_to_move();
  return   piece_on(move_from(m)) == make_piece(c, PAWN)
        && pawn_is_passed(c, move_to(m));
}

inline int Position::startpos_ply_counter() const {
  return startPosPlyCounter;
}

inline bool Position::opposite_colored_bishops() const {

  return   piece_count(WHITE, BISHOP) == 1 && piece_count(BLACK, BISHOP) == 1
        && opposite_color_squares(piece_list(WHITE, BISHOP, 0), piece_list(BLACK, BISHOP, 0));
}

inline bool Position::has_pawn_on_7th(Color c) const {
  return pieces(PAWN, c) & rank_bb(relative_rank(c, RANK_7));
}

inline bool Position::is_chess960() const {
  return chess960;
}

inline bool Position::move_is_capture(Move m) const {

  // Move must not be MOVE_NONE !
  return (m & (3 << 15)) ? !move_is_castle(m) : !square_is_empty(move_to(m));
}

inline bool Position::move_is_capture_or_promotion(Move m) const {

  // Move must not be MOVE_NONE !
  return (m & (0x1F << 12)) ? !move_is_castle(m) : !square_is_empty(move_to(m));
}

inline PieceType Position::captured_piece_type() const {
  return st->capturedType;
}

inline int Position::thread() const {
  return threadID;
}

inline void Position::do_allow_oo(Color c) {
  st->castleRights |= (1 + int(c));
}

inline void Position::do_allow_ooo(Color c) {
  st->castleRights |= (4 + 4*int(c));
}

#endif // !defined(POSITION_H_INCLUDED)
