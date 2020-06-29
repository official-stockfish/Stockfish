/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef EVALUATE_H_INCLUDED
#define EVALUATE_H_INCLUDED

#include <string>

#include "types.h"

class Position;

namespace Eval {

std::string trace(const Position& pos);

Value evaluate(const Position& pos);

void evaluate_with_no_return(const Position& pos);

Value compute_eval(const Position& pos);

#if defined(EVAL_NNUE) || defined(EVAL_LEARN)
// Read the evaluation function file.
// This is only called once in response to the "is_ready" command. It is not supposed to be called twice.
// (However, if isready is sent again after EvalDir (evaluation function folder) has been changed, read it again.)
void load_eval();

static uint64_t calc_check_sum() {return 0;}

static void print_softname(uint64_t check_sum) {}

// --- enum corresponding to P of constant KPP (ball and arbitrary 2 pieces) used in evaluation function

// (BonaPiece wants to define freely in experiment of evaluation function, so I don't define it here.)


// A type that represents P(Piece) when calling KKP/KPP in Bonanza.
// When you ask for É∞ KPP, you need a unique number for each box Å~ piece type, like the step at 39 points.
enum BonaPiece : int32_t
{
	// Meaning of f = friend (Å‡first move). Meaning of e = enemy (Å‡rear)

	// Value when uninitialized
	BONA_PIECE_NOT_INIT = -1,

	// Invalid piece. When you drop a piece, move unnecessary pieces here.
	BONA_PIECE_ZERO = 0,

	fe_hand_end = BONA_PIECE_ZERO + 1,

	// Don't pack the numbers of unrealistic walks and incense on the board like Bonanza.
	// Reason 1) When learning, there are times when the incense is on the first stage in relative PP, and it is difficult to display it correctly in the inverse transformation.
	// Reason 2) It is difficult to convert from Square with vertical Bitboard.

	// --- Pieces on the board
	f_pawn = fe_hand_end,
	e_pawn = f_pawn + SQUARE_NB,
	f_knight = e_pawn + SQUARE_NB,
	e_knight = f_knight + SQUARE_NB,
	f_bishop = e_knight + SQUARE_NB,
	e_bishop = f_bishop + SQUARE_NB,
	f_rook = e_bishop + SQUARE_NB,
	e_rook = f_rook + SQUARE_NB,
	f_queen = e_rook + SQUARE_NB,
	e_queen = f_queen + SQUARE_NB,
	fe_end = e_queen + SQUARE_NB,
	f_king = fe_end,
	e_king = f_king + SQUARE_NB,
	fe_end2 = e_king + SQUARE_NB, // Last number including balls.
};

#define ENABLE_INCR_OPERATORS_ON(T)                                \
inline T& operator++(T& d) { return d = T(int(d) + 1); }           \
inline T& operator--(T& d) { return d = T(int(d) - 1); }

ENABLE_INCR_OPERATORS_ON(BonaPiece)

#undef ENABLE_INCR_OPERATORS_ON

// The number when you look at BonaPiece from the back (the number of steps from the previous 39 to the number 71 from the back)
// Let's call the paired one the ExtBonaPiece type.
union ExtBonaPiece
{
	struct {
		BonaPiece fw; // from white
		BonaPiece fb; // from black
	};
	BonaPiece from[2];

	ExtBonaPiece() {}
	ExtBonaPiece(BonaPiece fw_, BonaPiece fb_) : fw(fw_), fb(fb_) {}
};

// Information about where the piece has moved from where to by this move.
// Assume the piece is an ExtBonaPiece expression.
struct ChangedBonaPiece
{
	ExtBonaPiece old_piece;
	ExtBonaPiece new_piece;
};

// An array for finding the BonaPiece corresponding to the piece pc on the board of the KPP table.
// example)
// BonaPiece fb = kpp_board_index[pc].fb + sq; // BonaPiece corresponding to pc in sq seen from the front
// BonaPiece fw = kpp_board_index[pc].fw + sq; // BonaPiece corresponding to pc in sq seen from behind
extern ExtBonaPiece kpp_board_index[PIECE_NB];

// List of pieces used in the evaluation function. A structure holding which piece (PieceNumber) is where (BonaPiece)
struct EvalList
{
	// List of frame numbers used in evaluation function (FV38 type)
	BonaPiece* piece_list_fw() const { return const_cast<BonaPiece*>(pieceListFw); }
	BonaPiece* piece_list_fb() const { return const_cast<BonaPiece*>(pieceListFb); }

	// Convert the specified piece_no piece to ExtBonaPiece type and return it.
	ExtBonaPiece bona_piece(PieceNumber piece_no) const
	{
		ExtBonaPiece bp;
		bp.fw = pieceListFw[piece_no];
		bp.fb = pieceListFb[piece_no];
		return bp;
	}

	// Place the piece_no pc piece in the sq box on the board
	void put_piece(PieceNumber piece_no, Square sq, Piece pc) {
		set_piece_on_board(piece_no, BonaPiece(kpp_board_index[pc].fw + sq), BonaPiece(kpp_board_index[pc].fb + Inv(sq)), sq);
	}

	// Returns the PieceNumber corresponding to a box on the board.
	PieceNumber piece_no_of_board(Square sq) const { return piece_no_list_board[sq]; }

	// Initialize the pieceList.
	// Set the value of unused pieces to BONA_PIECE_ZERO in case you want to deal with dropped pieces.
	// A normal evaluation function can be used as an evaluation function for missing frames.
	// piece_no_list is initialized with PIECE_NUMBER_NB to facilitate debugging.
	void clear()
	{

		for (auto& p: pieceListFw)
			p = BONA_PIECE_ZERO;

		for (auto& p: pieceListFb)
			p = BONA_PIECE_ZERO;

		for (auto& v :piece_no_list_board)
			v = PIECE_NUMBER_NB;
	}

	// Check whether the pieceListFw[] held internally is a correct BonaPiece.
	// Note: For debugging. slow.
	bool is_valid(const Position& pos);

	// Set that the BonaPiece of the piece_no piece on the board sq is fb,fw.
	inline void set_piece_on_board(PieceNumber piece_no, BonaPiece fw, BonaPiece fb, Square sq)
	{
		assert(is_ok(piece_no));
		pieceListFw[piece_no] = fw;
		pieceListFb[piece_no] = fb;
		piece_no_list_board[sq] = piece_no;
	}

	// Piece list. Piece Number Shows how many pieces are in place (Bona Piece). Used in FV38 etc.

	// Length of piece list
  // 38 fixed
public:
	int length() const { return PIECE_NUMBER_KING; }

	// Must be a multiple of 4 to use VPGATHERDD.
	// In addition, the KPPT type evaluation function, etc. is based on the assumption that the 39th and 40th elements are zero.
	// Please note that there is a part that is accessed.
	static const int MAX_LENGTH = 32;

  // An array that holds the piece number (PieceNumber) for the pieces on the board
  // Hold up to +1 for when the ball is moving to SQUARE_NB,
  // SQUARE_NB balls are not moved, so this value should never be used.
  PieceNumber piece_no_list_board[SQUARE_NB_PLUS1];
private:

	BonaPiece pieceListFw[MAX_LENGTH];
	BonaPiece pieceListFb[MAX_LENGTH];
};

// For management of evaluation value difference calculation
// A structure for managing the number of pieces that have moved from the previous stage
// Up to 2 moving pieces.
struct DirtyPiece
{
	// What changed from the piece with that piece number
	Eval::ChangedBonaPiece changed_piece[2];

	// The number of dirty pieces
	PieceNumber pieceNo[2];

	// The number of dirty files.
	// It can be 0 for null move.
	// Up to 2 moving pieces and taken pieces.
	int dirty_num;

};
#endif  // defined(EVAL_NNUE) || defined(EVAL_LEARN)
}

#endif // #ifndef EVALUATE_H_INCLUDED
