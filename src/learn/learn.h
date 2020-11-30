#ifndef _LEARN_H_
#define _LEARN_H_

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

#include "autograd.h"
#include "packed_sfen.h"

#include "position.h"

#include <sstream>
#include <vector>
#include <mutex>
#include <string>

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

    // Saving interval of evaluation function at learning. Save each time you learn this number of phases.
    // Needless to say, the longer the saving interval, the shorter the learning time.
    // Folder name is incremented for each save like 0/, 1/, 2/...
    // By default, once every 1 billion phases.
    constexpr std::size_t LEARN_EVAL_SAVE_INTERVAL = 100'000'000ULL;

    // Reduce the output of rmse during learning to 1 for this number of times.
    // rmse calculation is done in one thread, so it takes some time, so reducing the output is effective.
    constexpr std::size_t LEARN_RMSE_OUTPUT_INTERVAL = 1;

    // Learning from the generated game record
    void learn(std::istringstream& is);

    using CalcLossFunc = ValueWithGrad<double>(Value, Value, int, int);

    struct Loss
    {
        double value() const
        {
            return m_loss.value;
        }

        double grad() const
        {
            return m_loss.grad;
        }

        uint64_t count() const
        {
            return m_count;
        }

        Loss() = default;

        Loss(const Loss& other) :
            m_loss(other.m_loss),
            m_count(other.m_count)
        {
        }

        Loss& operator += (const ValueWithGrad<double>& rhs)
        {
            std::unique_lock lock(m_mutex);

            m_loss += rhs.abs();
            m_count += 1;

            return *this;
        }

        Loss& operator += (const Loss& rhs)
        {
            std::unique_lock lock(m_mutex);

            m_loss += rhs.m_loss.abs();
            m_count += rhs.m_count;

            return *this;
        }

        void reset()
        {
            std::unique_lock lock(m_mutex);

            m_loss = ValueWithGrad<double>{ 0.0, 0.0 };
            m_count = 0;
        }

        template <typename StreamT>
        void print_with_grad(const std::string& prefix, StreamT& s) const
        {
            s << "  - " << prefix << "_loss       = " << m_loss.value / (double)m_count << std::endl;
            s << "  - " << prefix << "_grad_norm  = " << m_loss.grad / (double)m_count << std::endl;
        }

        template <typename StreamT>
        void print_only_loss(const std::string& prefix, StreamT& s) const
        {
            s << "  - " << prefix << "_loss       = " << m_loss.value / (double)m_count << std::endl;
        }

    private:
        ValueWithGrad<double> m_loss{ 0.0, 0.0 };
        uint64_t m_count{0};
        std::mutex m_mutex;
    };
}

#endif // ifndef _LEARN_H_
