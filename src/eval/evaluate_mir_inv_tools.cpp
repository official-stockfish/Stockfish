#include "evaluate_mir_inv_tools.h"

namespace Eval
{

	// --- tables

	// あるBonaPieceを相手側から見たときの値
	// BONA_PIECE_INITが-1なので符号型で持つ必要がある。
	// KPPTを拡張しても当面、BonaPieceが2^15を超えることはないのでint16_tで良しとする。
	int16_t inv_piece_[Eval::fe_end];

	// 盤面上のあるBonaPieceをミラーした位置にあるものを返す。
	int16_t mir_piece_[Eval::fe_end];


	// --- methods

	// あるBonaPieceを相手側から見たときの値を返す
	Eval::BonaPiece inv_piece(Eval::BonaPiece p) { return (Eval::BonaPiece)inv_piece_[p]; }

	// 盤面上のあるBonaPieceをミラーした位置にあるものを返す。
	Eval::BonaPiece mir_piece(Eval::BonaPiece p) { return (Eval::BonaPiece)mir_piece_[p]; }

	std::function<void()> mir_piece_init_function;

	void init_mir_inv_tables()
	{
		// mirrorとinverseのテーブルの初期化。

		// 初期化は1回に限る。
		static bool first = true;
		if (!first) return;
		first = false;

		// fとeとの交換
		int t[] = {
			f_pawn             , e_pawn            ,
			f_knight           , e_knight          ,
			f_bishop           , e_bishop          ,
			f_rook             , e_rook            ,
			f_queen            , e_queen           ,
		};

		// 未初期化の値を突っ込んでおく。
		for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
		{
			inv_piece_[p] = BONA_PIECE_NOT_INIT;

			// mirrorは手駒に対しては機能しない。元の値を返すだけ。
			mir_piece_[p] = (p < f_pawn) ? p : BONA_PIECE_NOT_INIT;
		}

		for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
		{
			for (int i = 0; i < 32 /* t.size() */; i += 2)
			{
				if (t[i] <= p && p < t[i + 1])
				{
					Square sq = (Square)(p - t[i]);

					// 見つかった!!
					BonaPiece q = (p < fe_hand_end) ? BonaPiece(sq + t[i + 1]) : (BonaPiece)(Inv(sq) + t[i + 1]);
					inv_piece_[p] = q;
					inv_piece_[q] = p;

					/*
					ちょっとトリッキーだが、pに関して盤上の駒は
					p >= fe_hand_end
					のとき。

					このpに対して、nを整数として(上のコードのiは偶数しかとらない)、
					a)  t[2n + 0] <= p < t[2n + 1] のときは先手の駒
					b)  t[2n + 1] <= p < t[2n + 2] のときは後手の駒
					　である。

					 ゆえに、a)の範囲にあるpをq = Inv(p-t[2n+0]) + t[2n+1] とすると180度回転させた升にある後手の駒となる。
					 そこでpとqをswapさせてinv_piece[ ]を初期化してある。
					 */

					 // 手駒に関してはmirrorなど存在しない。
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
			// 未初期化のままになっている。上のテーブルの初期化コードがおかしい。
			assert(mir_piece_[p] != BONA_PIECE_NOT_INIT && mir_piece_[p] < fe_end);
			assert(inv_piece_[p] != BONA_PIECE_NOT_INIT && inv_piece_[p] < fe_end);

			// mirとinvは、2回適用したら元の座標に戻る。
			assert(mir_piece_[mir_piece_[p]] == p);
			assert(inv_piece_[inv_piece_[p]] == p);

			// mir->inv->mir->invは元の場所でなければならない。
			assert(p == inv_piece(mir_piece(inv_piece(mir_piece(p)))));

			// inv->mir->inv->mirは元の場所でなければならない。
			assert(p == mir_piece(inv_piece(mir_piece(inv_piece(p)))));
		}

#if 0
		// 評価関数のミラーをしても大丈夫であるかの事前検証
		// 値を書き込んだときにassertionがあるので、ミラーしてダメである場合、
		// そのassertに引っかかるはず。

		// AperyのWCSC26の評価関数、kppのp1==0とかp1==20(後手の0枚目の歩)とかの
		// ところにゴミが入っていて、これを回避しないとassertに引っかかる。

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

		// さらに出現しない升の盤上の歩、香、桂も除外(Aperyはここにもゴミが入っている)
		for (Rank r = RANK_1; r <= RANK_2; ++r)
			for (File f = FILE_1; f <= FILE_9; ++f)
			{
				if (r == RANK_1)
				{
					// 1段目の歩
					BonaPiece b1 = BonaPiece(f_pawn + (f | r));
					s.insert(b1);
					s.insert(inv_piece[b1]);

					// 1段目の香
					BonaPiece b2 = BonaPiece(f_lance + (f | r));
					s.insert(b2);
					s.insert(inv_piece[b2]);
				}

				// 1,2段目の桂
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
