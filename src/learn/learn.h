#ifndef _LEARN_H_
#define _LEARN_H_

#if defined(EVAL_LEARN)

// ----------------------
// Floating point for learning
// ----------------------

// If this is set to double, the calculation accuracy will be higher, but the weight array entangled memory will be doubled.
// Currently, if this is float, the weight array is 4.5 times the size of the evaluation function file. (About 4.5GB with KPPT)
// Even if it is a double type, there is almost no difference in the way of convergence, so fix it to float.

// when using float
using LearnFloatType = float;

// when using double
//typedef double LearnFloatType;

// when using float16
//#include "half_float.h"
//typedef HalfFloat::float16 LearnFloatType;

// ======================
// configure
// ======================

// ----------------------
// Learning with the method of elmo (WCSC27)
// ----------------------

#define LOSS_FUNCTION "ELMO_METHOD(WCSC27)"

// ----------------------
// Definition of struct used in Learner
// ----------------------

#include "packed_sfen.h"

#include "position.h"

#include <sstream>
#include <vector>

namespace Learner
{
    // ----------------------
    // Settings for learning
    // ----------------------

    // mini-batch size.
    // Calculate the gradient by combining this number of phases.
    // If you make it smaller, the number of update_weights() will increase and the convergence will be faster. The gradient is incorrect.
    // If you increase it, the number of update_weights() decreases, so the convergence will be slow. The slope will come out accurately.
    // I don't think you need to change this value in most cases.

    constexpr std::size_t LEARN_MINI_BATCH_SIZE = 1000 * 1000 * 1;

    // The number of phases to read from the file at one time. After reading this much, shuffle.
    // It is better to have a certain size, but this number x 40 bytes x 3 times as much memory is consumed. 400MB*3 is consumed in the 10M phase.
    // Must be a multiple of THREAD_BUFFER_SIZE(=10000).

    constexpr std::size_t LEARN_SFEN_READ_SIZE = 1000 * 1000 * 10;

    // Saving interval of evaluation function at learning. Save each time you learn this number of phases.
    // Needless to say, the longer the saving interval, the shorter the learning time.
    // Folder name is incremented for each save like 0/, 1/, 2/...
    // By default, once every 1 billion phases.
    constexpr std::size_t LEARN_EVAL_SAVE_INTERVAL = 1000000000ULL;

    // Reduce the output of rmse during learning to 1 for this number of times.
    // rmse calculation is done in one thread, so it takes some time, so reducing the output is effective.
    constexpr std::size_t LEARN_RMSE_OUTPUT_INTERVAL = 1;

    double calc_grad(Value shallow, const PackedSfenValue& psv);

    // Learning from the generated game record
    void learn(Position& pos, std::istringstream& is);
}

#endif

#endif // ifndef _LEARN_H_
