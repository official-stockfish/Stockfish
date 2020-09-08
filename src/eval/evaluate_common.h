#ifndef _EVALUATE_COMMON_H_
#define _EVALUATE_COMMON_H_

#if defined(EVAL_LEARN)

// A common header-like function for modern evaluation functions (EVAL_KPPT and EVAL_KPP_KKPT).

#include <string>

namespace Eval
{
	// --------------------------
	// for learning
	// --------------------------

	// Save the evaluation function parameters to a file.
	// You can specify the extension added to the end of the file.
	void save_eval(std::string suffix);

	// Get the current eta.
	double get_eta();
}

#endif // defined(EVAL_LEARN)

#endif // _EVALUATE_KPPT_COMMON_H_
