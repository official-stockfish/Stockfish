#ifndef __LEARN_WEIGHT_H__
#define __LEARN_WEIGHT_H__

// A set of machine learning tools related to the weight array used for machine learning of evaluation functions

#include "learn.h"

#if defined (EVAL_LEARN)

#include "../misc.h"  // PRNG , my_insertion_sort

#include <array>
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
