#ifndef LEARNER_AUTOGRAD_H
#define LEARNER_AUTOGRAD_H

#include <cmath>
#include <utility>
#include <type_traits>
#include <memory>
#include <tuple>

namespace Learner::Autograd::UnivariateStatic
{

    template <typename T>
    struct Identity
    {
        using type = T;
    };

    template <typename T>
    using Id = typename Identity<T>::type;

    template <typename T, int I>
    struct VariableParameter
    {
        using ValueType = T;

        VariableParameter()
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>& args) const
        {
            return std::get<I>(args);
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>&) const
        {
            return T(1.0);
        }
    };

    template <typename T, int I>
    struct ConstantParameter
    {
        using ValueType = T;

        ConstantParameter()
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>& args) const
        {
            return std::get<I>(args);
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>&) const
        {
            return T(0.0);
        }
    };

    template <typename T>
    struct Constant
    {
        using ValueType = T;

        Constant(T x) :
            m_x(std::move(x))
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>&) const
        {
            return m_x;
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>&) const
        {
            return T(0.0);
        }

    private:
        T m_x;
    };

    template <typename LhsT, typename RhsT, typename T = typename LhsT::ValueType>
    struct Sum
    {
        using ValueType = T;

        Sum(LhsT lhs, RhsT rhs) :
            m_lhs(std::move(lhs)),
            m_rhs(std::move(rhs))
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.value(args) + m_rhs.value(args);
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.grad(args) + m_rhs.grad(args);
        }

    private:
        LhsT m_lhs;
        RhsT m_rhs;
    };

    template <typename LhsT, typename RhsT, typename T = typename LhsT::ValueType>
    auto operator+(LhsT lhs, RhsT rhs)
    {
        return Sum(std::move(lhs), std::move(rhs));
    }

    template <typename LhsT, typename T = typename LhsT::ValueType>
    auto operator+(LhsT lhs, Id<T> rhs)
    {
        return Sum(std::move(lhs), Constant(rhs));
    }

    template <typename RhsT, typename T = typename RhsT::ValueType>
    auto operator+(Id<T> lhs, RhsT rhs)
    {
        return Sum(Constant(lhs), std::move(rhs));
    }

    template <typename LhsT, typename RhsT, typename T = typename LhsT::ValueType>
    struct Difference
    {
        using ValueType = T;

        Difference(LhsT lhs, RhsT rhs) :
            m_lhs(std::move(lhs)),
            m_rhs(std::move(rhs))
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.value(args) - m_rhs.value(args);
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.grad(args) - m_rhs.grad(args);
        }

    private:
        LhsT m_lhs;
        RhsT m_rhs;
    };

    template <typename LhsT, typename RhsT, typename T = typename LhsT::ValueType>
    auto operator-(LhsT lhs, RhsT rhs)
    {
        return Difference(std::move(lhs), std::move(rhs));
    }

    template <typename LhsT, typename T = typename LhsT::ValueType>
    auto operator-(LhsT lhs, Id<T> rhs)
    {
        return Difference(std::move(lhs), Constant(rhs));
    }

    template <typename RhsT, typename T = typename RhsT::ValueType>
    auto operator-(Id<T> lhs, RhsT rhs)
    {
        return Difference(Constant(lhs), std::move(rhs));
    }

    template <typename LhsT, typename RhsT, typename T = typename LhsT::ValueType>
    struct Product
    {
        using ValueType = T;

        Product(LhsT lhs, RhsT rhs) :
            m_lhs(std::move(lhs)),
            m_rhs(std::move(rhs))
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.value(args) * m_rhs.value(args);
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.grad(args) * m_rhs.value(args) + m_lhs.value(args) * m_rhs.grad(args);
        }

    private:
        LhsT m_lhs;
        RhsT m_rhs;
    };

    template <typename LhsT, typename RhsT, typename T = typename LhsT::ValueType>
    auto operator*(LhsT lhs, RhsT rhs)
    {
        return Product(std::move(lhs), std::move(rhs));
    }

    template <typename LhsT, typename T = typename LhsT::ValueType>
    auto operator*(LhsT lhs, Id<T> rhs)
    {
        return Product(std::move(lhs), Constant(rhs));
    }

    template <typename RhsT, typename T = typename RhsT::ValueType>
    auto operator*(Id<T> lhs, RhsT rhs)
    {
        return Product(Constant(lhs), std::move(rhs));
    }

    template <typename ArgT, typename T = typename ArgT::ValueType>
    struct Sigmoid
    {
        using ValueType = T;

        explicit Sigmoid(ArgT x) :
            m_x(std::move(x))
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>& args) const
        {
            return value_(m_x.value(args));
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_x.grad(args) * grad_(m_x.value(args));
        }

    private:
        ArgT m_x;

        T value_(T x) const
        {
            return 1.0 / (1.0 + std::exp(-x));
        }

        T grad_(T x) const
        {
            return value_(x) * (1.0 - value_(x));
        }
    };

    template <typename ArgT>
    auto sigmoid(ArgT x)
    {
        return Sigmoid(std::move(x));
    }

    template <typename ArgT, typename T = typename ArgT::ValueType>
    struct Pow
    {
        using ValueType = T;

        explicit Pow(ArgT x, Id<T> exponent) :
            m_x(std::move(x)),
            m_exponent(std::move(exponent))
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>& args) const
        {
            return std::pow(m_x.value(args), m_exponent);
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_exponent * std::pow(m_x.value(args), m_exponent - T(1.0)) * m_x.grad(args);
        }

    private:
        ArgT m_x;
        T m_exponent;
    };

    template <typename ArgT, typename T = typename ArgT::ValueType>
    auto pow(ArgT x, Id<T> exp)
    {
        return Pow(std::move(x), std::move(exp));
    }

    template <typename ArgT, typename T = typename ArgT::ValueType>
    struct Log
    {
        using ValueType = T;

        explicit Log(ArgT x) :
            m_x(std::move(x))
        {
        }

        template <typename... ArgsTs>
        T value(const std::tuple<ArgsTs...>& args) const
        {
            return value_(m_x.value(args));
        }

        template <typename... ArgsTs>
        T grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_x.grad(args) * grad_(m_x.value(args));
        }

    private:
        ArgT m_x;

        T value_(T x) const
        {
            return std::log(x);
        }

        T grad_(T x) const
        {
            return 1.0 / x;
        }
    };

    template <typename ArgT>
    auto log(ArgT x)
    {
        return Log(std::move(x));
    }

}

#endif