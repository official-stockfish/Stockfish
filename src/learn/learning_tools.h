#ifndef __LEARN_WEIGHT_H__
#define __LEARN_WEIGHT_H__

// A set of machine learning tools related to the weight array used for machine learning of evaluation functions

#include "learn.h"
#if defined (EVAL_LEARN)
#include <array>

#include "../eval/evaluate_mir_inv_tools.h"

#if defined(SGD_UPDATE) || defined(USE_KPPP_MIRROR_WRITE)
#include "../misc.h"  // PRNG , my_insertion_sort
#endif

#include <cmath>	// std::sqrt()

namespace EvalLearningTools
{
	// -------------------------------------------------
	//                  Initialization
	// -------------------------------------------------

	// Initialize the tables in this EvalLearningTools namespace.
	// Be sure to call once before learning starts.
	// In this function, we also call init_mir_inv_tables().
	// (It is not necessary to call init_mir_inv_tables() when calling this function.)
	void init();

	// -------------------------------------------------
	//                     flags
	// -------------------------------------------------

	// When the dimension is lowered, it may become the smallest index among them
	// A flag array that is true for the known index.
	// This array is also initialized by init().
	// KPPP is not involved.
	// Therefore, the valid index range of this array is from KK::min_index() to KPP::max_index().
	extern std::vector<bool> min_index_flag;

	// -------------------------------------------------
	//   Array for learning that stores gradients etc.
	// -------------------------------------------------

#if defined(_MSC_VER)
#pragma pack(push,2)
#elif defined(__GNUC__)
#pragma pack(2)
#endif
	struct Weight
	{
		// cumulative value of one mini-batch gradient
		LearnFloatType g = LearnFloatType(0);

		// When ADA_GRAD_UPDATE. LearnFloatType == float,
		// total 4*2 + 4*2 + 1*2 = 18 bytes
		// It suffices to secure a Weight array that is 4.5 times the size of the evaluation function parameter of 1GB.
		// However, sizeof(Weight)==20 code is generated if the structure alignment is in 4-byte units, so
		// Specify pragma pack(2).

		// For SGD_UPDATE, this structure is reduced by 10 bytes to 8 bytes.

		// Learning rate η(eta) such as AdaGrad.
		// It is assumed that eta1,2,3,eta1_epoch,eta2_epoch have been set by the time updateFV() is called.
		// The epoch of update_weights() gradually changes from eta1 to eta2 until eta1_epoch.
		// After eta2_epoch, gradually change from eta2 to eta3.
		static double eta;
		static double eta1;
		static double eta2;
		static double eta3;
		static uint64_t eta1_epoch;
		static uint64_t eta2_epoch;

		// Batch initialization of eta. If 0 is passed, the default value will be set.
		static void init_eta(double eta1, double eta2, double eta3, uint64_t eta1_epoch, uint64_t eta2_epoch)
		{
			Weight::eta1 = (eta1 != 0) ? eta1 : 30.0;
			Weight::eta2 = (eta2 != 0) ? eta2 : 30.0;
			Weight::eta3 = (eta3 != 0) ? eta3 : 30.0;
			Weight::eta1_epoch = (eta1_epoch != 0) ? eta1_epoch : 0;
			Weight::eta2_epoch = (eta2_epoch != 0) ? eta2_epoch : 0;
		}

		// Set eta according to epoch.
		static void calc_eta(uint64_t epoch)
		{
			if (Weight::eta1_epoch == 0) // Exclude eta2
				Weight::eta = Weight::eta1;
			else if (epoch < Weight::eta1_epoch)
				// apportion
				Weight::eta = Weight::eta1 + (Weight::eta2 - Weight::eta1) * epoch / Weight::eta1_epoch;
			else if (Weight::eta2_epoch == 0) // Exclude eta3
				Weight::eta = Weight::eta2;
			else if (epoch < Weight::eta2_epoch)
				Weight::eta = Weight::eta2 + (Weight::eta3 - Weight::eta2) * (epoch - Weight::eta1_epoch) / (Weight::eta2_epoch - Weight::eta1_epoch);
			else
				Weight::eta = Weight::eta3;
		}

		template <typename T> void updateFV(T& v) { updateFV(v, 1.0); }

#if defined (ADA_GRAD_UPDATE)

		// Since the maximum value that can be accurately calculated with float is INT16_MAX*256-1
		// Keep the small value as a marker.
		const LearnFloatType V0_NOT_INIT = (INT16_MAX * 128);

		// What holds v internally. The previous implementation kept a fixed decimal with only a fractional part to save memory,
		// Since it is doubtful in accuracy and the visibility is bad, it was abolished.
		LearnFloatType v0 = LearnFloatType(V0_NOT_INIT);

		// AdaGrad g2
		LearnFloatType g2 = LearnFloatType(0);

		// update with AdaGrad
		// When executing this function, the value of g and the member do not change
		// Guaranteed by the caller. It does not have to be an atomic operation.
		// k is a coefficient for eta. 1.0 is usually sufficient. If you want to lower eta for your turn item, set this to 1/8.0 etc.
		template <typename T>
		void updateFV(T& v,double k)
		{
			// AdaGrad update formula
			// Gradient vector is g, vector to be updated is v, η(eta) is a constant,
			//     g2 = g2 + g^2
			//     v = v - ηg/sqrt(g2)

			constexpr double epsilon = 0.000001;

			if (g == LearnFloatType(0))
				return;

			g2 += g * g;

			// If v0 is V0_NOT_INIT, it means that the value is not initialized with the value of KK/KKP/KPP array,
			// In this case, read the value of v from the one passed in the argument.
			double V = (v0 == V0_NOT_INIT) ? v : v0;

			V -= k * eta * (double)g / sqrt((double)g2 + epsilon);

			// Limit the value of V to be within the range of types.
			// By the way, windows.h defines the min and max macros, so to avoid it,
			// Here, it is enclosed in parentheses so that it is not treated as a function-like macro.
			V = (std::min)((double)(std::numeric_limits<T>::max)() , V);
			V = (std::max)((double)(std::numeric_limits<T>::min)() , V);

			v0 = (LearnFloatType)V;
			v = (T)round(V);

			// Clear g because one update of mini-batch for this element is over
			// g[i] = 0;
			// → There is a problem of dimension reduction, so this will be done by the caller.
		}

#elif defined(SGD_UPDATE)

		// See only the sign of the gradient Update with SGD
		// When executing this function, the value of g and the member do not change
		// Guaranteed by the caller. It does not have to be an atomic operation.
		template <typename T>
		void updateFV(T & v , double k)
		{
			if (g == 0)
				return;

			// See only the sign of g and update.
			// If g <0, add v a little.
			// If g> 0, subtract v slightly.

			// Since we only add integers, no decimal part is required.

			// It's a good idea to move around 0-5.
			// It is better to have a Gaussian distribution, so generate a 5-bit random number (each bit has a 1/2 probability of 1),
			// Pop_count() it. At this time, it has a binomial distribution.
			//int16_t diff = (int16_t)POPCNT32((u32)prng.rand(31));
			// → If I do this with 80 threads, this AsyncPRNG::rand() locks, so I slowed down. This implementation is not good.
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

		// grad setting
		template <typename T> void set_grad(const T& g_) { g = g_; }

		// Add grad
		template <typename T> void add_grad(const T& g_) { g += g_; }

		LearnFloatType get_grad() const { return g; }
	};
#if defined(_MSC_VER)
#pragma pack(pop)
#elif defined(__GNUC__)
#pragma pack(0)
#endif

	// Turned weight array
	// In order to be able to handle it transparently, let's have the same member as Weight.
	struct Weight2
	{
		Weight w[2];

		//Evaluate your turn, eta 1/8.
		template <typename T> void updateFV(std::array<T, 2>& v) { w[0].updateFV(v[0] , 1.0); w[1].updateFV(v[1],1.0/8.0); }

		template <typename T> void set_grad(const std::array<T, 2>& g) { for (int i = 0; i<2; ++i) w[i].set_grad(g[i]); }
		template <typename T> void add_grad(const std::array<T, 2>& g) { for (int i = 0; i<2; ++i) w[i].add_grad(g[i]); }

		std::array<LearnFloatType, 2> get_grad() const { return std::array<LearnFloatType, 2>{w[0].get_grad(), w[1].get_grad()}; }
	};

	// ------------------------------------------------ -
	// A helper that calculates the index when the Weight array is serialized.
	// ------------------------------------------------ -

	// Base class for KK,KKP,KPP,KKPP
	// How to use these classes
	//
	// 1. Initialize with set() first. Example) KK g_kk; g_kk.set(SQUARE_NB,fe_end,0);
	// 2. Next create an instance with fromIndex(), fromKK(), etc.
	// 3. Access using properties such as king(), piece0(), piece1().
	//
	// It may be difficult to understand just by this explanation, but if you look at init_grad(), add_grad(), update_weights() etc. in the learning part
	// I think you can understand it including the necessity.
	//
	// Note: this derived class may indirectly reference the above inv_piece/mir_piece for dimension reduction, so
	// Initialize by calling EvalLearningTools::init() or init_mir_inv_tables() first.
	//
	// Remarks) /*final*/ is written for the function name that should not be overridden on the derived class side.
	// The function that should be overridden on the derived class side is a pure virtual function with "= 0".
	// Only virtual functions are added to the derived class that may or may not be overridden.
	//
	struct SerializerBase
	{

		// Minimum value and maximum value of serial number +1 when serializing KK, KKP, KPP arrays.
		/*final*/ uint64_t min_index() const { return min_index_; }
		/*final*/ uint64_t max_index() const { return min_index() + max_raw_index_; }

		// max_index() - min_index() the value of.
		// Calculate the value from max_king_sq_,fe_end_ etc. on the derived class side and return it.
		virtual uint64_t size() const = 0;

		// Determine if the given index is more than min_index() and less than max_index().
		/*final*/ bool is_ok(uint64_t index) { return min_index() <= index && index < max_index(); }

		// Make sure to call this set(). Otherwise, construct an instance using fromKK()/fromIndex() etc. on the derived class side.
		virtual void set(int max_king_sq, uint64_t fe_end, uint64_t min_index)
		{
			max_king_sq_ = max_king_sq;
			fe_end_ = fe_end;
			min_index_ = min_index;
			max_raw_index_ = size();
		}

		// Get the index when serialized, based on the value of the current member.
		/*final*/ uint64_t toIndex() const {
			return min_index() + toRawIndex();
		}

		// Returns the index when serializing. (The value of min_index() is before addition)
		virtual uint64_t toRawIndex() const = 0;

	protected:
		// The value of min_index() returned by this class
		uint64_t min_index_;

		// The value of max_index() returned by this class = min_index() + max_raw_index_
		// This variable is calculated by size() of the derived class.
		uint64_t max_raw_index_;

		// The number of balls to support (normally SQUARE_NB)
		int max_king_sq_;

		// Maximum BonaPiece value supported
		uint64_t fe_end_;

	};

	struct KK : public SerializerBase
	{
	protected:
		KK(Square king0, Square king1,bool inverse) : king0_(king0), king1_(king1) , inverse_sign(inverse) {}
	public:
		KK() {}

		virtual uint64_t size() const { return max_king_sq_ * max_king_sq_; }

		// builder that creates KK object from index (serial number)
		KK fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// builder that creates KK object from raw_index (number starting from 0, not serial number)
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
			// The variable name kk is used in the Eval::kk array etc., so it needs to be different. (The same applies to KKP, KPP classes, etc.)
			KK my_kk(king0, king1, inverse);
			my_kk.set(max_king_sq_, fe_end_, min_index());
			return my_kk;
		}
		KK fromKK(Square king0, Square king1) const { return fromKK(king0, king1, false); }

		// When you construct this object using fromIndex(), you can get information with the following accessors.
		Square king0() const { return king0_; }
		Square king1() const { return king1_; }

// number of dimension reductions
#if defined(USE_KK_INVERSE_WRITE)
	#define KK_LOWER_COUNT 4
#elif defined(USE_KK_MIRROR_WRITE)
	#define KK_LOWER_COUNT 2
#else 
	#define KK_LOWER_COUNT 1
#endif

#if defined(USE_KK_INVERSE_WRITE) && !defined(USE_KK_MIRROR_WRITE) 
		// USE_KK_INVERSE_WRITE If you use it, please also define USE_KK_MIRROR_WRITE.
		static_assert(false, "define also USE_KK_MIRROR_WRITE!");
#endif

		// Get the index of the low-dimensional array.
		// When USE_KK_INVERSE_WRITE is enabled, the inverse of them will be in [2] and [3].
		// Note that the sign of grad must be reversed for this dimension reduction.
		// You can use is_inverse() because it can be determined.
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

		// Get the index when counting the value of min_index() of this class as 0.
		virtual uint64_t toRawIndex() const {
			return (uint64_t)king0_ * (uint64_t)max_king_sq_ + (uint64_t)king1_;
		}

		// Returns whether or not the dimension lowered with toLowerDimensions is inverse.
		bool is_inverse() const {
			return inverse_sign;
		}

		// When is_inverse() == true, reverse the sign that is not grad's turn and return it.
		template <typename T>
		std::array<T, 2> apply_inverse_sign(const std::array<T, 2>& rhs)
		{
			return !is_inverse() ? rhs : std::array<T, 2>{-rhs[0], rhs[1]};
		}

		// comparison operator
		bool operator==(const KK& rhs) { return king0() == rhs.king0() && king1() == rhs.king1(); }
		bool operator!=(const KK& rhs) { return !(*this == rhs); }

	private:
		Square king0_, king1_ ;
		bool inverse_sign;
	};

	// Output for debugging.
	static std::ostream& operator<<(std::ostream& os, KK rhs)
	{
		os << "KK(" << rhs.king0() << "," << rhs.king1() << ")";
		return os;
	}

		// Same as KK. For KKP.
	struct KKP : public SerializerBase
	{
	protected:
		KKP(Square king0, Square king1, Eval::BonaPiece p) : king0_(king0), king1_(king1), piece_(p), inverse_sign(false) {}
		KKP(Square king0, Square king1, Eval::BonaPiece p, bool inverse) : king0_(king0), king1_(king1), piece_(p),inverse_sign(inverse) {}
	public:
		KKP() {}

		virtual uint64_t size() const { return (uint64_t)max_king_sq_*(uint64_t)max_king_sq_*(uint64_t)fe_end_; }

		// builder that creates KKP object from index (serial number)
		KKP fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// A builder that creates a KKP object from raw_index (a number that starts from 0, not a serial number)
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

		// When you construct this object using fromIndex(), you can get information with the following accessors.
		Square king0() const { return king0_; }
		Square king1() const { return king1_; }
		Eval::BonaPiece piece() const { return piece_; }

		// Number of KKP dimension reductions
#if defined(USE_KKP_INVERSE_WRITE)
		#define KKP_LOWER_COUNT 4
#elif defined(USE_KKP_MIRROR_WRITE)
		#define KKP_LOWER_COUNT 2
#else
		#define KKP_LOWER_COUNT 1
#endif

#if defined(USE_KKP_INVERSE_WRITE) && !defined(USE_KKP_MIRROR_WRITE) 
		// USE_KKP_INVERSE_WRITE If you use it, please also define USE_KKP_MIRROR_WRITE.
		static_assert(false, "define also USE_KKP_MIRROR_WRITE!");
#endif

		// Get the index of the low-dimensional array. The mirrored one is returned to kkp_[1].
		// When USE_KKP_INVERSE_WRITE is enabled, the inverse of them will be in [2] and [3].
		// Note that the sign of grad must be reversed for this dimension reduction.
		// You can use is_inverse() because it can be determined.
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

		// Get the index when counting the value of min_index() of this class as 0.
		virtual uint64_t toRawIndex() const {
			return  ((uint64_t)king0_ * (uint64_t)max_king_sq_ + (uint64_t)king1_) * (uint64_t)fe_end_ + (uint64_t)piece_;
		}

		// Returns whether or not the dimension lowered with toLowerDimensions is inverse.
		bool is_inverse() const {
			return inverse_sign;
		}

		// When is_inverse() == true, reverse the sign that is not grad's turn and return it.
		template <typename T>
		std::array<T, 2> apply_inverse_sign(const std::array<T, 2>& rhs)
		{
			return !is_inverse() ? rhs : std::array<T, 2>{-rhs[0], rhs[1]};
		}

		// comparison operator
		bool operator==(const KKP& rhs) { return king0() == rhs.king0() && king1() == rhs.king1() && piece() == rhs.piece(); }
		bool operator!=(const KKP& rhs) { return !(*this == rhs); }

	private:
		Square king0_, king1_;
		Eval::BonaPiece piece_;
		bool inverse_sign;
	};

	// Output for debugging.
	static std::ostream& operator<<(std::ostream& os, KKP rhs)
	{
		os << "KKP(" << rhs.king0() << "," << rhs.king1() << "," << rhs.piece() << ")";
		return os;
	}


	// Same as KK and KKP. For KPP
	struct KPP : public SerializerBase
	{
	protected:
		KPP(Square king, Eval::BonaPiece p0, Eval::BonaPiece p1) : king_(king), piece0_(p0), piece1_(p1) {}

	public:
		KPP() {}

		// The minimum and maximum KPP values ​​of serial numbers when serializing KK, KKP, KPP arrays.
#if !defined(USE_TRIANGLE_WEIGHT_ARRAY)
		virtual uint64_t size() const { return (uint64_t)max_king_sq_*(uint64_t)fe_end_*(uint64_t)fe_end_; }
#else
		// Triangularize the square array part of [fe_end][fe_end] of kpp[SQUARE_NB][fe_end][fe_end].
		// If kpp[SQUARE_NB][triangle_fe_end], the first row of this triangular array has one element, the second row has two elements, and so on.
		// hence triangle_fe_end = 1 + 2 + .. + fe_end = fe_end * (fe_end + 1) / 2
		virtual uint64_t size() const { return (uint64_t)max_king_sq_*(uint64_t)triangle_fe_end; }
#endif

		virtual void set(int max_king_sq, uint64_t fe_end, uint64_t min_index)
		{
		// This value is used in size(), and size() is used in SerializerBase::set(), so calculate first.
			triangle_fe_end = (uint64_t)fe_end*((uint64_t)fe_end + 1) / 2;

			SerializerBase::set(max_king_sq, fe_end, min_index);
		}

		// builder that creates KPP object from index (serial number)
		KPP fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// A builder that creates KPP objects from raw_index (a number that starts from 0, not a serial number)
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

			// Write the expression to find piece0, piece1 from index2 here.
			// This is the inverse function of index2 = i * (i+1) / 2 + j.
			// If j = 0, i^2 + i-2 * index2 == 0
			// From the solution formula of the quadratic equation i = (sqrt(8*index2+1)-1) / 2.
			// After i is converted into an integer, j can be calculated as j = index2-i * (i + 1) / 2.

			// BonaPiece assumes 32bit (may not fit in 16bit), so this multiplication must be 64bit.
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

		// When you construct this object using fromIndex(), you can get information with the following accessors.
		Square king() const { return king_; }
		Eval::BonaPiece piece0() const { return piece0_; }
		Eval::BonaPiece piece1() const { return piece1_; }


// number of dimension reductions
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

		// Get the index of the low-dimensional array. The ones with p1 and p2 swapped, the ones mirrored, etc. are returned.
		void toLowerDimensions(/*out*/ KPP kpp_[KPP_LOWER_COUNT]) const {

#if defined(USE_TRIANGLE_WEIGHT_ARRAY)
			// Note that if you use a triangular array, the swapped piece0 and piece1 will not be returned.
			kpp_[0] = fromKPP(king_, piece0_, piece1_);
#if defined(USE_KPP_MIRROR_WRITE)
			kpp_[1] = fromKPP(Mir(king_), mir_piece(piece0_), mir_piece(piece1_));
#endif

#else
			// When not using triangular array
			kpp_[0] = fromKPP(king_, piece0_, piece1_);
			kpp_[1] = fromKPP(king_, piece1_, piece0_);
#if defined(USE_KPP_MIRROR_WRITE)
			kpp_[2] = fromKPP(Mir(king_), mir_piece(piece0_), mir_piece(piece1_));
			kpp_[3] = fromKPP(Mir(king_), mir_piece(piece1_), mir_piece(piece0_));
#endif
#endif
		}

		// Get the index when counting the value of min_index() of this class as 0.
		virtual uint64_t toRawIndex() const {

#if !defined(USE_TRIANGLE_WEIGHT_ARRAY)

			return ((uint64_t)king_ * (uint64_t)fe_end_ + (uint64_t)piece0_) * (uint64_t)fe_end_ + (uint64_t)piece1_;

#else
			// Macro similar to that used in Bonanza 6.0
			auto PcPcOnSq = [&](Square k, Eval::BonaPiece i, Eval::BonaPiece j)
			{

				// (i,j) in this triangular array is the element in the i-th row and the j-th column.
				// 1st row + 2 + ... + i = i * (i+1) / 2 because the i-th row and 0th column is the total of the elements up to that point
				// The i-th row and the j-th column is j plus this. i*(i+1)/2+j

				// BonaPiece type is assumed to be 32 bits, so if you do not pay attention to multiplication, it will overflow.
				return (uint64_t)k * triangle_fe_end + (uint64_t)(uint64_t(i)*(uint64_t(i)+1) / 2 + uint64_t(j));
			};

			auto k = king_;
			auto i = piece0_;
			auto j = piece1_;

			return (i >= j) ? PcPcOnSq(k, i, j) : PcPcOnSq(k, j, i);
#endif
		}

		// Returns whether or not the dimension lowered with toLowerDimensions is inverse.
		// Prepared to match KK, KKP and interface. This method always returns false for this KPP class.
		bool is_inverse() const {
			return false;
		}

		// comparison operator
		bool operator==(const KPP& rhs) {
			return king() == rhs.king() &&
				((piece0() == rhs.piece0() && piece1() == rhs.piece1())
#if defined(USE_TRIANGLE_WEIGHT_ARRAY)
					// When using a triangular array, allow swapping of piece0 and piece1.
				|| (piece0() == rhs.piece1() && piece1() == rhs.piece0())
#endif
					); }
		bool operator!=(const KPP& rhs) { return !(*this == rhs); }


	private:
		Square king_;
		Eval::BonaPiece piece0_, piece1_;

		uint64_t triangle_fe_end; // = (uint64_t)fe_end_*((uint64_t)fe_end_ + 1) / 2;
	};

	// Output for debugging.
	static std::ostream& operator<<(std::ostream& os, KPP rhs)
	{
		os << "KPP(" << rhs.king() << "," << rhs.piece0() << "," << rhs.piece1() << ")";
		return os;
	}

	// 4 pieces related to KPPP. However, if there is a turn and you do not consider mirrors etc., memory of 2 TB or more is required for learning.
	// Even if you use a triangular array, you need 50GB x 12 bytes = 600GB for learning.
	// It takes about half as much as storing only the mirrored one.
	// Here, the triangular array is always used and the mirrored one is stored.
	//
	// Also, king() of this class is not limited to Square of the actual king, but a value from 0 to (king_sq-1) is simply returned.
	// This needs to be converted to an appropriate ball position on the user side when performing compression using a mirror.
	//
	// Later, regarding the pieces0,1,2 returned by this class,
	// piece0() >piece1() >piece2()
	// It is, and it is necessary to keep this constraint when passing piece0,1,2 in the constructor.
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

		// Set fe_end and king_sq.
		// fe_end: fe_end assumed by this KPPP class
		// king_sq: Number of balls to handle in KPPP.
		// 3 layers x 3 mirrors = 3 layers x 5 lines = 15
		// 2 steps x 2 mirrors without mirror = 18
		// Set this first using set() on the side that uses this KPPP class.
		virtual void set(int max_king_sq, uint64_t fe_end,uint64_t min_index) {
			// This value is used in size(), and size() is used in SerializerBase::set(), so calculate first.
			triangle_fe_end = fe_end * (fe_end - 1) * (fe_end - 2) / 6;

			SerializerBase::set(max_king_sq, fe_end, min_index);
		}

		// number of dimension reductions
		// For the time being, the dimension reduction of the mirror is not supported. I wonder if I'll do it here...
/*
#if defined(USE_KPPP_MIRROR_WRITE)
#define KPPP_LOWER_COUNT 2
#else
#define KPPP_LOWER_COUNT 1
#endif
*/
#define KPPP_LOWER_COUNT 1

		// Get the index of the low-dimensional array.
		// Note that the one with p0,p1,p2 swapped will not be returned.
		// Also, the mirrored one is returned only when USE_KPPP_MIRROR_WRITE is enabled.
		void toLowerDimensions(/*out*/ KPPP kppp_[KPPP_LOWER_COUNT]) const
		{
			kppp_[0] = fromKPPP(king_, piece0_, piece1_,piece2_);
#if KPPP_LOWER_COUNT > 1
			// If mir_piece is done, it will be in a state not sorted. Need code to sort.
			Eval::BonaPiece p_list[3] = { mir_piece(piece2_), mir_piece(piece1_), mir_piece(piece0_) };
			my_insertion_sort(p_list, 0, 3);
			kppp_[1] = fromKPPP((int)Mir((Square)king_), p_list[2] , p_list[1], p_list[0]);
#endif
		}

		// builder that creates KPPP object from index (serial number)
		KPPP fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// A builder that creates KPPP objects from raw_index (a number that starts from 0, not a serial number)
		KPPP fromRawIndex(uint64_t raw_index) const
		{
			uint64_t index2 = raw_index % triangle_fe_end;

			// Write the expression to find piece0, piece1, piece2 from index2 here.
			// This is the inverse function of index2 = i(i-1)(i-2)/6-1 + j(j+1)/2 + k.
			// For j = k = 0, the real root is i = ... from the solution formula of the cubic equation. (The following formula)
			// However, if index2 is 0 or 1, there are multiple real solutions. You have to consider this. It is necessary to take measures against insufficient calculation accuracy.
			// After i is calculated, i can be converted into an integer, then put in the first expression and then j can be calculated in the same way as in KPP.

			// This process is a relatively difficult numerical calculation. Various ideas are needed.

			int piece0;
			if (index2 <= 1)
			{
				// There are multiple real solutions only when index2 == 0,1.
				piece0 = (int)index2 + 2;

			} else {

				//double t = pow(sqrt((243 *index2 * index2-1) * 3) + 27 * index2, 1.0 / 3);
				// → In this case, the content of sqrt() will overflow if index2 becomes large.

				// Since the contents of sqrt() overflow, do not multiply 3.0 in sqrt, but multiply sqrt(3.0) outside sqrt.
				// Since the contents of sqrt() will overflow, use an approximate expression when index2 is large.

				double t;
				
				if (index2 < 100000000)
					t = pow(sqrt((243.0 *index2 * index2 - 1)) * sqrt(3.0) + 27 * index2, 1.0 / 3);
				else
					// If index2 is very large, we can think of the contents of sqrt as approximately √243 * index2.
					t = pow( index2 * sqrt(243 * 3.0) + 27 * index2, 1.0 / 3);

				// Add deltas to avoid a slight calculation error when rounding.
				// If it is too large, it may increase by 1 so adjustment is necessary.

				const double delta = 0.000000001;

				piece0 = int(t / pow(3.0, 2.0 / 3) + 1.0 / (pow(3.0, 1.0 / 3) * t) + delta) + 1;
				// Uuu. Is it really like this? ('Ω`)
			}

			//Since piece2 is obtained, substitute piece2 for i of i(i-1)(i-2)/6 (=a) in the above formula. Also substitute k = 0.
			// j(j+1)/2 = index2-a
			// This is from the solution formula of the quadratic equation..

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

			// Propagate king_sq and fe_end.
			return fromKPPP((Square)king, (Eval::BonaPiece)piece0, (Eval::BonaPiece)piece1 , (Eval::BonaPiece)piece2);
		}

		// Specify k,p0,p1,p2 to build KPPP instance.
		// The king_sq and fe_end passed by set() which is internally retained are inherited.
		KPPP fromKPPP(int king, Eval::BonaPiece p0, Eval::BonaPiece p1, Eval::BonaPiece p2) const
		{
			KPPP kppp(king, p0, p1, p2);
			kppp.set(max_king_sq_, fe_end_,min_index());
			return kppp;
		}

		// Get the index when counting the value of min_index() of this class as 0.
		virtual uint64_t toRawIndex() const {

			// Macro similar to the one used in Bonanza 6.0
			// Precondition) i> j> k.
			// NG in case of i==j,j==k.
			auto PcPcPcOnSq = [this](int king, Eval::BonaPiece i, Eval::BonaPiece j , Eval::BonaPiece k)
			{
				// (i,j,k) in this triangular array is the element in the i-th row and the j-th column.
				// 0th row 0th column 0th is the sum of the elements up to that point, so 0 + 0 + 1 + 3 + 6 + ... + (i)*(i-1)/2 = i*( i-1)*(i-2)/6
				// i-th row, j-th column, 0-th is j with j added. + j*(j-1) / 2
				// i-th row, j-th column and k-th row is k plus it. + k
				assert(i > j && j > k);

				// BonaPiece type is assumed to be 32 bits, so if you do not pay attention to multiplication, it will overflow.
				return (uint64_t)king * triangle_fe_end + (uint64_t)(
						  uint64_t(i)*(uint64_t(i) - 1) * (uint64_t(i) - 2) / 6
						+ uint64_t(j)*(uint64_t(j) - 1) / 2
						+ uint64_t(k)
					);
			};

			return PcPcPcOnSq(king_, piece0_, piece1_, piece2_);
		}

		// When you construct this object using fromIndex(), you can get information with the following accessors.
		int king() const { return king_; }
		Eval::BonaPiece piece0() const { return piece0_; }
		Eval::BonaPiece piece1() const { return piece1_; }
		Eval::BonaPiece piece2() const { return piece2_; }
		// Returns whether or not the dimension lowered with toLowerDimensions is inverse.
		// Prepared to match KK, KKP and interface. This method always returns false for this KPPP class.
		bool is_inverse() const {
			return false;
		}

		// Returns the number of elements in a triangular array. It is assumed that the kppp array is the following two-dimensional array.
		// kppp[king_sq][triangle_fe_end];
		uint64_t get_triangle_fe_end() const { return triangle_fe_end; }

		// comparison operator
		bool operator==(const KPPP& rhs) {
			// piece0> piece1> piece2 is assumed, so there is no possibility of replacement.
			return king() == rhs.king() && piece0() == rhs.piece0() && piece1() == rhs.piece1() && piece2() == rhs.piece2();
		}
		bool operator!=(const KPPP& rhs) { return !(*this == rhs); }

	private:

		int king_;
		Eval::BonaPiece piece0_, piece1_,piece2_;

		// The part of the square array of [fe_end][fe_end][fe_end] of kppp[king_sq][fe_end][fe_end][fe_end] is made into a triangular array.
		// If kppp[king_sq][triangle_fe_end], the number of elements from the 0th row of this triangular array is 0,0,1,3,..., The nth row is n(n-1)/2.
		// therefore,
		// triangle_fe_end = Σn(n-1)/2 , n=0..fe_end-1
		//                 =  fe_end * (fe_end - 1) * (fe_end - 2) / 6
		uint64_t triangle_fe_end; // ((uint64_t)Eval::fe_end)*((uint64_t)Eval::fe_end - 1)*((uint64_t)Eval::fe_end - 2) / 6;
	};

	// Output for debugging.
	static std::ostream& operator<<(std::ostream& os, KPPP rhs)
	{
		os << "KPPP(" << rhs.king() << "," << rhs.piece0() << "," << rhs.piece1() << "," << rhs.piece2() << ")";
		return os;
	}

	// For learning about 4 pieces by KKPP.
	//
	// Same design as KPPP class. In KPPP class, treat as one with less p.
	// The positions of the two balls are encoded as values ​​from 0 to king_sq-1.
	//
	// Later, regarding the pieces0 and 1 returned by this class,
	// piece0() >piece1()
	// It is, and it is necessary to keep this constraint even when passing piece0,1 in the constructor.
	//
	// Due to this constraint, BonaPieceZero cannot be assigned to piece0 and piece1 at the same time and passed.
	// If you want to support learning of dropped frames, you need to devise with evaluate().
	struct KKPP: SerializerBase
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

		// Set fe_end and king_sq.
		// fe_end: fe_end assumed by this KPPP class
		// king_sq: Number of balls to handle in KPPP.
		// 9 steps x mirrors 9 steps x 5 squared squares (balls before and after) = 45*45 = 2025.
		// Set this first using set() on the side that uses this KKPP class.
		void set(int max_king_sq, uint64_t fe_end , uint64_t min_index) {
			// This value is used in size(), and size() is used in SerializerBase::set(), so calculate first.
			triangle_fe_end = fe_end * (fe_end - 1) / 2;

			SerializerBase::set(max_king_sq, fe_end, min_index);
		}

		// number of dimension reductions
		// For the time being, the dimension reduction of the mirror is not supported. I wonder if I'll do it here... (Because the memory for learning is a waste)
#define KKPP_LOWER_COUNT 1

		// Get the index of the low-dimensional array.
		//Note that the one with p0,p1,p2 swapped will not be returned.
		// Also, the mirrored one is returned only when USE_KPPP_MIRROR_WRITE is enabled.
		void toLowerDimensions(/*out*/ KKPP kkpp_[KPPP_LOWER_COUNT]) const
		{
			kkpp_[0] = fromKKPP(king_, piece0_, piece1_);

			// When mirroring, mir_piece will not be sorted. Need code to sort.
			// We also need to define a mirror for king_.
		}

		// builder that creates KKPP object from index (serial number)
		KKPP fromIndex(uint64_t index) const { assert(index >= min_index()); return fromRawIndex(index - min_index()); }

		// builder that creates KKPP object from raw_index (number starting from 0, not serial number)
		KKPP fromRawIndex(uint64_t raw_index) const
		{
			uint64_t index2 = raw_index % triangle_fe_end;

			// Write the expression to find piece0, piece1, piece2 from index2 here.
			// This is the inverse function of index2 = i(i-1)/2 + j.
			// Use the formula of the solution of the quadratic equation with j=0.
			// When index2=0, it is a double root, but the smaller one does not satisfy i>j and is ignored.

			int piece0 = (int(sqrt(8 * index2 + 1)) + 1)/2;
			int piece1 = int(index2 - piece0 * (piece0 - 1) /2 );

			assert(piece0 > piece1);

			assert(piece1 < (int)fe_end_);
			assert(piece0 < (int)fe_end_);

			raw_index /= triangle_fe_end;

			int king = (int)(raw_index  /* % SQUARE_NB */);
			assert(king < max_king_sq_);

			// Propagate king_sq and fe_end.
			return fromKKPP(king, (Eval::BonaPiece)piece0, (Eval::BonaPiece)piece1);
		}

		// Specify k,p0,p1 to build KKPP instance.
		// The king_sq and fe_end passed by set() which is internally retained are inherited.
		KKPP fromKKPP(int king, Eval::BonaPiece p0, Eval::BonaPiece p1) const
		{
			KKPP kkpp(king, p0, p1);
			kkpp.set(max_king_sq_, fe_end_,min_index());
			return kkpp;
		}

		// Get the index when counting the value of min_index() of this class as 0.
		virtual uint64_t toRawIndex() const {

			// Macro similar to the one used in Bonanza 6.0
			// Precondition) i> j.
			// NG in case of i==j,j==k.
			auto PcPcOnSq = [this](int king, Eval::BonaPiece i, Eval::BonaPiece j)
			{
				assert(i > j);

				// BonaPiece type is assumed to be 32 bits, so if you do not pay attention to multiplication, it will overflow.
				return (uint64_t)king * triangle_fe_end + (uint64_t)(
					+ uint64_t(i)*(uint64_t(i) - 1) / 2
					+ uint64_t(j)
					);
			};

			return PcPcOnSq(king_, piece0_, piece1_);
		}

		// When you construct this object using fromIndex(), fromKKPP(), you can get information with the following accessors.
		int king() const { return king_; }
		Eval::BonaPiece piece0() const { return piece0_; }
		Eval::BonaPiece piece1() const { return piece1_; }

		// Returns whether or not the dimension lowered with toLowerDimensions is inverse.
		// Prepared to match KK, KKP and interface. In this KKPP class, this method always returns false.
		bool is_inverse() const {
			return false;
		}

		//Returns the number of elements in a triangular array. It is assumed that the kkpp array is the following two-dimensional array.
		//   kkpp[king_sq][triangle_fe_end];
		uint64_t get_triangle_fe_end() const { return triangle_fe_end; }

		// comparison operator
		bool operator==(const KKPP& rhs) {
			// Since piece0> piece1 is assumed, there is no possibility of replacement.
			return king() == rhs.king() && piece0() == rhs.piece0() && piece1() == rhs.piece1();
		}
		bool operator!=(const KKPP& rhs) { return !(*this == rhs); }

	private:

		int king_;
		Eval::BonaPiece piece0_, piece1_;

		// Triangularize the square array part of [fe_end][fe_end] of kppp[king_sq][fe_end][fe_end].
		uint64_t triangle_fe_end = 0;
		
	};

	// Output for debugging.
	static std::ostream& operator<<(std::ostream& os, KKPP rhs)
	{
		os << "KKPP(" << rhs.king() << "," << rhs.piece0() << "," << rhs.piece1() << ")";
		return os;
	}


}

#endif // defined (EVAL_LEARN)
#endif
