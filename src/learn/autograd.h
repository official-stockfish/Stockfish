#ifndef LEARNER_AUTOGRAD_H
#define LEARNER_AUTOGRAD_H

#include <cmath>
#include <utility>
#include <type_traits>
#include <memory>
#include <tuple>
#include <optional>
#include <algorithm>

namespace Learner
{
    template <typename T>
    struct ValueWithGrad
    {
        T value;
        T grad;

        ValueWithGrad& operator+=(const ValueWithGrad<T>& rhs)
        {
            value += rhs.value;
            grad += rhs.grad;
            return *this;
        }

        ValueWithGrad& operator-=(const ValueWithGrad<T>& rhs)
        {
            value -= rhs.value;
            grad -= rhs.grad;
            return *this;
        }

        ValueWithGrad& operator*=(T rhs)
        {
            value *= rhs;
            grad *= rhs;
            return *this;
        }

        ValueWithGrad& operator/=(T rhs)
        {
            value /= rhs;
            grad /= rhs;
            return *this;
        }

        ValueWithGrad abs() const
        {
            return { std::abs(value), std::abs(grad) };
        }

        ValueWithGrad clamp_grad(T max) const
        {
            return { value, std::clamp(grad, -max, max) };
        }
    };
}

namespace Learner::Autograd::UnivariateStatic
{

    template <typename T>
    struct Identity
    {
        using type = T;
    };

    template <typename T>
    using Id = typename Identity<T>::type;

    template <typename T>
    using StoreValueOrRef = std::conditional_t<
            std::is_rvalue_reference_v<T>,
            std::remove_reference_t<T>,
            const std::remove_reference_t<T>&
        >;

    template <typename T, typename ChildT>
    struct Evaluable
    {
        constexpr Evaluable() = default;

        template <typename... ArgsTs>
        auto eval(const std::tuple<ArgsTs...>& args) const
        {
            return ValueWithGrad<T>{ value(args), grad(args) };
        }

        template <typename... ArgsTs>
        auto value(const std::tuple<ArgsTs...>& args) const
        {
            const ChildT* this_ = static_cast<const ChildT*>(this);

            if (!value_cache.has_value())
            {
                value_cache = this_->calculate_value(args);
            }

            return *value_cache;
        }

        template <typename... ArgsTs>
        auto grad(const std::tuple<ArgsTs...>& args) const
        {
            const ChildT* this_ = static_cast<const ChildT*>(this);

            if (!grad_cache.has_value())
            {
                grad_cache = this_->calculate_grad(args);
            }

            return *grad_cache;
        }

    private:
        mutable std::optional<T> value_cache;
        mutable std::optional<T> grad_cache;
    };

    template <typename T, int I>
    struct VariableParameter : Evaluable<T, VariableParameter<T, I>>
    {
        using ValueType = T;

        constexpr VariableParameter()
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return std::get<I>(args);
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>&) const
        {
            return T(1.0);
        }
    };

    template <typename T, int I>
    struct ConstantParameter : Evaluable<T, ConstantParameter<T, I>>
    {
        using ValueType = T;

        constexpr ConstantParameter()
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return std::get<I>(args);
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>&) const
        {
            return T(0.0);
        }
    };

    template <typename T>
    struct Constant : Evaluable<T, Constant<T>>
    {
        using ValueType = T;

        constexpr Constant(T x) :
            m_x(std::move(x))
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>&) const
        {
            return m_x;
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>&) const
        {
            return T(0.0);
        }

    private:
        T m_x;
    };

    template <typename LhsT, typename RhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    struct Sum : Evaluable<T, Sum<LhsT, RhsT, T>>
    {
        using ValueType = T;

        constexpr Sum(LhsT&& lhs, RhsT&& rhs) :
            m_lhs(std::forward<LhsT>(lhs)),
            m_rhs(std::forward<RhsT>(rhs))
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.value(args) + m_rhs.value(args);
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.grad(args) + m_rhs.grad(args);
        }

    private:
        StoreValueOrRef<LhsT> m_lhs;
        StoreValueOrRef<RhsT> m_rhs;
    };

    template <typename LhsT, typename RhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    constexpr auto operator+(LhsT&& lhs, RhsT&& rhs)
    {
        return Sum<LhsT&&, RhsT&&>(std::forward<LhsT>(lhs), std::forward<RhsT>(rhs));
    }

    template <typename LhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    constexpr auto operator+(LhsT&& lhs, Id<T> rhs)
    {
        return Sum<LhsT&&, Constant<T>&&>(std::forward<LhsT>(lhs), Constant(rhs));
    }

    template <typename RhsT, typename T = typename std::remove_reference_t<RhsT>::ValueType>
    constexpr auto operator+(Id<T> lhs, RhsT&& rhs)
    {
        return Sum<Constant<T>&&, RhsT&&>(Constant(lhs), std::forward<RhsT>(rhs));
    }

    template <typename LhsT, typename RhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    struct Difference : Evaluable<T, Difference<LhsT, RhsT, T>>
    {
        using ValueType = T;

        constexpr Difference(LhsT&& lhs, RhsT&& rhs) :
            m_lhs(std::forward<LhsT>(lhs)),
            m_rhs(std::forward<RhsT>(rhs))
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.value(args) - m_rhs.value(args);
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.grad(args) - m_rhs.grad(args);
        }

    private:
        StoreValueOrRef<LhsT> m_lhs;
        StoreValueOrRef<RhsT> m_rhs;
    };

    template <typename LhsT, typename RhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    constexpr auto operator-(LhsT&& lhs, RhsT&& rhs)
    {
        return Difference<LhsT&&, RhsT&&>(std::forward<LhsT>(lhs), std::forward<RhsT>(rhs));
    }

    template <typename LhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    constexpr auto operator-(LhsT&& lhs, Id<T> rhs)
    {
        return Difference<LhsT&&, Constant<T>&&>(std::forward<LhsT>(lhs), Constant(rhs));
    }

    template <typename RhsT, typename T = typename std::remove_reference_t<RhsT>::ValueType>
    constexpr auto operator-(Id<T> lhs, RhsT&& rhs)
    {
        return Difference<Constant<T>&&, RhsT&&>(Constant(lhs), std::forward<RhsT>(rhs));
    }

    template <typename LhsT, typename RhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    struct Product : Evaluable<T, Product<LhsT, RhsT, T>>
    {
        using ValueType = T;

        constexpr Product(LhsT&& lhs, RhsT&& rhs) :
            m_lhs(std::forward<LhsT>(lhs)),
            m_rhs(std::forward<RhsT>(rhs))
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.value(args) * m_rhs.value(args);
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_lhs.grad(args) * m_rhs.value(args) + m_lhs.value(args) * m_rhs.grad(args);
        }

    private:
        StoreValueOrRef<LhsT> m_lhs;
        StoreValueOrRef<RhsT> m_rhs;
    };

    template <typename LhsT, typename RhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    constexpr auto operator*(LhsT&& lhs, RhsT&& rhs)
    {
        return Product<LhsT&&, RhsT&&>(std::forward<LhsT>(lhs), std::forward<RhsT>(rhs));
    }

    template <typename LhsT, typename T = typename std::remove_reference_t<LhsT>::ValueType>
    constexpr auto operator*(LhsT&& lhs, Id<T> rhs)
    {
        return Product<LhsT&&, Constant<T>&&>(std::forward<LhsT>(lhs), Constant(rhs));
    }

    template <typename RhsT, typename T = typename std::remove_reference_t<RhsT>::ValueType>
    constexpr auto operator*(Id<T> lhs, RhsT&& rhs)
    {
        return Product<Constant<T>&&, RhsT&&>(Constant(lhs), std::forward<RhsT>(rhs));
    }

    template <typename ArgT, typename T = typename std::remove_reference_t<ArgT>::ValueType>
    struct Negation : Evaluable<T, Negation<ArgT, T>>
    {
        using ValueType = T;

        constexpr explicit Negation(ArgT&& x) :
            m_x(std::forward<ArgT>(x))
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return -m_x.value(args);
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>& args) const
        {
            return -m_x.grad(args);
        }

    private:
        StoreValueOrRef<ArgT> m_x;
    };

    template <typename ArgT, typename T = typename std::remove_reference_t<ArgT>::ValueType>
    constexpr auto operator-(ArgT&& x)
    {
        return Negation<ArgT&&>(std::forward<ArgT>(x));
    }

    template <typename ArgT, typename T = typename std::remove_reference_t<ArgT>::ValueType>
    struct Sigmoid : Evaluable<T, Sigmoid<ArgT, T>>
    {
        using ValueType = T;

        constexpr explicit Sigmoid(ArgT&& x) :
            m_x(std::forward<ArgT>(x))
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return value_(m_x.value(args));
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_x.grad(args) * grad_(m_x.value(args));
        }

    private:
        StoreValueOrRef<ArgT> m_x;

        T value_(T x) const
        {
            return 1.0 / (1.0 + std::exp(-x));
        }

        T grad_(T x) const
        {
            return value_(x) * (1.0 - value_(x));
        }
    };

    template <typename ArgT, typename T = typename std::remove_reference_t<ArgT>::ValueType>
    constexpr auto sigmoid(ArgT&& x)
    {
        return Sigmoid<ArgT&&>(std::forward<ArgT>(x));
    }

    template <typename ArgT, typename T = typename std::remove_reference_t<ArgT>::ValueType>
    struct Pow : Evaluable<T, Pow<ArgT, T>>
    {
        using ValueType = T;

        constexpr explicit Pow(ArgT&& x, Id<T> exponent) :
            m_x(std::forward<ArgT>(x)),
            m_exponent(std::move(exponent))
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return std::pow(m_x.value(args), m_exponent);
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_exponent * std::pow(m_x.value(args), m_exponent - T(1.0)) * m_x.grad(args);
        }

    private:
        StoreValueOrRef<ArgT> m_x;
        T m_exponent;
    };

    template <typename ArgT, typename T = typename std::remove_reference_t<ArgT>::ValueType>
    constexpr auto pow(ArgT&& x, Id<T> exp)
    {
        return Pow<ArgT&&>(std::forward<ArgT>(x), std::move(exp));
    }

    template <typename ArgT, typename T = typename std::remove_reference_t<ArgT>::ValueType>
    struct Log : Evaluable<T, Log<ArgT, T>>
    {
        using ValueType = T;

        constexpr explicit Log(ArgT&& x) :
            m_x(std::forward<ArgT>(x))
        {
        }

        template <typename... ArgsTs>
        T calculate_value(const std::tuple<ArgsTs...>& args) const
        {
            return value_(m_x.value(args));
        }

        template <typename... ArgsTs>
        T calculate_grad(const std::tuple<ArgsTs...>& args) const
        {
            return m_x.grad(args) * grad_(m_x.value(args));
        }

    private:
        StoreValueOrRef<ArgT> m_x;

        T value_(T x) const
        {
            return std::log(x);
        }

        T grad_(T x) const
        {
            return 1.0 / x;
        }
    };

    template <typename ArgT, typename T = typename std::remove_reference_t<ArgT>::ValueType>
    constexpr auto log(ArgT&& x)
    {
        return Log<ArgT&&>(std::forward<ArgT>(x));
    }

}

#endif