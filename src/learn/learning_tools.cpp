#include "learning_tools.h"

#if defined (EVAL_LEARN)

#include "../misc.h"

using namespace Eval;

namespace EvalLearningTools
{

	// --- static variables

	double Weight::eta;
	double Weight::eta1;
	double Weight::eta2;
	double Weight::eta3;
	uint64_t Weight::eta1_epoch;
	uint64_t Weight::eta2_epoch;
}

#endif
