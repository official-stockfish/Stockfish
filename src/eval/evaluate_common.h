#ifndef _EVALUATE_COMMON_H_
#define _EVALUATE_COMMON_H_

#include <functional>

#include "../position.h"

namespace Eval
{

#if defined(USE_EVAL_HASH)
	// prefetch function
	void prefetch_evalhash(const Key key);
#endif

	// An operator that applies the function f to each parameter of the evaluation function.
	// Used for parameter analysis etc.
	// type indicates the survey target.
	// type = -1 :KK,KKP,KPP all
	// type = 0: KK only
	// type = 1: KKP only
	// type = 2: KPP only
	void foreach_eval_param(std::function<void(int32_t, int32_t)>f, int type = -1);

	// --------------------------
	// for learning
	// --------------------------

#if defined(EVAL_LEARN)
	// Initialize the gradient array during learning
	// Pass the learning rate as an argument. If 0.0, the default value is used.
	// The epoch of update_weights() gradually changes from eta to eta2 until eta_epoch.
	// After eta2_epoch, gradually change from eta2 to eta3.
	void init_grad(double eta1, uint64_t eta_epoch, double eta2, uint64_t eta2_epoch, double eta3);

	// Add the gradient difference value to the gradient array for all features that appear in the current phase.
	void add_grad(Position& pos, Color rootColor, double delt_grad);

	// Do SGD or AdaGrad or something based on the current gradient.
	// epoch: Generation counter (starting from 0)
	void update_weights(uint64_t epoch);

	// Save the evaluation function parameters to a file.
	// You can specify the extension added to the end of the file.
	void save_eval(std::string suffix);

	// Get the current eta.
	double get_eta();

	// --learning related commands

	// A function that normalizes KK. Note that it is not completely equivalent to the original evaluation function.
	// By making the values ​​of kkp and kpp as close to zero as possible, the value of the feature factor (which is zero) that did not appear during learning
	// The idea of ​​ensuring it is valid.
	void regularize_kk();

#endif


}

#endif // _EVALUATE_KPPT_COMMON_H_
