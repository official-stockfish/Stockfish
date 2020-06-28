#if defined(EVAL_NNUE) || defined(EVAL_LEARN)

#include "evaluate_mir_inv_tools.h"

namespace Eval
{

	// --- tables

	// Value when a certain BonaPiece is seen from the other side
	// BONA_PIECE_INIT is -1, so it must be a signed type.
	// Even if KPPT is expanded, BonaPiece will not exceed 2^15 for the time being, so int16_t is good.
	int16_t inv_piece_[Eval::fe_end];

	// Returns the one at the position where a BonaPiece on the board is mirrored.
	int16_t mir_piece_[Eval::fe_end];


	// --- methods

// Returns the value when a certain BonaPiece is seen from the other side
	Eval::BonaPiece inv_piece(Eval::BonaPiece p) { return (Eval::BonaPiece)inv_piece_[p]; }

	// Returns the one at the position where a BonaPiece on the board is mirrored.
	Eval::BonaPiece mir_piece(Eval::BonaPiece p) { return (Eval::BonaPiece)mir_piece_[p]; }

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
			f_pawn             , e_pawn            ,
			f_knight           , e_knight          ,
			f_bishop           , e_bishop          ,
			f_rook             , e_rook            ,
			f_queen            , e_queen           ,
		};

		// Insert uninitialized value.
		for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
		{
			inv_piece_[p] = BONA_PIECE_NOT_INIT;

			// mirror does not work for hand pieces. Just return the original value.
			mir_piece_[p] = (p < f_pawn) ? p : BONA_PIECE_NOT_INIT;
		}

		for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
		{
			for (int i = 0; i < 32 /* t.size() */; i += 2)
			{
				if (t[i] <= p && p < t[i + 1])
				{
					Square sq = (Square)(p - t[i]);

					// found!!
					BonaPiece q = (p < fe_hand_end) ? BonaPiece(sq + t[i + 1]) : (BonaPiece)(Inv(sq) + t[i + 1]);
					inv_piece_[p] = q;
					inv_piece_[q] = p;

					/*
					It's a bit tricky, but regarding p
										p >= fe_hand_end
										When.

					For this p, let n be an integer (i in the above code can only be an even number),
					a) When t[2n + 0] <= p <t[2n + 1], the first piece
					b) When t[2n + 1] <= p <t[2n + 2], the back piece
					Is.

					Therefore, if p in the range of a) is set to q = Inv(p-t[2n+0]) + t[2n+1], it becomes the back piece in the box rotated 180 degrees.
					So inv_piece[] is initialized by swapping p and q.
					*/

					// There is no mirror for hand pieces.
					if (p < fe_hand_end)
						continue;

					BonaPiece r1 = (BonaPiece)(Mir(sq) + t[i]);
					mir_piece_[p] = r1;
					mir_piece_[r1] = p;

					BonaPiece p2 = (BonaPiece)(sq + t[i + 1]);
					BonaPiece r2 = (BonaPiece)(Mir(sq) + t[i + 1]);
					mir_piece_[p2] = r2;
					mir_piece_[r2] = p2;

					break;
				}
			}
		}

		if (mir_piece_init_function)
			mir_piece_init_function();

		for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
		{
			// It remains uninitialized. The initialization code in the table above is incorrect.
			assert(mir_piece_[p] != BONA_PIECE_NOT_INIT && mir_piece_[p] < fe_end);
			assert(inv_piece_[p] != BONA_PIECE_NOT_INIT && inv_piece_[p] < fe_end);

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

		std::unordered_set<BonaPiece> s;
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
			s.insert((BonaPiece)b);

		// Excludes walks, incense, and katsura on the board that do not appear further (Apery also contains garbage here)
		for (Rank r = RANK_1; r <= RANK_2; ++r)
			for (File f = FILE_1; f <= FILE_9; ++f)
			{
				if (r == RANK_1)
				{
					// first step
					BonaPiece b1 = BonaPiece(f_pawn + (f | r));
					s.insert(b1);
					s.insert(inv_piece[b1]);

					// 1st stage incense
					BonaPiece b2 = BonaPiece(f_lance + (f | r));
					s.insert(b2);
					s.insert(inv_piece[b2]);
				}

				// Katsura on the 1st and 2nd steps
				BonaPiece b = BonaPiece(f_knight + (f | r));
				s.insert(b);
				s.insert(inv_piece[b]);
			}

		cout << "\nchecking kpp_write()..";
		for (auto sq : SQ)
		{
			cout << sq << ' ';
			for (BonaPiece p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
				for (BonaPiece p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
					if (!s.count(p1) && !s.count(p2))
						kpp_write(sq, p1, p2, kpp[sq][p1][p2]);
		}
		cout << "\nchecking kkp_write()..";

		for (auto sq1 : SQ)
		{
			cout << sq1 << ' ';
			for (auto sq2 : SQ)
				for (BonaPiece p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
					if (!s.count(p1))
						kkp_write(sq1, sq2, p1, kkp[sq1][sq2][p1]);
		}
		cout << "..done!" << endl;
#endif
	}

}

#endif  // defined(EVAL_NNUE) || defined(EVAL_LEARN)
