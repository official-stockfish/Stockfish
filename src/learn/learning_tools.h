#ifndef __LEARN_WEIGHT_H__
#define __LEARN_WEIGHT_H__

// 評価関数の機械学習のときに用いる重み配列などに関する機械学習用ツール類一式

#include "learn.h"
#if defined (EVAL_LEARN)
#include "../eval/evaluate_mir_inv_tools.h"

#if defined(SGD_UPDATE) || defined(USE_KPPP_MIRROR_WRITE)
#include "../misc.h"  // PRNG , my_insertion_sort
#endif

#include <cmath>	// std::sqrt()

namespace EvalLearningTools
{
	// -------------------------------------------------
	//                     初期化
	// -------------------------------------------------

	// このEvalLearningTools名前空間にあるテーブル類を初期化する。
	// 学習の開始までに必ず一度呼び出すこと。
	// この関数のなかで、init_mir_inv_tables()も呼び出している。
	// (この関数を呼ぶときは、init_mir_inv_tables()を呼び出す必要はない。)
	void init();

	// -------------------------------------------------
	//                     flags
	// -------------------------------------------------

	// 次元下げしたときに、そのなかの一番小さなindexになることが
	// わかっているindexに対してtrueとなっているフラグ配列。
	// この配列もinit()によって初期化される。
	// KPPPに関しては、関与しない。
	// ゆえに、この配列の有効なindexの範囲は、KK::min_index()～KPP::max_index()まで。
	extern std::vector<bool> min_index_flag;

	// -------------------------------------------------
	//       勾配等を格納している学習用の配列
	// -------------------------------------------------

#if defined(_MSC_VER)
#pragma pack(push,2)
#elif defined(__GNUC__)
#pragma pack(2)
#endif
	struct Weight
	{
		// mini-batch 1回分の勾配の累積値
		LearnFloatType g = LearnFloatType(0);

		// ADA_GRAD_UPDATEのとき。LearnFloatType == floatとして、
		// 合計 4*2 + 4*2 + 1*2 = 18 bytes
		// 1GBの評価関数パラメーターに対してその4.5倍のサイズのWeight配列が確保できれば良い。
		// ただし、構造体のアライメントが4バイト単位になっているとsizeof(Weight)==20なコードが生成されるので
		// pragma pack(2)を指定しておく。

		// SGD_UPDATE の場合、この構造体はさらに10バイト減って、8バイトで済む。

		// AdaGradなどの学習率η(eta)。
		// updateFV()が呼び出されるまでにeta1,2,3,eta1_epoch,eta2_epochは設定されているものとする。
		// update_weights()のepochが、eta1_epochまでeta1から徐々にeta2に変化する。
		// eta2_epoch以降は、eta2から徐々にeta3に変化する。
		static double eta;
		static double eta1;
		static double eta2;
		static double eta3;
		static uint64_t eta1_epoch;
		static uint64_t eta2_epoch;

		// etaの一括初期化。0が渡された場合、デフォルト値が設定される。
		static void init_eta(double eta1, double eta2, double eta3, uint64_t eta1_epoch, uint64_t eta2_epoch)
		{
			Weight::eta1 = (eta1 != 0) ? eta1 : 30.0;
			Weight::eta2 = (eta2 != 0) ? eta2 : 30.0;
			Weight::eta3 = (eta3 != 0) ? eta3 : 30.0;
			Weight::eta1_epoch = (eta1_epoch != 0) ? eta1_epoch : 0;
			Weight::eta2_epoch = (eta2_epoch != 0) ? eta2_epoch : 0;
		}

		// epochに応じたetaを設定してやる。
		static void calc_eta(uint64_t epoch)
		{
			if (Weight::eta1_epoch == 0) // eta2適用除外
				Weight::eta = Weight::eta1;
			else if (epoch < Weight::eta1_epoch)
				// 按分する
				Weight::eta = Weight::eta1 + (Weight::eta2 - Weight::eta1) * epoch / Weight::eta1_epoch;
			else if (Weight::eta2_epoch == 0) // eta3適用除外
				Weight::eta = Weight::eta2;
			else if (epoch < Weight::eta2_epoch)
				Weight::eta = Weight::eta2 + (Weight::eta3 - Weight::eta2) * (epoch - Weight::eta1_epoch) / (Weight::eta2_epoch - Weight::eta1_epoch);
			else
				Weight::eta = Weight::eta3;
		}

		template <typename T> void updateFV(T& v) { updateFV(v, 1.0); }

#if defined (ADA_GRAD_UPDATE)

		// floatで正確に計算できる最大値はINT16_MAX*256-1なのでそれより
		// 小さい値をマーカーにしておく。
		const LearnFloatType V0_NOT_INIT = (INT16_MAX * 128);

		// vを内部的に保持しているもの。以前の実装ではメモリの節約のために固定小数で小数部だけを保持していたが
		// 精度的に怪しいし、見通しが悪くなるので廃止した。
		LearnFloatType v0 = LearnFloatType(V0_NOT_INIT);

		// AdaGradのg2
		LearnFloatType g2 = LearnFloatType(0);

		// AdaGradでupdateする
		// この関数を実行しているときにgの値やメンバーが書き変わらないことは
		// 呼び出し側で保証されている。atomic演算である必要はない。
		// kはetaに掛かる係数。普通は1.0で良い。手番項に対してetaを下げたいときにここを1/8.0などとする。
		template <typename T>
		void updateFV(T& v,double k)
		{
			// AdaGradの更新式
			//   勾配ベクトルをg、更新したいベクトルをv、η(eta)は定数として、
			//     g2 = g2 + g^2
			//     v = v - ηg/sqrt(g2)

			constexpr double epsilon = 0.000001;

			if (g == LearnFloatType(0))
				return;

			g2 += g * g;

			// v0がV0_NOT_INITであるなら、値がKK/KKP/KPP配列の値で初期化されていないということだから、
			// この場合、vの値を引数で渡されたものから読み込む。
			double V = (v0 == V0_NOT_INIT) ? v : v0;

			V -= k * eta * (double)g / sqrt((double)g2 + epsilon);

			// Vの値を型の範囲に収まるように制限する。
			// ちなみに、windows.hがmin,maxマクロを定義してしまうのでそれを回避するために、
			// ここでは括弧で括ることで関数形式マクロとして扱われないようにしている。
			V = (std::min)((double)(std::numeric_limits<T>::max)() , V);
			V = (std::max)((double)(std::numeric_limits<T>::min)() , V);

			v0 = (LearnFloatType)V;
			v = (T)round(V);

			// この要素に関するmini-batchの1回分の更新が終わったのでgをクリア
			// g[i] = 0;
			// →次元下げの問題があるので、これは呼び出し側で行なうことにする。
		}

#elif defined(SGD_UPDATE)

		// 勾配の符号だけ見るSGDでupdateする
		// この関数を実行しているときにgの値やメンバーが書き変わらないことは
		// 呼び出し側で保証されている。atomic演算である必要はない。
		template <typename T>
		void updateFV(T & v , double k)
		{
			if (g == 0)
				return;

			// gの符号だけ見てupdateする。
			// g < 0 なら vを少し足す。
			// g > 0 なら vを少し引く。

			// 整数しか足さないので小数部不要。

			// 0～5ぐらいずつ動かすのがよさげ。
			// ガウス分布っぽいほうが良いので5bitの乱数を発生させて(それぞれのbitは1/2の確率で1である)、
			// それをpop_count()する。このとき、二項分布になっている。
			//int16_t diff = (int16_t)POPCNT32((u32)prng.rand(31));
			// →　これ80スレッドでやったら、このAsyncPRNG::rand()がlockするのでslow downした。この実装良くない。
			int16_t diff = 1;

			double V = v;
			if (g > 0.0)
				V-= diff;
			else
				V+= diff;

			V = (std::min)((double)(std::numeric_limits<T>::max)(), V);
			V = (std::max)((double)(std::numeric_limits<T>::min)(), V);

			v = (T)V;
		}

#endif

		// gradの設定
		template <typename T> void set_grad(const T& g_) { g = g_; }

		// gradの加算
		template <typename T> void add_grad(const T& g_) { g += g_; }

		LearnFloatType get_grad() const { return g; }
	};
#if defined(_MSC_VER)
#pragma pack(pop)
#elif defined(__GNUC__)
#pragma pack(0)
#endif

	// 手番つきのweight配列
	// 透過的に扱えるようにするために、Weightと同じメンバを持たせておいてやる。
	struct Weight2
	{
		Weight w[2];

		// 手番評価、etaを1/8に評価しておく。
		template <typename T> void updateFV(std::array<T, 2>& v) { w[0].updateFV(v[0] , 1.0); w[1].updateFV(v[1],1.0/8.0); }

		template <typename T> void set_grad(const std::array<T, 2>& g) { for (int i = 0; i<2; ++i) w[i].set_grad(g[i]); }
		template <typename T> void add_grad(const std::array<T, 2>& g) { for (int i = 0; i<2; ++i) w[i].add_grad(g[i]); }

		std::array<LearnFloatType, 2> get_grad() const { return std::array<LearnFloatType, 2>{w[0].get_grad(), w[1].get_grad()}; }
	};

	// -------------------------------------------------
	// Weight配列を直列化したときのindexを計算したりするヘルパー。
	// -------------------------------------------------

	// KK,KKP,KPP,KKPPの基底クラス
	// これらのクラスの使い方
	// 
	// 1. まずset()で初期化する。例) KK g_kk; g_kk.set(SQUARE_NB,fe_end,0);
	// 2. 次にfromIndex(),fromKK()などでインスタンスを生成
	// 3. king(),piece0(),piece1()などのプロパティを用いてアクセス。
	// 
	// この説明だけではわかりにくいかも知れないが、学習部のinit_grad(),add_grad(),update_weights()などを見れば
	// 必要性を含めて理解できると思う。
	//
	// 注意 : この派生クラスでは次元下げのために上記のinv_piece/mir_pieceを間接的に参照することがあるので、
	// 最初にEvalLearningTools::init()かinit_mir_inv_tables()を呼び出して初期化すること。
	//
	// 備考) 派生クラス側でoverrideすべきではない関数名には/*final*/と書いてある。
	//       派生クラス側でoverrideすべき関数は "= 0"をつけて、純粋仮想関数にしてある。
	//       派生クラス側でoverrideしてもしなくても良い関数はvirtualだけつけてある。
	//
	struct SerializerBase
	{

		// KK,KKP,KPP配列を直列化するときの通し番号の最小値、最大値+1。
		/*final*/ uint64_t min_index() const { return min_index_; }
		/*final*/ uint64_t max_index() const { return min_index() + max_raw_index_; }

		// max_index() - min_index()の値。
		// 派生クラス側でmax_king_sq_,fe_end_などから、値を計算して返すようにする。
		virtual uint64_t size() const = 0;

		// 与えられたindexが、min_index()以上、max_index()未満にあるかを判定する。
		/*final*/ bool is_ok(uint64_t index) { return min_index() <= index && index < max_index(); }

		// 必ずこのset()を呼び出して使う。さもなくば、派生クラス側のfromKK()/fromIndex()などでインスタンスを構築して使う。
		virtual void set(int max_king_sq, uint64_t fe_end, uint64_t min_index)
		{
			max_king_sq_ = max_king_sq;
			fe_end_ = fe_end;
			min_index_ = min_index;
			max_raw_index_ = size();
		}

		// 現在のメンバの値に基いて、直列化されたときのindexを取得する。
		/*final*/ uint64_t toIndex() const {
			return min_index() + toRawIndex();
		}

		// 直列化するときのindexを返す。(min_index()の値は加算する前のもの)
		virtual uint64_t toRawIndex() const = 0;

	protected:
		// このクラスの返すmin_index()の値
		uint64_t min_index_;

		// このクラスの返すmax_index()の値 = min_index() + max_raw_index_
		// この変数は派生クラスのsize()で計算されたもの。
		uint64_t max_raw_index_;

		// サポートする玉の升の数(通常SQUARE_NB)
		int max_king_sq_;

		// サポートするBonaPieceの最大値
		uint64_t fe_end_;

	};

	struct KK : public SerializerBase
	{
	protected:
		KK(Square king0, Square king1,bool inverse) : king0_(king0), king1_(king1) , inverse_sign(inverse) {}
	public:
		KK() {}

		virtual uint64_t size() const { return max_king_sq_ * max_king_sq_; }

		// index(通し番号)からKKのオブジェクトを生成するbuilder
		KK fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// raw_index(通し番号ではなく0から始まる番号)からKKのオブジェクトを生成するbuilder
		KK fromRawIndex(uint64_t raw_index) const
		{
			int king1 = (int)(raw_index % SQUARE_NB);
			raw_index /= SQUARE_NB;
			int king0 = (int)(raw_index  /* % SQUARE_NB */);
			assert(king0 < SQUARE_NB);
			return fromKK((Square)king0, (Square)king1 , false);
		}
		KK fromKK(Square king0, Square king1 , bool inverse) const
		{
			// kkという変数名はEval::kk配列などで使っているので別の名前にする必要がある。(以下、KKP,KPPクラスなどでも同様)
			KK my_kk(king0, king1, inverse);
			my_kk.set(max_king_sq_, fe_end_, min_index());
			return my_kk;
		}
		KK fromKK(Square king0, Square king1) const { return fromKK(king0, king1, false); }

		// fromIndex()を用いてこのオブジェクトを構築したときに、以下のアクセッサで情報が得られる。
		Square king0() const { return king0_; }
		Square king1() const { return king1_; }

// 次元下げの数
#if defined(USE_KK_INVERSE_WRITE)
	#define KK_LOWER_COUNT 4
#elif defined(USE_KK_MIRROR_WRITE)
	#define KK_LOWER_COUNT 2
#else 
	#define KK_LOWER_COUNT 1
#endif

#if defined(USE_KK_INVERSE_WRITE) && !defined(USE_KK_MIRROR_WRITE) 
		// USE_KK_INVERSE_WRITEわ使うならUSE_KK_MIRROR_WRITEも定義して欲しい。
		static_assert(false, "define also USE_KK_MIRROR_WRITE!");
#endif

		// 低次元の配列のindexを得る。
		// USE_KK_INVERSE_WRITEが有効なときは、それらをinverseしたものが[2],[3]に入る。
		// この次元下げに関して、gradの符号は反転させないといけないので注意すること。
		// is_inverse()で判定できるのでこれを利用すると良い。
		void toLowerDimensions(/*out*/KK kk_[KK_LOWER_COUNT]) const {
			kk_[0] = fromKK(king0_, king1_,false);
#if defined(USE_KK_MIRROR_WRITE)
			kk_[1] = fromKK(Mir(king0_),Mir(king1_),false);
#if defined(USE_KK_INVERSE_WRITE)
			kk_[2] = fromKK(Inv(king1_), Inv(king0_),true);
			kk_[3] = fromKK(Inv(Mir(king1_)) , Inv(Mir(king0_)),true);
#endif
#endif
		}

		// このクラスのmin_index()の値を0として数えたときのindexを取得する。
		virtual uint64_t toRawIndex() const {
			return (uint64_t)king0_ * (uint64_t)max_king_sq_ + (uint64_t)king1_;
		}

		// toLowerDimensionsで次元下げしたものがinverseしたものであるかを返す。
		bool is_inverse() const {
			return inverse_sign;
		}

		// is_inverse() == trueのときに、gradの手番ではないほうの符号を反転させて返す。
		template <typename T>
		std::array<T, 2> apply_inverse_sign(const std::array<T, 2>& rhs)
		{
			return !is_inverse() ? rhs : std::array<T, 2>{-rhs[0], rhs[1]};
		}

		// 比較演算子
		bool operator==(const KK& rhs) { return king0() == rhs.king0() && king1() == rhs.king1(); }
		bool operator!=(const KK& rhs) { return !(*this == rhs); }

	private:
		Square king0_, king1_ ;
		bool inverse_sign;
	};

	// デバッグ用出力。
	static std::ostream& operator<<(std::ostream& os, KK rhs)
	{
		os << "KK(" << rhs.king0() << "," << rhs.king1() << ")";
		return os;
	}

	// KKと同じく。KKP用。
	struct KKP : public SerializerBase
	{
	protected:
		KKP(Square king0, Square king1, Eval::BonaPiece p) : king0_(king0), king1_(king1), piece_(p), inverse_sign(false) {}
		KKP(Square king0, Square king1, Eval::BonaPiece p, bool inverse) : king0_(king0), king1_(king1), piece_(p),inverse_sign(inverse) {}
	public:
		KKP() {}

		virtual uint64_t size() const { return (uint64_t)max_king_sq_*(uint64_t)max_king_sq_*(uint64_t)fe_end_; }

		// index(通し番号)からKKPのオブジェクトを生成するbuilder
		KKP fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// raw_index(通し番号ではなく0から始まる番号)からKKPのオブジェクトを生成するbuilder
		KKP fromRawIndex(uint64_t raw_index) const
		{
			int piece = (int)(raw_index % Eval::fe_end);
			raw_index /= Eval::fe_end;
			int king1 = (int)(raw_index % SQUARE_NB);
			raw_index /= SQUARE_NB;
			int king0 = (int)(raw_index  /* % SQUARE_NB */);
			assert(king0 < SQUARE_NB);
			return fromKKP((Square)king0, (Square)king1, (Eval::BonaPiece)piece,false);
		}

		KKP fromKKP(Square king0, Square king1, Eval::BonaPiece p, bool inverse) const
		{
			KKP my_kkp(king0, king1, p, inverse);
			my_kkp.set(max_king_sq_,fe_end_,min_index());
			return my_kkp;
		}
		KKP fromKKP(Square king0, Square king1, Eval::BonaPiece p) const { return fromKKP(king0, king1, p, false); }

		// fromIndex()を用いてこのオブジェクトを構築したときに、以下のアクセッサで情報が得られる。
		Square king0() const { return king0_; }
		Square king1() const { return king1_; }
		Eval::BonaPiece piece() const { return piece_; }

		// KKPの次元下げの数
#if defined(USE_KKP_INVERSE_WRITE)
		#define KKP_LOWER_COUNT 4
#elif defined(USE_KKP_MIRROR_WRITE)
		#define KKP_LOWER_COUNT 2
#else
		#define KKP_LOWER_COUNT 1
#endif

#if defined(USE_KKP_INVERSE_WRITE) && !defined(USE_KKP_MIRROR_WRITE) 
		// USE_KKP_INVERSE_WRITEわ使うならUSE_KKP_MIRROR_WRITEも定義して欲しい。
		static_assert(false, "define also USE_KKP_MIRROR_WRITE!");
#endif

		// 低次元の配列のindexを得る。ミラーしたものがkkp_[1]に返る。
		// USE_KKP_INVERSE_WRITEが有効なときは、それらをinverseしたものが[2],[3]に入る。
		// この次元下げに関して、gradの符号は反転させないといけないので注意すること。
		// is_inverse()で判定できるのでこれを利用すると良い。
		void toLowerDimensions(/*out*/ KKP kkp_[KKP_LOWER_COUNT]) const {
			kkp_[0] = fromKKP(king0_, king1_, piece_,false);
#if defined(USE_KKP_MIRROR_WRITE)
			kkp_[1] = fromKKP(Mir(king0_), Mir(king1_), mir_piece(piece_),false);
#if defined(USE_KKP_INVERSE_WRITE)
			kkp_[2] = fromKKP( Inv(king1_), Inv(king0_), inv_piece(piece_),true);
			kkp_[3] = fromKKP( Inv(Mir(king1_)), Inv(Mir(king0_)) , inv_piece(mir_piece(piece_)),true);
#endif
#endif
		}

		// このクラスのmin_index()の値を0として数えたときのindexを取得する。
		virtual uint64_t toRawIndex() const {
			return  ((uint64_t)king0_ * (uint64_t)max_king_sq_ + (uint64_t)king1_) * (uint64_t)fe_end_ + (uint64_t)piece_;
		}

		// toLowerDimensionsで次元下げしたものがinverseしたものであるかを返す。
		bool is_inverse() const {
			return inverse_sign;
		}

		// is_inverse() == trueのときに、gradの手番ではないほうの符号を反転させて返す。
		template <typename T>
		std::array<T, 2> apply_inverse_sign(const std::array<T, 2>& rhs)
		{
			return !is_inverse() ? rhs : std::array<T, 2>{-rhs[0], rhs[1]};
		}

		// 比較演算子
		bool operator==(const KKP& rhs) { return king0() == rhs.king0() && king1() == rhs.king1() && piece() == rhs.piece(); }
		bool operator!=(const KKP& rhs) { return !(*this == rhs); }

	private:
		Square king0_, king1_;
		Eval::BonaPiece piece_;
		bool inverse_sign;
	};

	// デバッグ用出力。
	static std::ostream& operator<<(std::ostream& os, KKP rhs)
	{
		os << "KKP(" << rhs.king0() << "," << rhs.king1() << "," << rhs.piece() << ")";
		return os;
	}


	// KK,KKPと同様。KPP用
	struct KPP : public SerializerBase
	{
	protected:
		KPP(Square king, Eval::BonaPiece p0, Eval::BonaPiece p1) : king_(king), piece0_(p0), piece1_(p1) {}

	public:
		KPP() {}

		// KK,KKP,KPP配列を直列化するときの通し番号の、KPPの最小値、最大値。
#if !defined(USE_TRIANGLE_WEIGHT_ARRAY)
		virtual uint64_t size() const { return (uint64_t)max_king_sq_*(uint64_t)fe_end_*(uint64_t)fe_end_; }
#else
		// kpp[SQUARE_NB][fe_end][fe_end]の[fe_end][fe_end]な正方配列の部分を三角配列化する。
		// kpp[SQUARE_NB][triangle_fe_end]とすると、この三角配列の1行目は要素1個、2行目は2個、…。
		// ゆえに、triangle_fe_end = 1 + 2 + .. + fe_end = fe_end * (fe_end + 1) / 2
		virtual uint64_t size() const { return (uint64_t)max_king_sq_*(uint64_t)triangle_fe_end; }
#endif

		virtual void set(int max_king_sq, uint64_t fe_end, uint64_t min_index)
		{
			// この値、size()で用いていて、SerializerBase::set()でsize()を使うので先に計算する。
			triangle_fe_end = (uint64_t)fe_end*((uint64_t)fe_end + 1) / 2;

			SerializerBase::set(max_king_sq, fe_end, min_index);
		}

		// index(通し番号)からKPPのオブジェクトを生成するbuilder
		KPP fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// raw_index(通し番号ではなく0から始まる番号)からKPPのオブジェクトを生成するbuilder
		KPP fromRawIndex(uint64_t raw_index) const
		{
			const uint64_t triangle_fe_end = (uint64_t)fe_end_*((uint64_t)fe_end_ + 1) / 2;

#if !defined(USE_TRIANGLE_WEIGHT_ARRAY)
			int piece1 = (int)(raw_index % fe_end_);
			raw_index /= fe_end_;
			int piece0 = (int)(raw_index % fe_end_);
			raw_index /= fe_end_;
#else
			uint64_t index2 = raw_index % triangle_fe_end;

			// ここにindex2からpiece0,piece1を求める式を書く。
			// これは index2 = i * (i+1) / 2 + j の逆関数となる。
			// j = 0 の場合、i^2 + i - 2 * index2 == 0なので
			// 2次方程式の解の公式から i = (sqrt(8*index2+1) - 1) / 2である。
			// iを整数化したのちに、j = index2 - i * (i + 1) / 2としてjを求めれば良い。

			// BonaPieceは32bit(16bitに収まらない可能性)を想定しているのでこの掛け算は64bitでないといけない。
			int piece1 = int(sqrt(8 * index2 + 1) - 1) / 2;
			int piece0 = int(index2 - (uint64_t)piece1*((uint64_t)piece1 + 1) / 2);

			assert(piece1 < (int)fe_end_);
			assert(piece0 < (int)fe_end_);
			assert(piece0 > piece1);

			raw_index /= triangle_fe_end;
#endif
			int king = (int)(raw_index  /* % SQUARE_NB */);
			assert(king < max_king_sq_);
			return fromKPP((Square)king, (Eval::BonaPiece)piece0, (Eval::BonaPiece)piece1);
		}

		KPP fromKPP(Square king, Eval::BonaPiece p0, Eval::BonaPiece p1) const
		{
			KPP my_kpp(king, p0, p1);
			my_kpp.set(max_king_sq_,fe_end_,min_index());
			return my_kpp;
		}

		// fromIndex()を用いてこのオブジェクトを構築したときに、以下のアクセッサで情報が得られる。
		Square king() const { return king_; }
		Eval::BonaPiece piece0() const { return piece0_; }
		Eval::BonaPiece piece1() const { return piece1_; }


		// 次元下げの数
#if defined(USE_KPP_MIRROR_WRITE)
	#if !defined(USE_TRIANGLE_WEIGHT_ARRAY)
		#define KPP_LOWER_COUNT 4
	#else
		#define KPP_LOWER_COUNT 2
	#endif
#else
	#if !defined(USE_TRIANGLE_WEIGHT_ARRAY)
		#define KPP_LOWER_COUNT 2
	#else
		#define KPP_LOWER_COUNT 1
	#endif
#endif

		// 低次元の配列のindexを得る。p1,p2を入れ替えたもの、ミラーしたものなどが返る。
		void toLowerDimensions(/*out*/ KPP kpp_[KPP_LOWER_COUNT]) const {

#if defined(USE_TRIANGLE_WEIGHT_ARRAY)
			// 三角配列を用いる場合は、piece0とpiece1を入れ替えたものは返らないので注意。
			kpp_[0] = fromKPP(king_, piece0_, piece1_);
#if defined(USE_KPP_MIRROR_WRITE)
			kpp_[1] = fromKPP(Mir(king_), mir_piece(piece0_), mir_piece(piece1_));
#endif

#else
			// 三角配列を用いない場合
			kpp_[0] = fromKPP(king_, piece0_, piece1_);
			kpp_[1] = fromKPP(king_, piece1_, piece0_);
#if defined(USE_KPP_MIRROR_WRITE)
			kpp_[2] = fromKPP(Mir(king_), mir_piece(piece0_), mir_piece(piece1_));
			kpp_[3] = fromKPP(Mir(king_), mir_piece(piece1_), mir_piece(piece0_));
#endif
#endif
		}

		// このクラスのmin_index()の値を0として数えたときのindexを取得する。
		virtual uint64_t toRawIndex() const {

#if !defined(USE_TRIANGLE_WEIGHT_ARRAY)

			return ((uint64_t)king_ * (uint64_t)fe_end_ + (uint64_t)piece0_) * (uint64_t)fe_end_ + (uint64_t)piece1_;

#else
			// Bonanza6.0で使われているのに似せたマクロ
			auto PcPcOnSq = [&](Square k, Eval::BonaPiece i, Eval::BonaPiece j)
			{

				// この三角配列の(i,j)は、i行目のj列目の要素。
				// i行目0列目は、そこまでの要素の合計であるから、1 + 2 + ... + i = i * (i+1) / 2
				// i行目j列目は、これにjを足したもの。i * (i + 1) /2 + j

				// BonaPiece型は、32bitを想定しているので掛け算には気をつけないとオーバーフローする。
				return (uint64_t)k * triangle_fe_end + (uint64_t)(uint64_t(i)*(uint64_t(i)+1) / 2 + uint64_t(j));
			};

			auto k = king_;
			auto i = piece0_;
			auto j = piece1_;

			return (i >= j) ? PcPcOnSq(k, i, j) : PcPcOnSq(k, j, i);
#endif
		}

		// toLowerDimensionsで次元下げしたものがinverseしたものであるかを返す。
		// KK,KKPとinterfaceを合せるために用意してある。このKPPクラスでは、このメソッドは常にfalseを返す。
		bool is_inverse() const {
			return false;
		}

		// 比較演算子
		bool operator==(const KPP& rhs) {
			return king() == rhs.king() &&
				((piece0() == rhs.piece0() && piece1() == rhs.piece1())
#if defined(USE_TRIANGLE_WEIGHT_ARRAY)
					// 三角配列を用いるときはpiece0とpiece1の入れ替わりを許容する。
				|| (piece0() == rhs.piece1() && piece1() == rhs.piece0())
#endif
					); }
		bool operator!=(const KPP& rhs) { return !(*this == rhs); }


	private:
		Square king_;
		Eval::BonaPiece piece0_, piece1_;

		uint64_t triangle_fe_end; // = (uint64_t)fe_end_*((uint64_t)fe_end_ + 1) / 2;
	};

	// デバッグ用出力。
	static std::ostream& operator<<(std::ostream& os, KPP rhs)
	{
		os << "KPP(" << rhs.king() << "," << rhs.piece0() << "," << rhs.piece1() << ")";
		return os;
	}

	// KPPPの4駒関係。ただし、手番ありでミラー等を考慮しないと学習に2TB以上のメモリが必要…。
	// 三角配列を使っても学習のために50GB×12バイト = 600GB必要。
	// ミラーしたもののみを格納するようにしてもの半分ぐらい必要。
	// ここでは、三角配列は必ず用いて、かつミラーしたものを格納するものとする。
	//
	// また、このクラスのking()は、実際のkingのSquareとは限らず、単に、0～(king_sq-1)までの値が返る。
	// これは、ミラーを利用した圧縮を行なう場合など、利用側で適切な玉の位置に変換してやる必要がある。
	// 
	// あと、このクラスの返すpiece0,1,2に関して、
	//   piece0() > piece1() > piece2()
	// であり、コンストラクタでpiece0,1,2を渡すときも、この制約を守る必要がある。
	struct KPPP : public SerializerBase
	{
	protected:
		KPPP(int king, Eval::BonaPiece p0, Eval::BonaPiece p1, Eval::BonaPiece p2) :
			king_(king), piece0_(p0), piece1_(p1), piece2_(p2)
		{
			assert(piece0_ > piece1_ && piece1_ > piece2_);
			/* sort_piece(); */
		}

	public:
		KPPP() {}

		virtual uint64_t size() const { return (uint64_t)max_king_sq_*triangle_fe_end; }

		// fe_endとking_sqを設定する。
		// fe_end : このKPPPクラスの想定するfe_end
		// king_sq : KPPPのときに扱う玉の升の数。
		//  3段×ミラーなら3段×5筋 = 15みたいな感じ。
		//  2段×ミラーなしなら2×9筋 = 18みたいな感じ。
		//  これをこのKPPPクラスを使う側でset()を用いて最初に設定する。
		virtual void set(int max_king_sq, uint64_t fe_end,uint64_t min_index) {
			// この値、size()で用いていて、SerializerBase::set()でsize()を使うので先に計算する。
			triangle_fe_end = fe_end * (fe_end - 1) * (fe_end - 2) / 6;

			SerializerBase::set(max_king_sq, fe_end, min_index);
		}

		// 次元下げの数
		// とりあえず、ミラーの次元下げ非対応。ここでやることもないかと…。
/*
#if defined(USE_KPPP_MIRROR_WRITE)
#define KPPP_LOWER_COUNT 2
#else
#define KPPP_LOWER_COUNT 1
#endif
*/
#define KPPP_LOWER_COUNT 1

		// 低次元の配列のindexを得る。
		// p0,p1,p2を入れ替えたものは返らないので注意。
		// またミラーしたものも、USE_KPPP_MIRROR_WRITEが有効なときしか返さない。
		void toLowerDimensions(/*out*/ KPPP kppp_[KPPP_LOWER_COUNT]) const
		{
			kppp_[0] = fromKPPP(king_, piece0_, piece1_,piece2_);
#if KPPP_LOWER_COUNT > 1
			// mir_pieceするとsortされてない状態になる。sortするコードが必要。
			Eval::BonaPiece p_list[3] = { mir_piece(piece2_), mir_piece(piece1_), mir_piece(piece0_) };
			my_insertion_sort(p_list, 0, 3);
			kppp_[1] = fromKPPP((int)Mir((Square)king_), p_list[2] , p_list[1], p_list[0]);
#endif
		}

		// index(通し番号)からKPPPのオブジェクトを生成するbuilder
		KPPP fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// raw_index(通し番号ではなく0から始まる番号)からKPPPのオブジェクトを生成するbuilder
		KPPP fromRawIndex(uint64_t raw_index) const
		{
			uint64_t index2 = raw_index % triangle_fe_end;

			// ここにindex2からpiece0,piece1,piece2を求める式を書く。
			// これは index2 = i(i-1)(i-2)/6-1 + j(j+1)/2 + k の逆関数となる。
			// j = k = 0 の場合、3次方程式の解の公式から実根は、 i = ...である。(以下式) 
			// ただしindex2が0,1のときは実数解が複数ある。これを考慮しないといけない。計算精度が足りないことに対する対策必要。
			// iが求まったあとはiを整数化したのちに、最初の式に入れてKPPのとき同様にjを求めれば良い。

			// この処理、数値計算としてわりと難しい。色々工夫が必要。

			int piece0;
			if (index2 <= 1)
			{
				// index2 == 0,1のときだけ実数解が複数ある。
				piece0 = (int)index2 + 2;

			} else {

				//double t = pow(sqrt((243 *index2 * index2 - 1) * 3) + 27 * index2, 1.0 / 3);
				// →　これだとindex2が大きくなるとsqrt()の中身、オーバーフローする。

				// sqrt()の中身がオーバーフローするので、sqrtのなかで3.0を掛けずにsqrtの外側でsqrt(3.0)を掛ける。
				// sqrt()の中身がオーバーフローするので、index2が大きいときは近似式を用いる。

				double t;
				
				if (index2 < 100000000)
					t = pow(sqrt((243.0 *index2 * index2 - 1)) * sqrt(3.0) + 27 * index2, 1.0 / 3);
				else
					// index2が非常に大きいとき、sqrtの中身、近似的に √243 * index2とみなせるだろう。
					t = pow( index2 * sqrt(243 * 3.0) + 27 * index2, 1.0 / 3);
				
				// 丸めのときに計算誤差でわずかに足りないのを防ぐためデルタを加算する。
				// 大きすぎると1大きい数になってしまう時があるので調整が必要。
				
				const double delta = 0.000000001;

				piece0 = int(t / pow(3.0, 2.0 / 3) + 1.0 / (pow(3.0, 1.0 / 3) * t) + delta) + 1;
				// ううう。ほんまにこんなことせんとあかんのか？(´ω｀)
			}

			// piece2が求まったので、上式のi(i-1)(i-2)/6(=aとする)のiにpiece2を代入。また、k = 0を代入。
			// j(j+1)/2 = index2 - a
			// これは、2次方程式の解の公式より..

			uint64_t a = (uint64_t)piece0*((uint64_t)piece0 - 1)*((uint64_t)piece0 - 2) / 6;
			int piece1 = int((1 + sqrt(8.0 * (index2 - a ) + 1)) / 2);
			uint64_t b = (uint64_t)piece1 * (piece1 - 1) / 2;
			int piece2 = int(index2 - a - b);

#if 0
			if (!((piece0 > piece1 && piece1 > piece2)))
			{
				std::cout << index << " , " << index2 << "," << a << "," << sqrt(8.0 * (index2 - a) + 1);
			}
#endif

			assert(piece0 > piece1 && piece1 > piece2);

			assert(piece2 < (int)fe_end_);
			assert(piece1 < (int)fe_end_);
			assert(piece0 < (int)fe_end_);

			raw_index /= triangle_fe_end;

			int king = (int)(raw_index  /* % SQUARE_NB */);
			assert(king < max_king_sq_);

			// king_sqとfe_endに関しては伝播させる。
			return fromKPPP((Square)king, (Eval::BonaPiece)piece0, (Eval::BonaPiece)piece1 , (Eval::BonaPiece)piece2);
		}

		// k,p0,p1,p2を指定してKPPPのインスタンスをbuildする。
		// 内部的に保持しているset()で渡されたking_sqとfe_endは引き継ぐ。
		KPPP fromKPPP(int king, Eval::BonaPiece p0, Eval::BonaPiece p1, Eval::BonaPiece p2) const
		{
			KPPP kppp(king, p0, p1, p2);
			kppp.set(max_king_sq_, fe_end_,min_index());
			return kppp;
		}

		// このクラスのmin_index()の値を0として数えたときのindexを取得する。
		virtual uint64_t toRawIndex() const {

			// Bonanza 6.0で使われているのに似せたマクロ
			// 前提条件) i > j > k であること。
			// i==j,j==kのケースはNG。
			auto PcPcPcOnSq = [this](int king, Eval::BonaPiece i, Eval::BonaPiece j , Eval::BonaPiece k)
			{
				// この三角配列の(i,j,k)は、i行目のj列目の要素。
				// i行目0列0番目は、そこまでの要素の合計であるから、0 + 0 + 1 + 3 + 6 + ... + (i)*(i-1)/2 = i*(i-1)*(i-2)/ 6
				// i行目j列0番目は、そこにjを加味したもの。 + j*(j-1) / 2
				// i行目j列k番目は、そこにkを足したもの。   + k
				assert(i > j && j > k);

				// BonaPiece型は、32bitを想定しているので掛け算には気をつけないとオーバーフローする。
				return (uint64_t)king * triangle_fe_end + (uint64_t)(
						  uint64_t(i)*(uint64_t(i) - 1) * (uint64_t(i) - 2) / 6
						+ uint64_t(j)*(uint64_t(j) - 1) / 2
						+ uint64_t(k)
					);
			};

			return PcPcPcOnSq(king_, piece0_, piece1_, piece2_);
		}

		// fromIndex()を用いてこのオブジェクトを構築したときに、以下のアクセッサで情報が得られる。
		int king() const { return king_; }
		Eval::BonaPiece piece0() const { return piece0_; }
		Eval::BonaPiece piece1() const { return piece1_; }
		Eval::BonaPiece piece2() const { return piece2_; }
		// toLowerDimensionsで次元下げしたものがinverseしたものであるかを返す。
		// KK,KKPとinterfaceを合せるために用意してある。このKPPPクラスでは、このメソッドは常にfalseを返す。
		bool is_inverse() const {
			return false;
		}

		// 3角配列化したときの要素の数を返す。kppp配列が、以下のような2次元配列だと想定している。
		//   kppp[king_sq][triangle_fe_end];
		uint64_t get_triangle_fe_end() const { return triangle_fe_end; }

		// 比較演算子
		bool operator==(const KPPP& rhs) {
			// piece0 > piece1 > piece2を前提とするので、入れ替わりの可能性はない。
			return king() == rhs.king() && piece0() == rhs.piece0() && piece1() == rhs.piece1() && piece2() == rhs.piece2();
		}
		bool operator!=(const KPPP& rhs) { return !(*this == rhs); }

	private:

		int king_;
		Eval::BonaPiece piece0_, piece1_,piece2_;

		// kppp[king_sq][fe_end][fe_end][fe_end]の[fe_end][fe_end][fe_end]な正方配列の部分を三角配列化する。
		// kppp[king_sq][triangle_fe_end]とすると、この三角配列の0行目から要素数は、0,0,1,3,…,n行目はn(n-1)/2個。
		// ゆえに、
		// triangle_fe_end = Σn(n-1)/2 , n=0..fe_end-1
		//                 =  fe_end * (fe_end - 1) * (fe_end - 2) / 6
		uint64_t triangle_fe_end; // ((uint64_t)Eval::fe_end)*((uint64_t)Eval::fe_end - 1)*((uint64_t)Eval::fe_end - 2) / 6;
	};

	// デバッグ用出力。
	static std::ostream& operator<<(std::ostream& os, KPPP rhs)
	{
		os << "KPPP(" << rhs.king() << "," << rhs.piece0() << "," << rhs.piece1() << "," << rhs.piece2() << ")";
		return os;
	}

	// KKPPによる4駒関係の学習用。
	//
	// KPPPクラスと同じ設計。KPPPクラスで、pが一枚少ないものとして扱う。
	// ２つの玉の位置は0～king_sq-1までの値としてencodeされているものとする。
	//
	// あと、このクラスの返すpiece0,1に関して、
	//   piece0() > piece1()
	// であり、コンストラクタでpiece0,1を渡すときも、この制約を守る必要がある。
	//
	// この制約から、BonaPieceZeroをpiece0,piece1に同時に代入して渡すことは出来ない。
	// 駒落ちの学習に対応させるならevaluate()で工夫が必要。
	struct KKPP : SerializerBase
	{
	protected:
		KKPP(int king, Eval::BonaPiece p0, Eval::BonaPiece p1) :
			king_(king), piece0_(p0), piece1_(p1)
		{
			assert(piece0_ > piece1_);
			/* sort_piece(); */
		}

	public:
		KKPP() {}

		virtual uint64_t size() const { return (uint64_t)max_king_sq_*triangle_fe_end; }

		// fe_endとking_sqを設定する。
		// fe_end : このKPPPクラスの想定するfe_end
		// king_sq : KPPPのときに扱う玉の升の数。
		//  9段×ミラーなら9段×5筋の2乗(先後の玉) = 45*45 = 2025 みたいな感じ。
		//  これをこのKKPPクラスを使う側でset()を用いて最初に設定する。
		void set(int max_king_sq, uint64_t fe_end , uint64_t min_index) {
			// この値、size()で用いていて、SerializerBase::set()でsize()を使うので先に計算する。
			triangle_fe_end = fe_end * (fe_end - 1) / 2;

			SerializerBase::set(max_king_sq, fe_end, min_index);
		}

		// 次元下げの数
		// とりあえず、ミラーの次元下げ非対応。ここでやることもないかと…。(学習用のメモリがもったいないので)
#define KKPP_LOWER_COUNT 1

		// 低次元の配列のindexを得る。
		// p0,p1,p2を入れ替えたものは返らないので注意。
		// またミラーしたものも、USE_KPPP_MIRROR_WRITEが有効なときしか返さない。
		void toLowerDimensions(/*out*/ KKPP kkpp_[KPPP_LOWER_COUNT]) const
		{
			kkpp_[0] = fromKKPP(king_, piece0_, piece1_);

			// ミラーする場合、mir_pieceするとsortされてない状態になる。sortするコードが必要。
			// あとking_に対するミラーを定義する必要も。
		}

		// index(通し番号)からKKPPのオブジェクトを生成するbuilder
		KKPP fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// raw_index(通し番号ではなく0から始まる番号)からKKPPのオブジェクトを生成するbuilder
		KKPP fromRawIndex(uint64_t raw_index) const
		{
			uint64_t index2 = raw_index % triangle_fe_end;

			// ここにindex2からpiece0,piece1,piece2を求める式を書く。
			// これは index2 = i(i-1)/2 + j の逆関数となる。
			// j=0として、二次方程式の解の公式を用いる。
			// index2=0のときは重根だが小さいほうはi>jを満たさないので無視。

			int piece0 = (int(sqrt(8 * index2 + 1)) + 1)/2;
			int piece1 = int(index2 - piece0 * (piece0 - 1) /2 );

			assert(piece0 > piece1);

			assert(piece1 < (int)fe_end_);
			assert(piece0 < (int)fe_end_);

			raw_index /= triangle_fe_end;

			int king = (int)(raw_index  /* % SQUARE_NB */);
			assert(king < max_king_sq_);

			// king_sqとfe_endに関しては伝播させる。
			return fromKKPP(king, (Eval::BonaPiece)piece0, (Eval::BonaPiece)piece1);
		}

		// k,p0,p1を指定してKKPPのインスタンスをbuildする。
		// 内部的に保持しているset()で渡されたking_sqとfe_endは引き継ぐ。
		KKPP fromKKPP(int king, Eval::BonaPiece p0, Eval::BonaPiece p1) const
		{
			KKPP kkpp(king, p0, p1);
			kkpp.set(max_king_sq_, fe_end_,min_index());
			return kkpp;
		}

		// このクラスのmin_index()の値を0として数えたときのindexを取得する。
		virtual uint64_t toRawIndex() const {

			// Bonanza 6.0で使われているのに似せたマクロ
			// 前提条件) i > jであること。
			// i==j,j==kのケースはNG。
			auto PcPcOnSq = [this](int king, Eval::BonaPiece i, Eval::BonaPiece j)
			{
				assert(i > j);

				// BonaPiece型は、32bitを想定しているので掛け算には気をつけないとオーバーフローする。
				return (uint64_t)king * triangle_fe_end + (uint64_t)(
					+ uint64_t(i)*(uint64_t(i) - 1) / 2
					+ uint64_t(j)
					);
			};

			return PcPcOnSq(king_, piece0_, piece1_);
		}

		// fromIndex(),fromKKPP()を用いてこのオブジェクトを構築したときに、以下のアクセッサで情報が得られる。
		int king() const { return king_; }
		Eval::BonaPiece piece0() const { return piece0_; }
		Eval::BonaPiece piece1() const { return piece1_; }

		// toLowerDimensionsで次元下げしたものがinverseしたものであるかを返す。
		// KK,KKPとinterfaceを合せるために用意してある。このKKPPクラスでは、このメソッドは常にfalseを返す。
		bool is_inverse() const {
			return false;
		}

		// 3角配列化したときの要素の数を返す。kkpp配列が、以下のような2次元配列だと想定している。
		//   kkpp[king_sq][triangle_fe_end];
		uint64_t get_triangle_fe_end() const { return triangle_fe_end; }

		// 比較演算子
		bool operator==(const KKPP& rhs) {
			// piece0 > piece1を前提とするので、入れ替わりの可能性はない。
			return king() == rhs.king() && piece0() == rhs.piece0() && piece1() == rhs.piece1();
		}
		bool operator!=(const KKPP& rhs) { return !(*this == rhs); }

	private:

		int king_;
		Eval::BonaPiece piece0_, piece1_;

		// kppp[king_sq][fe_end][fe_end]の[fe_end][fe_end]な正方配列の部分を三角配列化する。
		uint64_t triangle_fe_end = 0;
		
	};

	// デバッグ用出力。
	static std::ostream& operator<<(std::ostream& os, KKPP rhs)
	{
		os << "KKPP(" << rhs.king() << "," << rhs.piece0() << "," << rhs.piece1() << ")";
		return os;
	}


}

#endif // defined (EVAL_LEARN)
#endif
