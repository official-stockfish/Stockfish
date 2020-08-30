#ifndef __LEARN_WEIGHT_H__
#define __LEARN_WEIGHT_H__

// A set of machine learning tools related to the weight array used for machine learning of evaluation functions

#include "learn.h"
#if defined (EVAL_LEARN)
#include <array>

#if defined(SGD_UPDATE) || defined(USE_KPPP_MIRROR_WRITE)
#include "../misc.h"  // PRNG , my_insertion_sort
#endif

#include <cmath>	// std::sqrt()

namespace EvalLearningTools
{
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
}

#endif // defined (EVAL_LEARN)
#endif
