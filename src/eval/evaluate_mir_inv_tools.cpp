#if defined(EVAL_NNUE) || defined(EVAL_LEARN)

#include "evaluate_mir_inv_tools.h"

namespace Eval
{

	// --- tables

	// Value when a certain PieceSquare is seen from the other side
	// BONA_PIECE_INIT is -1, so it must be a signed type.
	// Even if KPPT is expanded, PieceSquare will not exceed 2^15 for the time being, so int16_t is good.
	int16_t inv_piece_[PieceSquare::PS_END];

	// Returns the one at the position where a PieceSquare on the board is mirrored.
	int16_t mir_piece_[PieceSquare::PS_END];


	// --- methods

// Returns the value when a certain PieceSquare is seen from the other side
	PieceSquare inv_piece(PieceSquare p) { return (PieceSquare)inv_piece_[p]; }

	// Returns the one at the position where a PieceSquare on the board is mirrored.
	PieceSquare mir_piece(PieceSquare p) { return (PieceSquare)mir_piece_[p]; }

	std::function<void()> mir_piece_init_function;

	void init_mir_inv_tables()
	{
		// Initialize the mirror and inverse tables.

		// Initialization is limited to once.
		static bool first = true;
		if (!first) return;
		first = false;

		// exchange f and e
		int t[] = {
			PieceSquare::PS_W_PAWN             , PieceSquare::PS_B_PAWN            ,
			PieceSquare::PS_W_KNIGHT           , PieceSquare::PS_B_KNIGHT          ,
			PieceSquare::PS_W_BISHOP           , PieceSquare::PS_B_BISHOP          ,
			PieceSquare::PS_W_ROOK             , PieceSquare::PS_B_ROOK            ,
			PieceSquare::PS_W_QUEEN            , PieceSquare::PS_B_QUEEN           ,
		};

		// Insert uninitialized value.
		for (PieceSquare p = PieceSquare::PS_NONE; p < PieceSquare::PS_END; ++p)
		{
			inv_piece_[p] = PieceSquare::PS_NOT_INIT;

			// mirror does not work for hand pieces. Just return the original value.
			mir_piece_[p] = (p < PieceSquare::PS_W_PAWN) ? p : PieceSquare::PS_NOT_INIT;
		}

		for (PieceSquare p = PieceSquare::PS_NONE; p < PieceSquare::PS_END; ++p)
		{
			for (int i = 0; i < 32 /* t.size() */; i += 2)
			{
				if (t[i] <= p && p < t[i + 1])
				{
					Square sq = (Square)(p - t[i]);

					// found!!
					PieceSquare q = (p < PieceSquare::PS_W_PAWN) ? PieceSquare(sq + t[i + 1]) : (PieceSquare)(rotate180(sq) + t[i + 1]);
					inv_piece_[p] = q;
					inv_piece_[q] = p;

					/*
					It's a bit tricky, but regarding p
										p >= PieceSquare::PS_W_PAWN
										When.

					For this p, let n be an integer (i in the above code can only be an even number),
					a) When t[2n + 0] <= p <t[2n + 1], the first piece
					b) When t[2n + 1] <= p <t[2n + 2], the back piece
					Is.

					Therefore, if p in the range of a) is set to q = rotate180(p-t[2n+0]) + t[2n+1], it becomes the back piece in the box rotated 180 degrees.
					So inv_piece[] is initialized by swapping p and q.
					*/

					// There is no mirror for hand pieces.
					if (p < PieceSquare::PS_W_PAWN)
						continue;

					PieceSquare r1 = (PieceSquare)(flip_file(sq) + t[i]);
					mir_piece_[p] = r1;
					mir_piece_[r1] = p;

					PieceSquare p2 = (PieceSquare)(sq + t[i + 1]);
					PieceSquare r2 = (PieceSquare)(flip_file(sq) + t[i + 1]);
					mir_piece_[p2] = r2;
					mir_piece_[r2] = p2;

					break;
				}
			}
		}

		if (mir_piece_init_function)
			mir_piece_init_function();

		for (PieceSquare p = PieceSquare::PS_NONE; p < PieceSquare::PS_END; ++p)
		{
			// It remains uninitialized. The initialization code in the table above is incorrect.
			assert(mir_piece_[p] != PieceSquare::PS_NOT_INIT && mir_piece_[p] < PieceSquare::PS_END);
			assert(inv_piece_[p] != PieceSquare::PS_NOT_INIT && inv_piece_[p] < PieceSquare::PS_END);

			// mir and inv return to their original coordinates after being applied twice.
			assert(mir_piece_[mir_piece_[p]] == p);
			assert(inv_piece_[inv_piece_[p]] == p);

			// mir->inv->mir->inv must be the original location.
			assert(p == inv_piece(mir_piece(inv_piece(mir_piece(p)))));

			// inv->mir->inv->mir must be the original location.
			assert(p == mir_piece(inv_piece(mir_piece(inv_piece(p)))));
		}

#if 0
		// Pre-verification that it is okay to mirror the evaluation function
		// When writing a value, there is an assertion, so if you can't mirror it,
		// Should get caught in the assert.

		// Apery's WCSC26 evaluation function, kpp p1==0 or p1==20 (0th step on the back)
		// There is dust in it, and if you don't avoid it, it will get caught in the assert.

		std::unordered_set<PieceSquare> s;
		vector<int> a = {
			f_hand_pawn - 1,e_hand_pawn - 1,
			f_hand_lance - 1, e_hand_lance - 1,
			f_hand_knight - 1, e_hand_knight - 1,
			f_hand_silver - 1, e_hand_silver - 1,
			f_hand_gold - 1, e_hand_gold - 1,
			f_hand_bishop - 1, e_hand_bishop - 1,
			f_hand_rook - 1, e_hand_rook - 1,
		};
		for (auto b : a)
			s.insert((PieceSquare)b);

		// Excludes walks, incense, and katsura on the board that do not appear further (Apery also contains garbage here)
		for (Rank r = RANK_1; r <= RANK_2; ++r)
			for (File f = FILE_1; f <= FILE_9; ++f)
			{
				if (r == RANK_1)
				{
					// first step
					PieceSquare b1 = PieceSquare(PieceSquare::PS_W_PAWN + (f | r));
					s.insert(b1);
					s.insert(inv_piece[b1]);

					// 1st stage incense
					PieceSquare b2 = PieceSquare(f_lance + (f | r));
					s.insert(b2);
					s.insert(inv_piece[b2]);
				}

				// Katsura on the 1st and 2nd steps
				PieceSquare b = PieceSquare(PieceSquare::PS_W_KNIGHT + (f | r));
				s.insert(b);
				s.insert(inv_piece[b]);
			}

		cout << "\nchecking kpp_write()..";
		for (auto sq : SQ)
		{
			cout << sq << ' ';
			for (PieceSquare p1 = PieceSquare::PS_NONE; p1 < PieceSquare::PS_END; ++p1)
				for (PieceSquare p2 = PieceSquare::PS_NONE; p2 < PieceSquare::PS_END; ++p2)
					if (!s.count(p1) && !s.count(p2))
						kpp_write(sq, p1, p2, kpp[sq][p1][p2]);
		}
		cout << "\nchecking kkp_write()..";

		for (auto sq1 : SQ)
		{
			cout << sq1 << ' ';
			for (auto sq2 : SQ)
				for (PieceSquare p1 = PieceSquare::PS_NONE; p1 < PieceSquare::PS_END; ++p1)
					if (!s.count(p1))
						kkp_write(sq1, sq2, p1, kkp[sq1][sq2][p1]);
		}
		cout << "..done!" << endl;
#endif
	}

}

#endif  // defined(EVAL_NNUE) || defined(EVAL_LEARN)
