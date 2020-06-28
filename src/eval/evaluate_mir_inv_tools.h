#ifndef _EVALUATE_MIR_INV_TOOLS_
#define _EVALUATE_MIR_INV_TOOLS_

#if defined(EVAL_NNUE) || defined(EVAL_LEARN)

// BonaPiece's mirror (horizontal flip) and inverse (180° on the board) tools to get pieces.

#include "../types.h"
#include "../evaluate.h"
#include <functional>

namespace Eval
{
	// -------------------------------------------------
	//                  tables
	// -------------------------------------------------

	// --- Provide Mirror and Inverse to BonaPiece.

	// These arrays are initialized by calling init() or init_mir_inv_tables();.
	// If you want to use only this table from the evaluation function,
	// Call init_mir_inv_tables().
	// These arrays are referenced from the KK/KKP/KPP classes below.

	// Returns the value when a certain BonaPiece is seen from the other side
	extern Eval::BonaPiece inv_piece(Eval::BonaPiece p);

	// Returns the one at the position where a BonaPiece on the board is mirrored.
	extern Eval::BonaPiece mir_piece(Eval::BonaPiece p);


	// callback called when initializing mir_piece/inv_piece
	// Used when extending fe_end on the user side.
	// Inv_piece_ and inv_piece_ are exposed because they are necessary for this initialization.
	// At the timing when mir_piece_init_function is called, until fe_old_end
	// It is guaranteed that these tables have been initialized.
	extern std::function<void()> mir_piece_init_function;
	extern int16_t mir_piece_[Eval::fe_end];
	extern int16_t inv_piece_[Eval::fe_end];

	// The table above will be initialized when you call this function explicitly or call init().
	extern void init_mir_inv_tables();
}

#endif  // defined(EVAL_NNUE) || defined(EVAL_LEARN)

#endif
