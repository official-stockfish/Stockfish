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

#if defined(EVAL_NNUE) || defined(EVAL_LEARN)
// 評価関数ファイルを読み込む。
// これは、"is_ready"コマンドの応答時に1度だけ呼び出される。2度呼び出すことは想定していない。
// (ただし、EvalDir(評価関数フォルダ)が変更になったあと、isreadyが再度送られてきたら読みなおす。)
void load_eval();

static uint64_t calc_check_sum() { return 0; }

static void print_softname(uint64_t check_sum) {}

// --- 評価関数で使う定数 KPP(玉と任意2駒)のPに相当するenum

// (評価関数の実験のときには、BonaPieceは自由に定義したいのでここでは定義しない。)


// BonanzaでKKP/KPPと言うときのP(Piece)を表現する型。
// Σ KPPを求めるときに、39の地点の歩のように、升×駒種に対して一意な番号が必要となる。
enum BonaPiece : int32_t
{
	// f = friend(≒先手)の意味。e = enemy(≒後手)の意味

	// 未初期化の時の値
	BONA_PIECE_NOT_INIT = -1,

	// 無効な駒。駒落ちのときなどは、不要な駒をここに移動させる。
	BONA_PIECE_ZERO = 0,

	fe_hand_end = BONA_PIECE_ZERO + 1,

    // Bonanzaのように盤上のありえない升の歩や香の番号を詰めない。
	// 理由1) 学習のときに相対PPで1段目に香がいるときがあって、それを逆変換において正しく表示するのが難しい。
	// 理由2) 縦型BitboardだとSquareからの変換に困る。

	// --- 盤上の駒
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
	fe_end2 = e_king + SQUARE_NB, // 玉も含めた末尾の番号。
};

#define ENABLE_INCR_OPERATORS_ON(T)                                \
inline T& operator++(T& d) { return d = T(int(d) + 1); }           \
inline T& operator--(T& d) { return d = T(int(d) - 1); }

ENABLE_INCR_OPERATORS_ON(BonaPiece)

#undef ENABLE_INCR_OPERATORS_ON

// BonaPieceを後手から見たとき(先手の39の歩を後手から見ると後手の71の歩)の番号とを
// ペアにしたものをExtBonaPiece型と呼ぶことにする。
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

// 駒が今回の指し手によってどこからどこに移動したのかの情報。
// 駒はExtBonaPiece表現であるとする。
struct ChangedBonaPiece
{
	ExtBonaPiece old_piece;
	ExtBonaPiece new_piece;
};

// KPPテーブルの盤上の駒pcに対応するBonaPieceを求めるための配列。
// 例)
// BonaPiece fb = kpp_board_index[pc].fb + sq; // 先手から見たsqにあるpcに対応するBonaPiece
// BonaPiece fw = kpp_board_index[pc].fw + sq; // 後手から見たsqにあるpcに対応するBonaPiece
extern ExtBonaPiece kpp_board_index[PIECE_NB];

// 評価関数で用いる駒リスト。どの駒(PieceNumber)がどこにあるのか(BonaPiece)を保持している構造体
struct EvalList
{
	// 評価関数(FV38型)で用いる駒番号のリスト
	BonaPiece* piece_list_fw() const { return const_cast<BonaPiece*>(pieceListFw); }
	BonaPiece* piece_list_fb() const { return const_cast<BonaPiece*>(pieceListFb); }

	// 指定されたpiece_noの駒をExtBonaPiece型に変換して返す。
	ExtBonaPiece bona_piece(PieceNumber piece_no) const
	{
		ExtBonaPiece bp;
		bp.fw = pieceListFw[piece_no];
		bp.fb = pieceListFb[piece_no];
		return bp;
	}

	// 盤上のsqの升にpiece_noのpcの駒を配置する
	void put_piece(PieceNumber piece_no, Square sq, Piece pc) {
		set_piece_on_board(piece_no, BonaPiece(kpp_board_index[pc].fw + sq), BonaPiece(kpp_board_index[pc].fb + Inv(sq)), sq);
	}

	// 盤上のある升sqに対応するPieceNumberを返す。
	PieceNumber piece_no_of_board(Square sq) const { return piece_no_list_board[sq]; }

	// pieceListを初期化する。
	// 駒落ちに対応させる時のために、未使用の駒の値はBONA_PIECE_ZEROにしておく。
	// 通常の評価関数を駒落ちの評価関数として流用できる。
	// piece_no_listのほうはデバッグが捗るようにPIECE_NUMBER_NBで初期化。
	void clear()
	{

		for (auto& p : pieceListFw)
			p = BONA_PIECE_ZERO;

		for (auto& p : pieceListFb)
			p = BONA_PIECE_ZERO;

		for (auto& v : piece_no_list_board)
			v = PIECE_NUMBER_NB;
	}

	// 内部で保持しているpieceListFw[]が正しいBonaPieceであるかを検査する。
	// 注 : デバッグ用。遅い。
	bool is_valid(const Position& pos);

	// 盤上sqにあるpiece_noの駒のBonaPieceがfb,fwであることを設定する。
	inline void set_piece_on_board(PieceNumber piece_no, BonaPiece fw, BonaPiece fb, Square sq)
	{
		assert(is_ok(piece_no));
		pieceListFw[piece_no] = fw;
		pieceListFb[piece_no] = fb;
		piece_no_list_board[sq] = piece_no;
	}

	// 駒リスト。駒番号(PieceNumber)いくつの駒がどこにあるのか(BonaPiece)を示す。FV38などで用いる。

	// 駒リストの長さ
  // 38固定
public:
	int length() const { return PIECE_NUMBER_KING; }

	// VPGATHERDDを使う都合、4の倍数でなければならない。
	// また、KPPT型評価関数などは、39,40番目の要素がゼロであることを前提とした
	// アクセスをしている箇所があるので注意すること。
	static const int MAX_LENGTH = 32;

  // 盤上の駒に対して、その駒番号(PieceNumber)を保持している配列
  // 玉がSQUARE_NBに移動しているとき用に+1まで保持しておくが、
  // SQUARE_NBの玉を移動させないので、この値を使うことはないはず。
  PieceNumber piece_no_list_board[SQUARE_NB_PLUS1];
private:

	BonaPiece pieceListFw[MAX_LENGTH];
	BonaPiece pieceListFb[MAX_LENGTH];
};

// 評価値の差分計算の管理用
// 前の局面から移動した駒番号を管理するための構造体
// 動く駒は、最大で2個。
struct DirtyPiece
{
	// その駒番号の駒が何から何に変わったのか
	Eval::ChangedBonaPiece changed_piece[2];

	// dirtyになった駒番号
	PieceNumber pieceNo[2];

	// dirtyになった個数。
	// null moveだと0ということもありうる。
	// 動く駒と取られる駒とで最大で2つ。
	int dirty_num;

};
#endif  // defined(EVAL_NNUE) || defined(EVAL_LEARN)
}

#endif // #ifndef EVALUATE_H_INCLUDED
