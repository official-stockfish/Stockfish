#ifndef _LEARN_H_
#define _LEARN_H_

#if defined(EVAL_LEARN)

#include <vector>

// =====================
// Settings for learning
// =====================

// If you select one of the following, the details after that will be automatically selected.
// If you don't select any of them, you need to set the subsequent details one by one.

// Learning setting by elmo method. This is the default setting.
// To make a standard squeeze diaphragm, specify "lambda 1" with the learn command.
#define LEARN_ELMO_METHOD


// ----------------------
// update formula
// ----------------------

// Ada Grad. Recommended because it is stable.
// #define ADA_GRAD_UPDATE

// SGD looking only at the sign of the gradient. It requires less memory, but the accuracy is...
// #define SGD_UPDATE

// ----------------------
// Settings for learning
// ----------------------

// mini-batch size.
// Calculate the gradient by combining this number of phases.
// If you make it smaller, the number of update_weights() will increase and the convergence will be faster. The gradient is incorrect.
// If you increase it, the number of update_weights() decreases, so the convergence will be slow. The slope will come out accurately.
// I don't think you need to change this value in most cases.

#define LEARN_MINI_BATCH_SIZE (1000 * 1000 * 1)

// The number of phases to read from the file at one time. After reading this much, shuffle.
// It is better to have a certain size, but this number x 40 bytes x 3 times as much memory is consumed. 400MB*3 is consumed in the 10M phase.
// Must be a multiple of THREAD_BUFFER_SIZE(=10000).

#define LEARN_SFEN_READ_SIZE (1000 * 1000 * 10)

// Saving interval of evaluation function at learning. Save each time you learn this number of phases.
// Needless to say, the longer the saving interval, the shorter the learning time.
// Folder name is incremented for each save like 0/, 1/, 2/...
// By default, once every 1 billion phases.
#define LEARN_EVAL_SAVE_INTERVAL (1000000000ULL)


// ----------------------
// Select the objective function
// ----------------------

// The objective function is the sum of squares of the difference in winning percentage
// See learner.cpp for more information.

//#define LOSS_FUNCTION_IS_WINNING_PERCENTAGE

// Objective function is cross entropy
// See learner.cpp for more information.
// So-called ordinary "rag cloth squeezer"
//#define LOSS_FUNCTION_IS_CROSS_ENTOROPY

// A version in which the objective function is cross entropy, but the win rate function is not passed
// #define LOSS_FUNCTION_IS_CROSS_ENTOROPY_FOR_VALUE

// elmo (WCSC27) method
// #define LOSS_FUNCTION_IS_ELMO_METHOD

// ※ Other things may be added.


// ----------------------
// debug settings for learning
// ----------------------

// Reduce the output of rmse during learning to 1 for this number of times.
// rmse calculation is done in one thread, so it takes some time, so reducing the output is effective.
#define LEARN_RMSE_OUTPUT_INTERVAL 1


// ----------------------
// learning from zero vector
// ----------------------

// Start learning the evaluation function parameters from the zero vector.
// Initialize to zero, generate a game, learn from zero vector,
// Game generation → If you repeat learning, you will get parameters that do not depend on the professional game. (maybe)
// (very time consuming)

//#define RESET_TO_ZERO_VECTOR


// ----------------------
// Floating point for learning
// ----------------------

// If this is set to double, the calculation accuracy will be higher, but the weight array entangled memory will be doubled.
// Currently, if this is float, the weight array is 4.5 times the size of the evaluation function file. (About 4.5GB with KPPT)
// Even if it is a double type, there is almost no difference in the way of convergence, so fix it to float.

// when using float
typedef float LearnFloatType;

// when using double
//typedef double LearnFloatType;

// when using float16
//#include "half_float.h"
//typedef HalfFloat::float16 LearnFloatType;

// ----------------------
// save memory
// ----------------------

// Use a triangular array for the Weight array (of which is KPP) to save memory.
// If this is used, the weight array for learning will be about 3 times as large as the evaluation function file.

#define USE_TRIANGLE_WEIGHT_ARRAY

// ----------------------
// dimension down
// ----------------------

// Dimension reduction for mirrors (left/right symmetry) and inverse (forward/backward symmetry).
// All on by default.

// Dimension reduction using mirror and inverse for KK. (Unclear effect)
// USE_KK_MIRROR_WRITE must be on when USE_KK_INVERSE_WRITE is on.
#define USE_KK_MIRROR_WRITE
#define USE_KK_INVERSE_WRITE

// Dimension reduction using Mirror and Inverse for KKP. (Inverse is not so effective)
// When USE_KKP_INVERSE_WRITE is turned on, USE_KKP_MIRROR_WRITE must also be turned on.
#define USE_KKP_MIRROR_WRITE
#define USE_KKP_INVERSE_WRITE

// Perform dimension reduction using a mirror for KPP. (Turning this off requires double the teacher position)
// KPP has no inverse. (Because there is only K on the front side)
#define USE_KPP_MIRROR_WRITE

// Perform a dimension reduction using a mirror for KPPP. (Turning this off requires double the teacher position)
// KPPP has no inverse. (Because there is only K on the front side)
#define USE_KPPP_MIRROR_WRITE

// Reduce the dimension by KPP for learning the KKPP component.
// Learning is very slow.
// Do not use as it is not debugged.
//#define USE_KKPP_LOWER_DIM


// ======================
// Settings for creating teacher phases
// ======================

// ----------------------
// write out the draw
// ----------------------

// When you reach a draw, write it out as a teacher position
// It's subtle whether it's better to do this.
// #define LEARN_GENSFEN_USE_DRAW_RESULT


// ======================
// configure
// ======================

// ----------------------
// Learning with the method of elmo (WCSC27)
// ----------------------

#if defined( LEARN_ELMO_METHOD )
#define LOSS_FUNCTION_IS_ELMO_METHOD
#define ADA_GRAD_UPDATE
#endif


// ----------------------
// Definition of struct used in Learner
// ----------------------
#include "../position.h"

namespace Learner
{
	//Structure in which PackedSfen and evaluation value are integrated
	// If you write different contents for each option, it will be a problem when reusing the teacher game
	// For the time being, write all the following members regardless of the options.
	struct PackedSfenValue
	{
		// phase
		PackedSfen sfen;

		// Evaluation value returned from Learner::search()
		int16_t score;

		// PV first move
		// Used when finding the match rate with the teacher
		uint16_t move;

		// Trouble of the phase from the initial phase.
		uint16_t gamePly;

		// 1 if the player on this side ultimately wins the game. -1 if you are losing.
		// 0 if a draw is reached.
		// The draw is in the teacher position generation command gensfen,
		// Only write if LEARN_GENSFEN_DRAW_RESULT is enabled.
		int8_t game_result;

		// When exchanging the file that wrote the teacher aspect with other people
		//Because this structure size is not fixed, pad it so that it is 40 bytes in any environment.
		uint8_t padding;

		// 32 + 2 + 2 + 2 + 1 + 1 = 40bytes
	};

	// Type that returns the reading line and the evaluation value at that time
	// Used in Learner::search(), Learner::qsearch().
	typedef std::pair<Value, std::vector<Move> > ValueAndPV;

	// So far, only Yaneura King 2018 Otafuku has this stub
	// This stub is required if EVAL_LEARN is defined.
	extern Learner::ValueAndPV  search(Position& pos, int depth , size_t multiPV = 1 , uint64_t NodesLimit = 0);
	extern Learner::ValueAndPV qsearch(Position& pos);

	double calc_grad(Value shallow, const PackedSfenValue& psv);

}

#endif

#endif // ifndef _LEARN_H_
