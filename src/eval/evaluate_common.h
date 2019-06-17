#ifndef _EVALUATE_COMMON_H_
#define _EVALUATE_COMMON_H_

// いまどきの手番つき評価関数(EVAL_KPPTとEVAL_KPP_KKPT)の共用header的なもの。

#if defined (EVAL_KPPT) || defined(EVAL_KPP_KKPT) || defined(EVAL_NNUE)
#include <functional>

// KKファイル名
#define KK_BIN "KK_synthesized.bin"

// KKPファイル名
#define KKP_BIN "KKP_synthesized.bin"

// KPPファイル名
#define KPP_BIN "KPP_synthesized.bin"

namespace Eval
{

#if defined(USE_EVAL_HASH)
	// prefetchする関数
	void prefetch_evalhash(const Key key);
#endif

	// 評価関数のそれぞれのパラメーターに対して関数fを適用してくれるoperator。
	// パラメーターの分析などに用いる。
	// typeは調査対象を表す。
	//   type = -1 : KK,KKP,KPPすべて
	//   type = 0  : KK のみ 
	//   type = 1  : KKPのみ 
	//   type = 2  : KPPのみ 
	void foreach_eval_param(std::function<void(int32_t, int32_t)>f, int type = -1);

	// --------------------------
	//        学習用
	// --------------------------

#if defined(EVAL_LEARN)
	// 学習のときの勾配配列の初期化
	// 学習率を引数に渡しておく。0.0なら、defaultの値を採用する。
	// update_weights()のepochが、eta_epochまでetaから徐々にeta2に変化する。
	// eta2_epoch以降は、eta2から徐々にeta3に変化する。
	void init_grad(double eta1, uint64_t eta_epoch, double eta2, uint64_t eta2_epoch, double eta3);

	// 現在の局面で出現している特徴すべてに対して、勾配の差分値を勾配配列に加算する。
	// freeze[0]  : kkは学習させないフラグ
	// freeze[1]  : kkpは学習させないフラグ
	// freeze[2]  : kppは学習させないフラグ
	// freeze[3]  : kpppは学習させないフラグ
	void add_grad(Position& pos, Color rootColor, double delt_grad, const std::array<bool, 4>& freeze);

	// 現在の勾配をもとにSGDかAdaGradか何かする。
	// epoch      : 世代カウンター(0から始まる)
	// freeze[0]  : kkは学習させないフラグ
	// freeze[1]  : kkpは学習させないフラグ
	// freeze[2]  : kppは学習させないフラグ
	// freeze[3]  : kpppは学習させないフラグ
	void update_weights(uint64_t epoch, const std::array<bool,4>& freeze);

	// 評価関数パラメーターをファイルに保存する。
	// ファイルの末尾につける拡張子を指定できる。
	void save_eval(std::string suffix);

	// 現在のetaを取得する。
	double get_eta();

	// -- 学習に関連したコマンド

	// KKを正規化する関数。元の評価関数と完全に等価にはならないので注意。
	// kkp,kppの値をなるべくゼロに近づけることで、学習中に出現しなかった特徴因子の値(ゼロになっている)が
	// 妥当であることを保証しようという考え。
	void regularize_kk();

#endif


}


#endif

#endif // _EVALUATE_KPPT_COMMON_H_