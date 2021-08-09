/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstdio>
#include <cassert>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <fstream>
#include <cstring>
#include <iostream>
#include <set>
#include <cstdio>
#include <cassert>
#include <array>
#include <limits>
#include <climits>
#include <optional>

#if (defined(_MSC_VER) || defined(__INTEL_COMPILER)) && !defined(__clang__)
#include <intrin.h>
#endif

namespace chess
{
    #if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)

    #define FORCEINLINE __attribute__((always_inline))

    #elif defined(_MSC_VER)

    // NOTE: for some reason it breaks the profiler a little
    //       keep it on only when not profiling.
    //#define FORCEINLINE __forceinline
    #define FORCEINLINE

    #else

    #define FORCEINLINE inline

    #endif

    #if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)

    #define NOINLINE __attribute__((noinline))

    #elif defined(_MSC_VER)

    #define NOINLINE __declspec(noinline)

    #else

    #define NOINLINE

    #endif

    namespace intrin
    {
        [[nodiscard]] constexpr int popcount_constexpr(std::uint64_t value)
        {
            int r = 0;
            while (value)
            {
                value &= value - 1;
                ++r;
            }
            return r;
        }

        [[nodiscard]] constexpr int lsb_constexpr(std::uint64_t value)
        {
            int c = 0;
            value &= ~value + 1; // leave only the lsb
            if ((value & 0x00000000FFFFFFFFull) == 0) c += 32;
            if ((value & 0x0000FFFF0000FFFFull) == 0) c += 16;
            if ((value & 0x00FF00FF00FF00FFull) == 0) c += 8;
            if ((value & 0x0F0F0F0F0F0F0F0Full) == 0) c += 4;
            if ((value & 0x3333333333333333ull) == 0) c += 2;
            if ((value & 0x5555555555555555ull) == 0) c += 1;
            return c;
        }

        [[nodiscard]] constexpr int msb_constexpr(std::uint64_t value)
        {
            int c = 63;
            if ((value & 0xFFFFFFFF00000000ull) == 0) { c -= 32; value <<= 32; }
            if ((value & 0xFFFF000000000000ull) == 0) { c -= 16; value <<= 16; }
            if ((value & 0xFF00000000000000ull) == 0) { c -= 8; value <<= 8; }
            if ((value & 0xF000000000000000ull) == 0) { c -= 4; value <<= 4; }
            if ((value & 0xC000000000000000ull) == 0) { c -= 2; value <<= 2; }
            if ((value & 0x8000000000000000ull) == 0) { c -= 1; }
            return c;
        }
    }

    namespace intrin
    {
        [[nodiscard]] inline int popcount(std::uint64_t b)
        {
    #if (defined(_MSC_VER) || defined(__INTEL_COMPILER)) && !defined(__clang__)

            return static_cast<int>(_mm_popcnt_u64(b));

    #else

            return static_cast<int>(__builtin_popcountll(b));

    #endif
        }

    #if defined(_MSC_VER) && !defined(__clang__)

        [[nodiscard]] inline int lsb(std::uint64_t value)
        {
            assert(value != 0);

            unsigned long idx;
            _BitScanForward64(&idx, value);
            return static_cast<int>(idx);
        }

        [[nodiscard]] inline int msb(std::uint64_t value)
        {
            assert(value != 0);

            unsigned long idx;
            _BitScanReverse64(&idx, value);
            return static_cast<int>(idx);
        }

    #else

        [[nodiscard]] inline int lsb(std::uint64_t value)
        {
            assert(value != 0);

            return __builtin_ctzll(value);
        }

        [[nodiscard]] inline int msb(std::uint64_t value)
        {
            assert(value != 0);

            return 63 ^ __builtin_clzll(value);
        }

    #endif
    }

    template <typename IntT>
    [[nodiscard]] constexpr IntT floorLog2(IntT value)
    {
        return intrin::msb_constexpr(value);
    }

    template <typename IntT>
    constexpr auto computeMasks()
    {
        static_assert(std::is_unsigned_v<IntT>);

        constexpr std::size_t numBits = sizeof(IntT) * CHAR_BIT;
        std::array<IntT, numBits + 1u> nbitmasks{};

        for (std::size_t i = 0; i < numBits; ++i)
        {
            nbitmasks[i] = (static_cast<IntT>(1u) << i) - 1u;
        }
        nbitmasks[numBits] = ~static_cast<IntT>(0u);

        return nbitmasks;
    }

    template <typename IntT>
    constexpr auto nbitmask = computeMasks<IntT>();

    template <std::size_t N, typename FromT, typename ToT = std::make_signed_t<FromT>>
    inline ToT signExtend(FromT value)
    {
        static_assert(std::is_signed_v<ToT>);
        static_assert(std::is_unsigned_v<FromT>);
        static_assert(sizeof(ToT) == sizeof(FromT));

        constexpr std::size_t totalBits = sizeof(FromT) * CHAR_BIT;

        static_assert(N > 0 && N <= totalBits);

        constexpr std::size_t unusedBits = totalBits - N;
        if constexpr (ToT(~FromT(0)) >> 1 == ToT(~FromT(0)))
        {
            return ToT(value << unusedBits) >> ToT(unusedBits);
        }
        else
        {
            constexpr FromT mask = (~FromT(0)) >> unusedBits;
            value &= mask;
            if (value & (FromT(1) << (N - 1)))
            {
                value |= ~mask;
            }
            return static_cast<ToT>(value);
        }
    }

    namespace lookup
    {
        constexpr int nthSetBitIndexNaive(std::uint64_t value, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                value &= value - 1;
            }
            return intrin::lsb_constexpr(value);
        }

        constexpr std::array<std::array<std::uint8_t, 8>, 256> nthSetBitIndex = []()
        {
            std::array<std::array<std::uint8_t, 8>, 256> t{};

            for (int i = 0; i < 256; ++i)
            {
                for (int j = 0; j < 8; ++j)
                {
                    t[i][j] = nthSetBitIndexNaive(i, j);
                }
            }

            return t;
        }();
    }

    inline int nthSetBitIndex(std::uint64_t v, std::uint64_t n)
    {
        std::uint64_t shift = 0;

        std::uint64_t p = intrin::popcount(v & 0xFFFFFFFFull);
        std::uint64_t pmask = static_cast<std::uint64_t>(p > n) - 1ull;
        v >>= 32 & pmask;
        shift += 32 & pmask;
        n -= p & pmask;

        p = intrin::popcount(v & 0xFFFFull);
        pmask = static_cast<std::uint64_t>(p > n) - 1ull;
        v >>= 16 & pmask;
        shift += 16 & pmask;
        n -= p & pmask;

        p = intrin::popcount(v & 0xFFull);
        pmask = static_cast<std::uint64_t>(p > n) - 1ull;
        shift += 8 & pmask;
        v >>= 8 & pmask;
        n -= p & pmask;

        return static_cast<int>(lookup::nthSetBitIndex[v & 0xFFull][n] + shift);
    }

    namespace util
    {
        inline std::size_t usedBits(std::size_t value)
        {
            if (value == 0) return 0;
            return intrin::msb(value) + 1;
        }
    }

    template <typename EnumT>
    struct EnumTraits;

    template <typename EnumT>
    [[nodiscard]] constexpr auto hasEnumTraits() -> decltype(EnumTraits<EnumT>::cardinaliy, bool{})
    {
        return true;
    }

    template <typename EnumT>
    [[nodiscard]] constexpr bool hasEnumTraits(...)
    {
        return false;
    }

    template <typename EnumT>
    [[nodiscard]] constexpr bool isNaturalIndex() noexcept
    {
        return EnumTraits<EnumT>::isNaturalIndex;
    }

    template <typename EnumT>
    [[nodiscard]] constexpr int cardinality() noexcept
    {
        return EnumTraits<EnumT>::cardinality;
    }

    template <typename EnumT>
    [[nodiscard]] constexpr const std::array<EnumT, cardinality<EnumT>()>& values() noexcept
    {
        return EnumTraits<EnumT>::values;
    }

    template <typename EnumT>
    [[nodiscard]] constexpr EnumT fromOrdinal(int id) noexcept
    {
        assert(!EnumTraits<EnumT>::isNaturalIndex || (id >= 0 && id < EnumTraits<EnumT>::cardinality));

        return EnumTraits<EnumT>::fromOrdinal(id);
    }

    template <typename EnumT>
    [[nodiscard]] constexpr typename EnumTraits<EnumT>::IdType ordinal(EnumT v) noexcept
    {
        return EnumTraits<EnumT>::ordinal(v);
    }

    template <typename EnumT, typename... ArgsTs, typename SFINAE = std::enable_if_t<hasEnumTraits<EnumT>()>>
    [[nodiscard]] constexpr decltype(auto) toString(EnumT v, ArgsTs&&... args)
    {
        return EnumTraits<EnumT>::toString(v, std::forward<ArgsTs>(args)...);
    }

    template <typename EnumT>
    [[nodiscard]] constexpr decltype(auto) toString(EnumT v)
    {
        return EnumTraits<EnumT>::toString(v);
    }

    template <typename EnumT, typename FormatT, typename SFINAE = std::enable_if_t<!hasEnumTraits<FormatT>()>>
    [[nodiscard]] constexpr decltype(auto) toString(FormatT&& f, EnumT v)
    {
        return EnumTraits<EnumT>::toString(std::forward<FormatT>(f), v);
    }

    template <typename EnumT>
    [[nodiscard]] constexpr decltype(auto) toChar(EnumT v)
    {
        return EnumTraits<EnumT>::toChar(v);
    }

    template <typename EnumT, typename FormatT>
    [[nodiscard]] constexpr decltype(auto) toChar(FormatT&& f, EnumT v)
    {
        return EnumTraits<EnumT>::toChar(std::forward<FormatT>(f), v);
    }

    template <typename EnumT, typename... ArgsTs>
    [[nodiscard]] constexpr decltype(auto) fromString(ArgsTs&& ... args)
    {
        return EnumTraits<EnumT>::fromString(std::forward<ArgsTs>(args)...);
    }

    template <typename EnumT, typename... ArgsTs>
    [[nodiscard]] constexpr decltype(auto) fromChar(ArgsTs&& ... args)
    {
        return EnumTraits<EnumT>::fromChar(std::forward<ArgsTs>(args)...);
    }

    template <>
    struct EnumTraits<bool>
    {
        using IdType = int;
        using EnumType = bool;

        static constexpr int cardinality = 2;
        static constexpr bool isNaturalIndex = true;

        static constexpr std::array<EnumType, cardinality> values{
            false,
            true
        };

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            return static_cast<EnumType>(id);
        }
    };

    template <typename EnumT, typename ValueT, std::size_t SizeV = cardinality<EnumT>()>
    struct EnumArray
    {
        static_assert(isNaturalIndex<EnumT>(), "Enum must start with 0 and end with cardinality-1.");

        using value_type      = ValueT;
        using size_type       = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer         = ValueT *;
        using const_pointer   = const ValueT*;
        using reference       = ValueT &;
        using const_reference = const ValueT &;

        using iterator       = pointer;
        using const_iterator = const_pointer;

        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        using KeyType = EnumT;
        using ValueType = ValueT;

        constexpr void fill(const ValueType& init)
        {
            for (auto& v : elements)
            {
                v = init;
            }
        }

        [[nodiscard]] constexpr ValueType& operator[](const KeyType& dir)
        {
            assert(static_cast<int>(ordinal(dir)) < static_cast<int>(SizeV));

            return elements[ordinal(dir)];
        }

        [[nodiscard]] constexpr const ValueType& operator[](const KeyType& dir) const
        {
            assert(static_cast<int>(ordinal(dir)) < static_cast<int>(SizeV));

            return elements[ordinal(dir)];
        }

        [[nodiscard]] constexpr ValueType& front()
        {
            return elements[0];
        }

        [[nodiscard]] constexpr const ValueType& front() const
        {
            return elements[0];
        }

        [[nodiscard]] constexpr ValueType& back()
        {
            return elements[SizeV - 1];
        }

        [[nodiscard]] constexpr const ValueType& back() const
        {
            return elements[SizeV - 1];
        }

        [[nodiscard]] constexpr pointer data()
        {
            return elements;
        }

        [[nodiscard]] constexpr const_pointer data() const
        {
            return elements;
        }

        [[nodiscard]] constexpr iterator begin() noexcept
        {
            return elements;
        }

        [[nodiscard]] constexpr const_iterator begin() const noexcept
        {
            return elements;
        }

        [[nodiscard]] constexpr iterator end() noexcept
        {
            return elements + SizeV;
        }

        [[nodiscard]] constexpr const_iterator end() const noexcept
        {
            return elements + SizeV;
        }

        [[nodiscard]] constexpr reverse_iterator rbegin() noexcept
        {
            return reverse_iterator(end());
        }

        [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept
        {
            return const_reverse_iterator(end());
        }

        [[nodiscard]] constexpr reverse_iterator rend() noexcept
        {
            return reverse_iterator(begin());
        }

        [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept
        {
            return const_reverse_iterator(begin());
        }

        [[nodiscard]] constexpr const_iterator cbegin() const noexcept
        {
            return begin();
        }

        [[nodiscard]] constexpr const_iterator cend() const noexcept
        {
            return end();
        }

        [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept
        {
            return rbegin();
        }

        [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept
        {
            return rend();
        }

        [[nodiscard]] constexpr size_type size() const noexcept
        {
            return SizeV;
        }

        ValueT elements[SizeV];
    };

    template <typename Enum1T, typename Enum2T, typename ValueT, std::size_t Size1V = cardinality<Enum1T>(), std::size_t Size2V = cardinality<Enum2T>()>
    using EnumArray2 = EnumArray<Enum1T, EnumArray<Enum2T, ValueT, Size2V>, Size1V>;

    enum struct Color : std::uint8_t
    {
        White,
        Black
    };

    template <>
    struct EnumTraits<Color>
    {
        using IdType = int;
        using EnumType = Color;

        static constexpr int cardinality = 2;
        static constexpr bool isNaturalIndex = true;

        static constexpr std::array<EnumType, cardinality> values{
            Color::White,
            Color::Black
        };

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            assert(id >= 0 && id < cardinality);

            return static_cast<EnumType>(id);
        }

        [[nodiscard]] static constexpr std::string_view toString(EnumType c) noexcept
        {
            return std::string_view("wb" + ordinal(c), 1);
        }

        [[nodiscard]] static constexpr char toChar(EnumType c) noexcept
        {
            return "wb"[ordinal(c)];
        }

        [[nodiscard]] static constexpr std::optional<Color> fromChar(char c) noexcept
        {
            if (c == 'w') return Color::White;
            if (c == 'b') return Color::Black;

            return {};
        }

        [[nodiscard]] static constexpr std::optional<Color> fromString(std::string_view sv) noexcept
        {
            if (sv.size() != 1) return {};

            return fromChar(sv[0]);
        }
    };

    constexpr Color operator!(Color c)
    {
        return fromOrdinal<Color>(ordinal(c) ^ 1);
    }

    enum struct PieceType : std::uint8_t
    {
        Pawn,
        Knight,
        Bishop,
        Rook,
        Queen,
        King,

        None
    };

    template <>
    struct EnumTraits<PieceType>
    {
        using IdType = int;
        using EnumType = PieceType;

        static constexpr int cardinality = 7;
        static constexpr bool isNaturalIndex = true;

        static constexpr std::array<EnumType, cardinality> values{
            PieceType::Pawn,
            PieceType::Knight,
            PieceType::Bishop,
            PieceType::Rook,
            PieceType::Queen,
            PieceType::King,
            PieceType::None
        };

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            assert(id >= 0 && id < cardinality);

            return static_cast<EnumType>(id);
        }

        [[nodiscard]] static constexpr std::string_view toString(EnumType p, Color c) noexcept
        {
            return std::string_view("PpNnBbRrQqKk " + (chess::ordinal(p) * 2 + chess::ordinal(c)), 1);
        }

        [[nodiscard]] static constexpr char toChar(EnumType p, Color c) noexcept
        {
            return "PpNnBbRrQqKk "[chess::ordinal(p) * 2 + chess::ordinal(c)];
        }

        [[nodiscard]] static constexpr std::optional<PieceType> fromChar(char c) noexcept
        {
            auto it = std::string_view("PpNnBbRrQqKk ").find(c);
            if (it == std::string::npos) return {};
            else return static_cast<PieceType>(it/2);
        }

        [[nodiscard]] static constexpr std::optional<PieceType> fromString(std::string_view sv) noexcept
        {
            if (sv.size() != 1) return {};

            return fromChar(sv[0]);
        }
    };

    struct Piece
    {
        [[nodiscard]] static constexpr Piece fromId(int id)
        {
            return Piece(id);
        }

        [[nodiscard]] static constexpr Piece none()
        {
            return Piece(PieceType::None, Color::White);
        }

        constexpr Piece() noexcept :
            Piece(PieceType::None, Color::White)
        {

        }

        constexpr Piece(PieceType type, Color color) noexcept :
            m_id((ordinal(type) << 1) | ordinal(color))
        {
            assert(type != PieceType::None || color == Color::White);
        }

        constexpr Piece& operator=(const Piece& other) = default;

        [[nodiscard]] constexpr friend bool operator==(Piece lhs, Piece rhs) noexcept
        {
            return lhs.m_id == rhs.m_id;
        }

        [[nodiscard]] constexpr friend bool operator!=(Piece lhs, Piece rhs) noexcept
        {
            return !(lhs == rhs);
        }

        [[nodiscard]] constexpr PieceType type() const
        {
            return fromOrdinal<PieceType>(m_id >> 1);
        }

        [[nodiscard]] constexpr Color color() const
        {
            return fromOrdinal<Color>(m_id & 1);
        }

        [[nodiscard]] constexpr std::pair<PieceType, Color> parts() const
        {
            return std::make_pair(type(), color());
        }

        [[nodiscard]] constexpr explicit operator int() const
        {
            return static_cast<int>(m_id);
        }

    private:
        constexpr Piece(int id) :
            m_id(id)
        {
        }

        std::uint8_t m_id; // lowest bit is a color, 7 highest bits are a piece type
    };

    [[nodiscard]] constexpr Piece operator|(PieceType type, Color color) noexcept
    {
        return Piece(type, color);
    }

    [[nodiscard]] constexpr Piece operator|(Color color, PieceType type) noexcept
    {
        return Piece(type, color);
    }

    constexpr Piece whitePawn = Piece(PieceType::Pawn, Color::White);
    constexpr Piece whiteKnight = Piece(PieceType::Knight, Color::White);
    constexpr Piece whiteBishop = Piece(PieceType::Bishop, Color::White);
    constexpr Piece whiteRook = Piece(PieceType::Rook, Color::White);
    constexpr Piece whiteQueen = Piece(PieceType::Queen, Color::White);
    constexpr Piece whiteKing = Piece(PieceType::King, Color::White);

    constexpr Piece blackPawn = Piece(PieceType::Pawn, Color::Black);
    constexpr Piece blackKnight = Piece(PieceType::Knight, Color::Black);
    constexpr Piece blackBishop = Piece(PieceType::Bishop, Color::Black);
    constexpr Piece blackRook = Piece(PieceType::Rook, Color::Black);
    constexpr Piece blackQueen = Piece(PieceType::Queen, Color::Black);
    constexpr Piece blackKing = Piece(PieceType::King, Color::Black);

    static_assert(Piece::none().type() == PieceType::None);

    template <>
    struct EnumTraits<Piece>
    {
        using IdType = int;
        using EnumType = Piece;

        static constexpr int cardinality = 13;
        static constexpr bool isNaturalIndex = true;

        static constexpr std::array<EnumType, cardinality> values{
            whitePawn,
            blackPawn,
            whiteKnight,
            blackKnight,
            whiteBishop,
            blackBishop,
            whiteRook,
            blackRook,
            whiteQueen,
            blackQueen,
            whiteKing,
            blackKing,
            Piece::none()
        };

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(int id) noexcept
        {
            assert(id >= 0 && id < cardinality);

            return Piece::fromId(id);
        }

        [[nodiscard]] static constexpr std::string_view toString(EnumType p) noexcept
        {
            return std::string_view("PpNnBbRrQqKk " + ordinal(p), 1);
        }

        [[nodiscard]] static constexpr char toChar(EnumType p) noexcept
        {
            return "PpNnBbRrQqKk "[ordinal(p)];
        }

        [[nodiscard]] static constexpr std::optional<Piece> fromChar(char c) noexcept
        {
            auto it = std::string_view("PpNnBbRrQqKk ").find(c);
            if (it == std::string::npos) return {};
            else return Piece::fromId(static_cast<int>(it));
        }

        [[nodiscard]] static constexpr std::optional<Piece> fromString(std::string_view sv) noexcept
        {
            if (sv.size() != 1) return {};

            return fromChar(sv[0]);
        }
    };

    template <typename TagT>
    struct Coord
    {
        constexpr Coord() noexcept :
            m_i(0)
        {
        }

        constexpr explicit Coord(int i) noexcept :
            m_i(i)
        {
        }

        [[nodiscard]] constexpr explicit operator int() const
        {
            return static_cast<int>(m_i);
        }

        constexpr friend Coord& operator++(Coord& c)
        {
            ++c.m_i;
            return c;
        }

        constexpr friend Coord& operator--(Coord& c)
        {
            --c.m_i;
            return c;
        }

        constexpr friend Coord& operator+=(Coord& c, int d)
        {
            c.m_i += d;
            return c;
        }

        constexpr friend Coord& operator-=(Coord& c, int d)
        {
            c.m_i -= d;
            return c;
        }

        constexpr friend Coord operator+(const Coord& c, int d)
        {
            Coord cpy(c);
            cpy += d;
            return cpy;
        }

        constexpr friend Coord operator-(const Coord& c, int d)
        {
            Coord cpy(c);
            cpy -= d;
            return cpy;
        }

        constexpr friend int operator-(const Coord& c1, const Coord& c2)
        {
            return c1.m_i - c2.m_i;
        }

        [[nodiscard]] constexpr friend bool operator==(const Coord& c1, const Coord& c2) noexcept
        {
            return c1.m_i == c2.m_i;
        }

        [[nodiscard]] constexpr friend bool operator!=(const Coord& c1, const Coord& c2) noexcept
        {
            return c1.m_i != c2.m_i;
        }

        [[nodiscard]] constexpr friend bool operator<(const Coord& c1, const Coord& c2) noexcept
        {
            return c1.m_i < c2.m_i;
        }

        [[nodiscard]] constexpr friend bool operator<=(const Coord& c1, const Coord& c2) noexcept
        {
            return c1.m_i <= c2.m_i;
        }

        [[nodiscard]] constexpr friend bool operator>(const Coord& c1, const Coord& c2) noexcept
        {
            return c1.m_i > c2.m_i;
        }

        [[nodiscard]] constexpr friend bool operator>=(const Coord& c1, const Coord& c2) noexcept
        {
            return c1.m_i >= c2.m_i;
        }

    private:
        std::int8_t m_i;
    };

    struct FileTag;
    struct RankTag;
    using File = Coord<FileTag>;
    using Rank = Coord<RankTag>;

    constexpr File fileA = File(0);
    constexpr File fileB = File(1);
    constexpr File fileC = File(2);
    constexpr File fileD = File(3);
    constexpr File fileE = File(4);
    constexpr File fileF = File(5);
    constexpr File fileG = File(6);
    constexpr File fileH = File(7);

    constexpr Rank rank1 = Rank(0);
    constexpr Rank rank2 = Rank(1);
    constexpr Rank rank3 = Rank(2);
    constexpr Rank rank4 = Rank(3);
    constexpr Rank rank5 = Rank(4);
    constexpr Rank rank6 = Rank(5);
    constexpr Rank rank7 = Rank(6);
    constexpr Rank rank8 = Rank(7);

    template <>
    struct EnumTraits<File>
    {
        using IdType = int;
        using EnumType = File;

        static constexpr int cardinality = 8;
        static constexpr bool isNaturalIndex = true;

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            assert(id >= 0 && id < cardinality);

            return static_cast<EnumType>(id);
        }

        [[nodiscard]] static constexpr std::string_view toString(EnumType c) noexcept
        {
            assert(ordinal(c) >= 0 && ordinal(c) < 8);

            return std::string_view("abcdefgh" + ordinal(c), 1);
        }

        [[nodiscard]] static constexpr std::optional<File> fromChar(char c) noexcept
        {
            if (c < 'a' || c > 'h') return {};
            return static_cast<File>(c - 'a');
        }

        [[nodiscard]] static constexpr std::optional<File> fromString(std::string_view sv) noexcept
        {
            if (sv.size() != 1) return {};

            return fromChar(sv[0]);
        }
    };

    template <>
    struct EnumTraits<Rank>
    {
        using IdType = int;
        using EnumType = Rank;

        static constexpr int cardinality = 8;
        static constexpr bool isNaturalIndex = true;

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            assert(id >= 0 && id < cardinality);

            return static_cast<EnumType>(id);
        }

        [[nodiscard]] static constexpr std::string_view toString(EnumType c) noexcept
        {
            assert(ordinal(c) >= 0 && ordinal(c) < 8);

            return std::string_view("12345678" + ordinal(c), 1);
        }

        [[nodiscard]] static constexpr std::optional<Rank> fromChar(char c) noexcept
        {
            if (c < '1' || c > '8') return {};
            return static_cast<Rank>(c - '1');
        }

        [[nodiscard]] static constexpr std::optional<Rank> fromString(std::string_view sv) noexcept
        {
            if (sv.size() != 1) return {};

            return fromChar(sv[0]);
        }
    };

    // files east
    // ranks north
    struct FlatSquareOffset
    {
        std::int8_t value;

        constexpr FlatSquareOffset() noexcept :
            value(0)
        {
        }

        constexpr FlatSquareOffset(int files, int ranks) noexcept :
            value(files + ranks * cardinality<File>())
        {
            assert(files + ranks * cardinality<File>() >= std::numeric_limits<std::int8_t>::min());
            assert(files + ranks * cardinality<File>() <= std::numeric_limits<std::int8_t>::max());
        }

        constexpr FlatSquareOffset operator-() const noexcept
        {
            return FlatSquareOffset(-value);
        }

    private:
        constexpr FlatSquareOffset(int v) noexcept :
            value(v)
        {
        }
    };

    struct Offset
    {
        std::int8_t files;
        std::int8_t ranks;

        constexpr Offset() :
            files(0),
            ranks(0)
        {
        }

        constexpr Offset(int files_, int ranks_) :
            files(files_),
            ranks(ranks_)
        {
        }

        [[nodiscard]] constexpr FlatSquareOffset flat() const
        {
            return { files, ranks };
        }

        [[nodiscard]] constexpr Offset operator-() const
        {
            return { -files, -ranks };
        }
    };

    struct SquareCoords
    {
        File file;
        Rank rank;

        constexpr SquareCoords() noexcept :
            file{},
            rank{}
        {
        }

        constexpr SquareCoords(File f, Rank r) noexcept :
            file(f),
            rank(r)
        {
        }

        constexpr friend SquareCoords& operator+=(SquareCoords& c, Offset offset)
        {
            c.file += offset.files;
            c.rank += offset.ranks;
            return c;
        }

        [[nodiscard]] constexpr friend SquareCoords operator+(const SquareCoords& c, Offset offset)
        {
            SquareCoords cpy(c);
            cpy.file += offset.files;
            cpy.rank += offset.ranks;
            return cpy;
        }

        [[nodiscard]] constexpr bool isOk() const
        {
            return file >= fileA && file <= fileH && rank >= rank1 && rank <= rank8;
        }
    };

    struct Square
    {
    private:
        static constexpr std::int8_t m_noneId = cardinality<Rank>() * cardinality<File>();

        static constexpr std::uint8_t fileMask = 0b111;
        static constexpr std::uint8_t rankMask = 0b111000;
        static constexpr std::uint8_t rankShift = 3;

    public:
        [[nodiscard]] static constexpr Square none()
        {
            return Square(m_noneId);
        }

        constexpr Square() noexcept :
            m_id(0)
        {
        }

        constexpr explicit Square(int idx) noexcept :
            m_id(idx)
        {
            assert(isOk() || m_id == m_noneId);
        }

        constexpr Square(File file, Rank rank) noexcept :
            m_id(ordinal(file) + ordinal(rank) * cardinality<File>())
        {
            assert(isOk());
        }

        constexpr explicit Square(SquareCoords coords) noexcept :
            Square(coords.file, coords.rank)
        {
        }

        [[nodiscard]] constexpr friend bool operator<(Square lhs, Square rhs) noexcept
        {
            return lhs.m_id < rhs.m_id;
        }

        [[nodiscard]] constexpr friend bool operator>(Square lhs, Square rhs) noexcept
        {
            return lhs.m_id > rhs.m_id;
        }

        [[nodiscard]] constexpr friend bool operator<=(Square lhs, Square rhs) noexcept
        {
            return lhs.m_id <= rhs.m_id;
        }

        [[nodiscard]] constexpr friend bool operator>=(Square lhs, Square rhs) noexcept
        {
            return lhs.m_id >= rhs.m_id;
        }

        [[nodiscard]] constexpr friend bool operator==(Square lhs, Square rhs) noexcept
        {
            return lhs.m_id == rhs.m_id;
        }

        [[nodiscard]] constexpr friend bool operator!=(Square lhs, Square rhs) noexcept
        {
            return !(lhs == rhs);
        }

        constexpr friend Square& operator++(Square& sq)
        {
            ++sq.m_id;
            return sq;
        }

        constexpr friend Square& operator--(Square& sq)
        {
            --sq.m_id;
            return sq;
        }

        [[nodiscard]] constexpr friend Square operator+(Square sq, FlatSquareOffset offset)
        {
            Square sqCpy = sq;
            sqCpy += offset;
            return sqCpy;
        }

        constexpr friend Square& operator+=(Square& sq, FlatSquareOffset offset)
        {
            assert(sq.m_id + offset.value >= 0 && sq.m_id + offset.value < Square::m_noneId);
            sq.m_id += offset.value;
            return sq;
        }

        [[nodiscard]] constexpr friend Square operator+(Square sq, Offset offset)
        {
            assert(sq.file() + offset.files >= fileA);
            assert(sq.file() + offset.files <= fileH);
            assert(sq.rank() + offset.ranks >= rank1);
            assert(sq.rank() + offset.ranks <= rank8);
            return operator+(sq, offset.flat());
        }

        constexpr friend Square& operator+=(Square& sq, Offset offset)
        {
            return operator+=(sq, offset.flat());
        }

        [[nodiscard]] constexpr explicit operator int() const
        {
            return m_id;
        }

        [[nodiscard]] constexpr File file() const
        {
            assert(isOk());
            return File(static_cast<unsigned>(m_id) & fileMask);
        }

        [[nodiscard]] constexpr Rank rank() const
        {
            assert(isOk());
            return Rank(static_cast<unsigned>(m_id) >> rankShift);
        }

        [[nodiscard]] constexpr SquareCoords coords() const
        {
            return { file(), rank() };
        }

        [[nodiscard]] constexpr Color color() const
        {
            assert(isOk());
            return !fromOrdinal<Color>((ordinal(rank()) + ordinal(file())) & 1);
        }

        constexpr void flipVertically()
        {
            m_id ^= rankMask;
        }

        constexpr void flipHorizontally()
        {
            m_id ^= fileMask;
        }

        constexpr Square flippedVertically() const
        {
            return Square(m_id ^ rankMask);
        }

        constexpr Square flippedHorizontally() const
        {
            return Square(m_id ^ fileMask);
        }

        [[nodiscard]] constexpr bool isOk() const
        {
            return m_id >= 0 && m_id < m_noneId;
        }

    private:
        std::int8_t m_id;
    };

    constexpr Square a1(fileA, rank1);
    constexpr Square a2(fileA, rank2);
    constexpr Square a3(fileA, rank3);
    constexpr Square a4(fileA, rank4);
    constexpr Square a5(fileA, rank5);
    constexpr Square a6(fileA, rank6);
    constexpr Square a7(fileA, rank7);
    constexpr Square a8(fileA, rank8);

    constexpr Square b1(fileB, rank1);
    constexpr Square b2(fileB, rank2);
    constexpr Square b3(fileB, rank3);
    constexpr Square b4(fileB, rank4);
    constexpr Square b5(fileB, rank5);
    constexpr Square b6(fileB, rank6);
    constexpr Square b7(fileB, rank7);
    constexpr Square b8(fileB, rank8);

    constexpr Square c1(fileC, rank1);
    constexpr Square c2(fileC, rank2);
    constexpr Square c3(fileC, rank3);
    constexpr Square c4(fileC, rank4);
    constexpr Square c5(fileC, rank5);
    constexpr Square c6(fileC, rank6);
    constexpr Square c7(fileC, rank7);
    constexpr Square c8(fileC, rank8);

    constexpr Square d1(fileD, rank1);
    constexpr Square d2(fileD, rank2);
    constexpr Square d3(fileD, rank3);
    constexpr Square d4(fileD, rank4);
    constexpr Square d5(fileD, rank5);
    constexpr Square d6(fileD, rank6);
    constexpr Square d7(fileD, rank7);
    constexpr Square d8(fileD, rank8);

    constexpr Square e1(fileE, rank1);
    constexpr Square e2(fileE, rank2);
    constexpr Square e3(fileE, rank3);
    constexpr Square e4(fileE, rank4);
    constexpr Square e5(fileE, rank5);
    constexpr Square e6(fileE, rank6);
    constexpr Square e7(fileE, rank7);
    constexpr Square e8(fileE, rank8);

    constexpr Square f1(fileF, rank1);
    constexpr Square f2(fileF, rank2);
    constexpr Square f3(fileF, rank3);
    constexpr Square f4(fileF, rank4);
    constexpr Square f5(fileF, rank5);
    constexpr Square f6(fileF, rank6);
    constexpr Square f7(fileF, rank7);
    constexpr Square f8(fileF, rank8);

    constexpr Square g1(fileG, rank1);
    constexpr Square g2(fileG, rank2);
    constexpr Square g3(fileG, rank3);
    constexpr Square g4(fileG, rank4);
    constexpr Square g5(fileG, rank5);
    constexpr Square g6(fileG, rank6);
    constexpr Square g7(fileG, rank7);
    constexpr Square g8(fileG, rank8);

    constexpr Square h1(fileH, rank1);
    constexpr Square h2(fileH, rank2);
    constexpr Square h3(fileH, rank3);
    constexpr Square h4(fileH, rank4);
    constexpr Square h5(fileH, rank5);
    constexpr Square h6(fileH, rank6);
    constexpr Square h7(fileH, rank7);
    constexpr Square h8(fileH, rank8);

    static_assert(e1.color() == Color::Black);
    static_assert(e8.color() == Color::White);

    static_assert(e1.file() == fileE);
    static_assert(e1.rank() == rank1);

    static_assert(e1.flippedHorizontally() == d1);
    static_assert(e1.flippedVertically() == e8);

    template <>
    struct EnumTraits<Square>
    {
        using IdType = int;
        using EnumType = Square;

        static constexpr int cardinality = chess::cardinality<Rank>() * chess::cardinality<File>();
        static constexpr bool isNaturalIndex = true;

        static constexpr std::array<EnumType, cardinality> values{
            a1, b1, c1, d1, e1, f1, g1, h1,
            a2, b2, c2, d2, e2, f2, g2, h2,
            a3, b3, c3, d3, e3, f3, g3, h3,
            a4, b4, c4, d4, e4, f4, g4, h4,
            a5, b5, c5, d5, e5, f5, g5, h5,
            a6, b6, c6, d6, e6, f6, g6, h6,
            a7, b7, c7, d7, e7, f7, g7, h7,
            a8, b8, c8, d8, e8, f8, g8, h8
        };

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            assert(id >= 0 && id < cardinality + 1);

            return static_cast<EnumType>(id);
        }

        [[nodiscard]] static constexpr std::string_view toString(Square sq)
        {
            assert(sq.isOk());

            return
                std::string_view(
                    "a1b1c1d1e1f1g1h1"
                    "a2b2c2d2e2f2g2h2"
                    "a3b3c3d3e3f3g3h3"
                    "a4b4c4d4e4f4g4h4"
                    "a5b5c5d5e5f5g5h5"
                    "a6b6c6d6e6f6g6h6"
                    "a7b7c7d7e7f7g7h7"
                    "a8b8c8d8e8f8g8h8"
                    + (ordinal(sq) * 2),
                    2
                );
        }

        [[nodiscard]] static constexpr std::optional<Square> fromString(std::string_view sv) noexcept
        {
            if (sv.size() != 2) return {};

            const char f = sv[0];
            const char r = sv[1];
            if (f < 'a' || f > 'h') return {};
            if (r < '1' || r > '8') return {};

            return Square(static_cast<File>(f - 'a'), static_cast<Rank>(r - '1'));
        }
    };

    static_assert(toString(d1) == std::string_view("d1"));
    static_assert(values<Square>()[29] == f4);

    enum struct MoveType : std::uint8_t
    {
        Normal,
        Promotion,
        Castle,
        EnPassant
    };

    template <>
    struct EnumTraits<MoveType>
    {
        using IdType = int;
        using EnumType = MoveType;

        static constexpr int cardinality = 4;
        static constexpr bool isNaturalIndex = true;

        static constexpr std::array<EnumType, cardinality> values{
            MoveType::Normal,
            MoveType::Promotion,
            MoveType::Castle,
            MoveType::EnPassant
        };

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            assert(id >= 0 && id < cardinality);

            return static_cast<EnumType>(id);
        }
    };

    enum struct CastleType : std::uint8_t
    {
        Short,
        Long
    };

    [[nodiscard]] constexpr CastleType operator!(CastleType ct)
    {
        return static_cast<CastleType>(static_cast<std::uint8_t>(ct) ^ 1);
    }

    template <>
    struct EnumTraits<CastleType>
    {
        using IdType = int;
        using EnumType = CastleType;

        static constexpr int cardinality = 2;
        static constexpr bool isNaturalIndex = true;

        static constexpr std::array<EnumType, cardinality> values{
            CastleType::Short,
            CastleType::Long
        };

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            assert(id >= 0 && id < cardinality);

            return static_cast<EnumType>(id);
        }
    };

    struct CompressedMove;

    // castling is encoded as a king capturing rook
    // ep is encoded as a normal pawn capture (move.to is empty on the board)
    struct Move
    {
        Square from;
        Square to;
        MoveType type = MoveType::Normal;
        Piece promotedPiece = Piece::none();

        [[nodiscard]] constexpr friend bool operator==(const Move& lhs, const Move& rhs) noexcept
        {
            return lhs.from == rhs.from
                && lhs.to == rhs.to
                && lhs.type == rhs.type
                && lhs.promotedPiece == rhs.promotedPiece;
        }

        [[nodiscard]] constexpr friend bool operator!=(const Move& lhs, const Move& rhs) noexcept
        {
            return !(lhs == rhs);
        }

        [[nodiscard]] constexpr CompressedMove compress() const noexcept;

        [[nodiscard]] constexpr static Move null()
        {
            return Move{ Square::none(), Square::none() };
        }

        [[nodiscard]] constexpr static Move castle(CastleType ct, Color c);

        [[nodiscard]] constexpr static Move normal(Square from, Square to)
        {
            return Move{ from, to, MoveType::Normal, Piece::none() };
        }

        [[nodiscard]] constexpr static Move enPassant(Square from, Square to)
        {
            return Move{ from, to, MoveType::EnPassant, Piece::none() };
        }

        [[nodiscard]] constexpr static Move promotion(Square from, Square to, Piece piece)
        {
            return Move{ from, to, MoveType::Promotion, piece };
        }
    };

    namespace detail::castle
    {
        constexpr EnumArray2<CastleType, Color, Move> moves = { {
            {{ { e1, h1, MoveType::Castle }, { e8, h8, MoveType::Castle } }},
            {{ { e1, a1, MoveType::Castle }, { e8, a8, MoveType::Castle } }}
        } };
    }

    [[nodiscard]] constexpr Move Move::castle(CastleType ct, Color c)
    {
        return detail::castle::moves[ct][c];
    }

    static_assert(sizeof(Move) == 4);

    struct CompressedMove
    {
    private:
        // from most significant bits
        // 2 bits for move type
        // 6 bits for from square
        // 6 bits for to square
        // 2 bits for promoted piece type
        //    0 if not a promotion
        static constexpr std::uint16_t squareMask = 0b111111u;
        static constexpr std::uint16_t promotedPieceTypeMask = 0b11u;
        static constexpr std::uint16_t moveTypeMask = 0b11u;

    public:
        [[nodiscard]] constexpr static CompressedMove readFromBigEndian(const unsigned char* data)
        {
            CompressedMove move{};
            move.m_packed = (data[0] << 8) | data[1];
            return move;
        }

        constexpr CompressedMove() noexcept :
            m_packed(0)
        {
        }

        // move must be either valid or a null move
        constexpr CompressedMove(Move move) noexcept :
            m_packed(0)
        {
            // else null move
            if (move.from != move.to)
            {
                assert(move.from != Square::none());
                assert(move.to != Square::none());

                m_packed =
                    (static_cast<std::uint16_t>(ordinal(move.type)) << (16 - 2))
                    | (static_cast<std::uint16_t>(ordinal(move.from)) << (16 - 2 - 6))
                    | (static_cast<std::uint16_t>(ordinal(move.to)) << (16 - 2 - 6 - 6));

                if (move.type == MoveType::Promotion)
                {
                    assert(move.promotedPiece != Piece::none());

                    m_packed |= ordinal(move.promotedPiece.type()) - ordinal(PieceType::Knight);
                }
                else
                {
                    assert(move.promotedPiece == Piece::none());
                }
            }
        }

        void writeToBigEndian(unsigned char* data) const
        {
            *data++ = m_packed >> 8;
            *data++ = m_packed & 0xFF;
        }

        [[nodiscard]] constexpr std::uint16_t packed() const
        {
            return m_packed;
        }

        [[nodiscard]] constexpr MoveType type() const
        {
            return fromOrdinal<MoveType>(m_packed >> (16 - 2));
        }

        [[nodiscard]] constexpr Square from() const
        {
            return fromOrdinal<Square>((m_packed >> (16 - 2 - 6)) & squareMask);
        }

        [[nodiscard]] constexpr Square to() const
        {
            return fromOrdinal<Square>((m_packed >> (16 - 2 - 6 - 6)) & squareMask);
        }

        [[nodiscard]] constexpr Piece promotedPiece() const
        {
            if (type() == MoveType::Promotion)
            {
                const Color color =
                    (to().rank() == rank1)
                    ? Color::Black
                    : Color::White;

                const PieceType pt = fromOrdinal<PieceType>((m_packed & promotedPieceTypeMask) + ordinal(PieceType::Knight));
                return color | pt;
            }
            else
            {
                return Piece::none();
            }
        }

        [[nodiscard]] constexpr Move decompress() const noexcept
        {
            if (m_packed == 0)
            {
                return Move::null();
            }
            else
            {
                const MoveType type = fromOrdinal<MoveType>(m_packed >> (16 - 2));
                const Square from = fromOrdinal<Square>((m_packed >> (16 - 2 - 6)) & squareMask);
                const Square to = fromOrdinal<Square>((m_packed >> (16 - 2 - 6 - 6)) & squareMask);
                const Piece promotedPiece = [&]() {
                    if (type == MoveType::Promotion)
                    {
                        const Color color =
                            (to.rank() == rank1)
                            ? Color::Black
                            : Color::White;

                        const PieceType pt = fromOrdinal<PieceType>((m_packed & promotedPieceTypeMask) + ordinal(PieceType::Knight));
                        return color | pt;
                    }
                    else
                    {
                        return Piece::none();
                    }
                }();

                return Move{ from, to, type, promotedPiece };
            }
        }

    private:
        std::uint16_t m_packed;
    };

    static_assert(sizeof(CompressedMove) == 2);

    [[nodiscard]] constexpr CompressedMove Move::compress() const noexcept
    {
        return CompressedMove(*this);
    }

    static_assert(a4 + Offset{ 0, 1 } == a5);
    static_assert(a4 + Offset{ 0, 2 } == a6);
    static_assert(a4 + Offset{ 0, -2 } == a2);
    static_assert(a4 + Offset{ 0, -1 } == a3);

    static_assert(e4 + Offset{ 1, 0 } == f4);
    static_assert(e4 + Offset{ 2, 0 } == g4);
    static_assert(e4 + Offset{ -1, 0 } == d4);
    static_assert(e4 + Offset{ -2, 0 } == c4);

    enum struct CastlingRights : std::uint8_t
    {
        None = 0x0,
        WhiteKingSide = 0x1,
        WhiteQueenSide = 0x2,
        BlackKingSide = 0x4,
        BlackQueenSide = 0x8,
        White = WhiteKingSide | WhiteQueenSide,
        Black = BlackKingSide | BlackQueenSide,
        All = WhiteKingSide | WhiteQueenSide | BlackKingSide | BlackQueenSide
    };

    [[nodiscard]] constexpr CastlingRights operator|(CastlingRights lhs, CastlingRights rhs)
    {
        return static_cast<CastlingRights>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
    }

    [[nodiscard]] constexpr CastlingRights operator&(CastlingRights lhs, CastlingRights rhs)
    {
        return static_cast<CastlingRights>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
    }

    [[nodiscard]] constexpr CastlingRights operator~(CastlingRights lhs)
    {
        return static_cast<CastlingRights>(~static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(CastlingRights::All));
    }

    constexpr CastlingRights& operator|=(CastlingRights& lhs, CastlingRights rhs)
    {
        lhs = static_cast<CastlingRights>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
        return lhs;
    }

    constexpr CastlingRights& operator&=(CastlingRights& lhs, CastlingRights rhs)
    {
        lhs = static_cast<CastlingRights>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
        return lhs;
    }
    // checks whether lhs contains rhs
    [[nodiscard]] constexpr bool contains(CastlingRights lhs, CastlingRights rhs)
    {
        return (lhs & rhs) == rhs;
    }

    template <>
    struct EnumTraits<CastlingRights>
    {
        using IdType = int;
        using EnumType = CastlingRights;

        static constexpr int cardinality = 4;
        static constexpr bool isNaturalIndex = false;

        static constexpr std::array<EnumType, cardinality> values{
            CastlingRights::WhiteKingSide,
            CastlingRights::WhiteQueenSide,
            CastlingRights::BlackKingSide,
            CastlingRights::BlackQueenSide
        };

        [[nodiscard]] static constexpr int ordinal(EnumType c) noexcept
        {
            return static_cast<IdType>(c);
        }

        [[nodiscard]] static constexpr EnumType fromOrdinal(IdType id) noexcept
        {
            return static_cast<EnumType>(id);
        }
    };

    struct CompressedReverseMove;

    struct ReverseMove
    {
        Move move;
        Piece capturedPiece;
        Square oldEpSquare;
        CastlingRights oldCastlingRights;

        // We need a well defined case for the starting position.
        constexpr ReverseMove() :
            move(Move::null()),
            capturedPiece(Piece::none()),
            oldEpSquare(Square::none()),
            oldCastlingRights(CastlingRights::All)
        {
        }

        constexpr ReverseMove(const Move& move_, Piece capturedPiece_, Square oldEpSquare_, CastlingRights oldCastlingRights_) :
            move(move_),
            capturedPiece(capturedPiece_),
            oldEpSquare(oldEpSquare_),
            oldCastlingRights(oldCastlingRights_)
        {
        }

        constexpr bool isNull() const
        {
            return move.from == move.to;
        }

        [[nodiscard]] constexpr CompressedReverseMove compress() const noexcept;

        [[nodiscard]] constexpr friend bool operator==(const ReverseMove& lhs, const ReverseMove& rhs) noexcept
        {
            return lhs.move == rhs.move
                && lhs.capturedPiece == rhs.capturedPiece
                && lhs.oldEpSquare == rhs.oldEpSquare
                && lhs.oldCastlingRights == rhs.oldCastlingRights;
        }

        [[nodiscard]] constexpr friend bool operator!=(const ReverseMove& lhs, const ReverseMove& rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    static_assert(sizeof(ReverseMove) == 7);

    struct CompressedReverseMove
    {
    private:
        // we use 7 bits because it can be Square::none()
        static constexpr std::uint32_t squareMask = 0b1111111u;
        static constexpr std::uint32_t pieceMask = 0b1111u;
        static constexpr std::uint32_t castlingRightsMask = 0b1111;
    public:

        constexpr CompressedReverseMove() noexcept :
            m_move{},
            m_oldState{}
        {
        }

        constexpr CompressedReverseMove(const ReverseMove& rm) noexcept :
            m_move(rm.move.compress()),
            m_oldState{ static_cast<uint16_t>(
                ((ordinal(rm.capturedPiece) & pieceMask) << 11)
                | ((ordinal(rm.oldCastlingRights) & castlingRightsMask) << 7)
                | (ordinal(rm.oldEpSquare) & squareMask)
                )
            }
        {
        }

        [[nodiscard]] constexpr Move move() const
        {
            return m_move.decompress();
        }

        [[nodiscard]] const CompressedMove& compressedMove() const
        {
            return m_move;
        }

        [[nodiscard]] constexpr Piece capturedPiece() const
        {
            return fromOrdinal<Piece>(m_oldState >> 11);
        }

        [[nodiscard]] constexpr CastlingRights oldCastlingRights() const
        {
            return fromOrdinal<CastlingRights>((m_oldState >> 7) & castlingRightsMask);
        }

        [[nodiscard]] constexpr Square oldEpSquare() const
        {
            return fromOrdinal<Square>(m_oldState & squareMask);
        }

        [[nodiscard]] constexpr ReverseMove decompress() const noexcept
        {
            const Piece capturedPiece = fromOrdinal<Piece>(m_oldState >> 11);
            const CastlingRights castlingRights = fromOrdinal<CastlingRights>((m_oldState >> 7) & castlingRightsMask);
            // We could pack the ep square more, but don't have to, because
            // can't save another byte anyway.
            const Square epSquare = fromOrdinal<Square>(m_oldState & squareMask);

            return ReverseMove(m_move.decompress(), capturedPiece, epSquare, castlingRights);
        }

    private:
        CompressedMove m_move;
        std::uint16_t m_oldState;
    };

    static_assert(sizeof(CompressedReverseMove) == 4);

    [[nodiscard]] constexpr CompressedReverseMove ReverseMove::compress() const noexcept
    {
        return CompressedReverseMove(*this);
    }

    // This can be regarded as a perfect hash. Going back is hard.
    struct PackedReverseMove
    {
        static constexpr std::uint32_t mask = 0x7FFFFFFu;
        static constexpr std::size_t numBits = 27;

    private:
        static constexpr std::uint32_t squareMask = 0b111111u;
        static constexpr std::uint32_t pieceMask = 0b1111u;
        static constexpr std::uint32_t pieceTypeMask = 0b111u;
        static constexpr std::uint32_t castlingRightsMask = 0b1111;
        static constexpr std::uint32_t fileMask = 0b111;

    public:
        constexpr PackedReverseMove(const std::uint32_t packed) :
            m_packed(packed)
        {

        }

        constexpr PackedReverseMove(const ReverseMove& reverseMove) :
            m_packed(
                0u
                // The only move when square is none() is null move and
                // then both squares are none(). No other move is like that
                // so we don't lose any information by storing only
                // the 6 bits of each square.
                | ((ordinal(reverseMove.move.from) & squareMask) << 21)
                | ((ordinal(reverseMove.move.to) & squareMask) << 15)
                // Other masks are just for code clarity, they should
                // never change the values.
                | ((ordinal(reverseMove.capturedPiece) & pieceMask) << 11)
                | ((ordinal(reverseMove.oldCastlingRights) & castlingRightsMask) << 7)
                | ((ordinal(reverseMove.move.promotedPiece.type()) & pieceTypeMask) << 4)
                | (((reverseMove.oldEpSquare != Square::none()) & 1) << 3)
                // We probably could omit the squareMask here but for clarity it's left.
                | (ordinal(Square(ordinal(reverseMove.oldEpSquare) & squareMask).file()) & fileMask)
            )
        {
        }

        constexpr std::uint32_t packed() const
        {
            return m_packed;
        }

        constexpr ReverseMove unpack(Color sideThatMoved) const
        {
            ReverseMove rmove{};

            rmove.move.from = fromOrdinal<Square>((m_packed >> 21) & squareMask);
            rmove.move.to = fromOrdinal<Square>((m_packed >> 15) & squareMask);
            rmove.capturedPiece = fromOrdinal<Piece>((m_packed >> 11) & pieceMask);
            rmove.oldCastlingRights = fromOrdinal<CastlingRights>((m_packed >> 7) & castlingRightsMask);
            const PieceType promotedPieceType = fromOrdinal<PieceType>((m_packed >> 4) & pieceTypeMask);
            if (promotedPieceType != PieceType::None)
            {
                rmove.move.promotedPiece = Piece(promotedPieceType, sideThatMoved);
                rmove.move.type = MoveType::Promotion;
            }
            const bool hasEpSquare = static_cast<bool>((m_packed >> 3) & 1);
            if (hasEpSquare)
            {
                // ep square is always where the opponent moved
                const Rank rank =
                    sideThatMoved == Color::White
                    ? rank6
                    : rank3;
                const File file = fromOrdinal<File>(m_packed & fileMask);
                rmove.oldEpSquare = Square(file, rank);
                if (rmove.oldEpSquare == rmove.move.to)
                {
                    rmove.move.type = MoveType::EnPassant;
                }
            }
            else
            {
                rmove.oldEpSquare = Square::none();
            }

            if (rmove.move.type == MoveType::Normal && rmove.oldCastlingRights != CastlingRights::None)
            {
                // If castling was possible then we know it was the king that moved from e1/e8.
                if (rmove.move.from == e1)
                {
                    if (rmove.move.to == h1 || rmove.move.to == a1)
                    {
                        rmove.move.type = MoveType::Castle;
                    }
                }
                else if (rmove.move.from == e8)
                {
                    if (rmove.move.to == h8 || rmove.move.to == a8)
                    {
                        rmove.move.type = MoveType::Castle;
                    }
                }
            }

            return rmove;
        }

    private:
        // Uses only 27 lowest bits.
        // Bit meaning from highest to lowest.
        // - 6 bits from
        // - 6 bits to
        // - 4 bits for the captured piece
        // - 4 bits for prev castling rights
        // - 3 bits promoted piece type
        // - 1 bit  to specify if the ep square was valid (false if none())
        // - 3 bits for prev ep square file
        std::uint32_t m_packed;
    };

    struct MoveCompareLess
    {
        [[nodiscard]] bool operator()(const Move& lhs, const Move& rhs) const noexcept
        {
            if (ordinal(lhs.from) < ordinal(rhs.from)) return true;
            if (ordinal(lhs.from) > ordinal(rhs.from)) return false;

            if (ordinal(lhs.to) < ordinal(rhs.to)) return true;
            if (ordinal(lhs.to) > ordinal(rhs.to)) return false;

            if (ordinal(lhs.type) < ordinal(rhs.type)) return true;
            if (ordinal(lhs.type) > ordinal(rhs.type)) return false;

            if (ordinal(lhs.promotedPiece) < ordinal(rhs.promotedPiece)) return true;

            return false;
        }
    };

    struct ReverseMoveCompareLess
    {
        [[nodiscard]] bool operator()(const ReverseMove& lhs, const ReverseMove& rhs) const noexcept
        {
            if (MoveCompareLess{}(lhs.move, rhs.move)) return true;
            if (MoveCompareLess{}(rhs.move, lhs.move)) return false;

            if (ordinal(lhs.capturedPiece) < ordinal(rhs.capturedPiece)) return true;
            if (ordinal(lhs.capturedPiece) > ordinal(rhs.capturedPiece)) return false;

            if (static_cast<unsigned>(lhs.oldCastlingRights) < static_cast<unsigned>(rhs.oldCastlingRights)) return true;
            if (static_cast<unsigned>(lhs.oldCastlingRights) > static_cast<unsigned>(rhs.oldCastlingRights)) return false;

            if (ordinal(lhs.oldEpSquare) < ordinal(rhs.oldEpSquare)) return true;
            if (ordinal(lhs.oldEpSquare) > ordinal(rhs.oldEpSquare)) return false;

            return false;
        }
    };

    struct BitboardIterator
    {
        using value_type = Square;
        using difference_type = std::ptrdiff_t;
        using reference = Square;
        using iterator_category = std::input_iterator_tag;
        using pointer = const Square*;

        constexpr BitboardIterator() noexcept :
            m_squares(0)
        {
        }

        constexpr BitboardIterator(std::uint64_t v) noexcept :
            m_squares(v)
        {
        }

        constexpr BitboardIterator(const BitboardIterator&) = default;
        constexpr BitboardIterator(BitboardIterator&&) = default;
        constexpr BitboardIterator& operator=(const BitboardIterator&) = default;
        constexpr BitboardIterator& operator=(BitboardIterator&&) = default;

        [[nodiscard]] constexpr bool friend operator==(BitboardIterator lhs, BitboardIterator rhs) noexcept
        {
            return lhs.m_squares == rhs.m_squares;
        }

        [[nodiscard]] constexpr bool friend operator!=(BitboardIterator lhs, BitboardIterator rhs) noexcept
        {
            return lhs.m_squares != rhs.m_squares;
        }

        [[nodiscard]] inline Square operator*() const
        {
            return first();
        }

        constexpr BitboardIterator& operator++() noexcept
        {
            popFirst();
            return *this;
        }

    private:
        std::uint64_t m_squares;

        constexpr void popFirst() noexcept
        {
            m_squares &= m_squares - 1;
        }

        [[nodiscard]] inline Square first() const
        {
            assert(m_squares != 0);

            return fromOrdinal<Square>(intrin::lsb(m_squares));
        }
    };

    struct Bitboard
    {
        // bits counted from the LSB
        // order is A1 B2 ... G8 H8
        // just like in Square

    public:
        constexpr Bitboard() noexcept :
            m_squares(0)
        {
        }

    private:
        constexpr explicit Bitboard(Square sq) noexcept :
            m_squares(static_cast<std::uint64_t>(1ULL) << ordinal(sq))
        {
            assert(sq.isOk());
        }

        constexpr explicit Bitboard(Rank r) noexcept :
            m_squares(static_cast<std::uint64_t>(0xFFULL) << (ordinal(r) * 8))
        {
        }

        constexpr explicit Bitboard(File f) noexcept :
            m_squares(static_cast<std::uint64_t>(0x0101010101010101ULL) << ordinal(f))
        {
        }

        constexpr explicit Bitboard(Color c) noexcept :
            m_squares(c == Color::White ? 0xAA55AA55AA55AA55ULL : ~0xAA55AA55AA55AA55ULL)
        {
        }

        constexpr explicit Bitboard(std::uint64_t bb) noexcept :
            m_squares(bb)
        {
        }

        // files A..file inclusive
        static constexpr EnumArray<File, std::uint64_t> m_filesUpToBB{
            0x0101010101010101ULL,
            0x0303030303030303ULL,
            0x0707070707070707ULL,
            0x0F0F0F0F0F0F0F0FULL,
            0x1F1F1F1F1F1F1F1FULL,
            0x3F3F3F3F3F3F3F3FULL,
            0x7F7F7F7F7F7F7F7FULL,
            0xFFFFFFFFFFFFFFFFULL
        };

    public:

        [[nodiscard]] static constexpr Bitboard none()
        {
            return Bitboard{};
        }

        [[nodiscard]] static constexpr Bitboard all()
        {
            return ~none();
        }

        [[nodiscard]] static constexpr Bitboard square(Square sq)
        {
            return Bitboard(sq);
        }

        [[nodiscard]] static constexpr Bitboard file(File f)
        {
            return Bitboard(f);
        }

        [[nodiscard]] static constexpr Bitboard rank(Rank r)
        {
            return Bitboard(r);
        }

        [[nodiscard]] static constexpr Bitboard color(Color c)
        {
            return Bitboard(c);
        }

        [[nodiscard]] static constexpr Bitboard fromBits(std::uint64_t bits)
        {
            return Bitboard(bits);
        }

        // inclusive
        [[nodiscard]] static constexpr Bitboard betweenFiles(File left, File right)
        {
            assert(left <= right);

            if (left == fileA)
            {
                return Bitboard::fromBits(m_filesUpToBB[right]);
            }
            else
            {
                return Bitboard::fromBits(m_filesUpToBB[right] ^ m_filesUpToBB[left - 1]);
            }
        }

        [[nodiscard]] constexpr bool isEmpty() const
        {
            return m_squares == 0;
        }

        [[nodiscard]] constexpr bool isSet(Square sq) const
        {
            return !!((m_squares >> ordinal(sq)) & 1ull);
        }

        constexpr void set(Square sq)
        {
            *this |= Bitboard(sq);
        }

        constexpr void unset(Square sq)
        {
            *this &= ~(Bitboard(sq));
        }

        constexpr void toggle(Square sq)
        {
            *this ^= Bitboard(sq);
        }

        [[nodiscard]] constexpr BitboardIterator begin() const
        {
            return BitboardIterator(m_squares);
        }

        [[nodiscard]] constexpr BitboardIterator end() const
        {
            return BitboardIterator{};
        }

        [[nodiscard]] constexpr BitboardIterator cbegin() const
        {
            return BitboardIterator(m_squares);
        }

        [[nodiscard]] constexpr BitboardIterator cend() const
        {
            return BitboardIterator{};
        }

        [[nodiscard]] constexpr bool friend operator==(Bitboard lhs, Bitboard rhs) noexcept
        {
            return lhs.m_squares == rhs.m_squares;
        }

        [[nodiscard]] constexpr bool friend operator!=(Bitboard lhs, Bitboard rhs) noexcept
        {
            return lhs.m_squares != rhs.m_squares;
        }

        constexpr Bitboard shiftedVertically(int ranks) const
        {
            if (ranks >= 0)
            {
                return fromBits(m_squares << 8 * ranks);
            }
            else
            {
                return fromBits(m_squares >> -8 * ranks);
            }
        }

        template <int files, int ranks>
        constexpr void shift()
        {
            static_assert(files >= -7);
            static_assert(ranks >= -7);
            static_assert(files <= 7);
            static_assert(ranks <= 7);

            if constexpr (files != 0)
            {
                constexpr Bitboard mask =
                    files > 0
                    ? Bitboard::betweenFiles(fileA, fileH - files)
                    : Bitboard::betweenFiles(fileA - files, fileH);

                m_squares &= mask.m_squares;
            }

            constexpr int shift = files + ranks * 8;
            if constexpr (shift == 0)
            {
                return;
            }

            if constexpr (shift < 0)
            {
                m_squares >>= -shift;
            }
            else
            {
                m_squares <<= shift;
            }
        }

        template <int files, int ranks>
        constexpr Bitboard shifted() const
        {
            Bitboard bbCpy(*this);
            bbCpy.shift<files, ranks>();
            return bbCpy;
        }

        constexpr void shift(Offset offset)
        {
            assert(offset.files >= -7);
            assert(offset.ranks >= -7);
            assert(offset.files <= 7);
            assert(offset.ranks <= 7);

            if (offset.files != 0)
            {
                const Bitboard mask =
                    offset.files > 0
                    ? Bitboard::betweenFiles(fileA, fileH - offset.files)
                    : Bitboard::betweenFiles(fileA - offset.files, fileH);

                m_squares &= mask.m_squares;
            }

            const int shift = offset.files + offset.ranks * 8;
            if (shift < 0)
            {
                m_squares >>= -shift;
            }
            else
            {
                m_squares <<= shift;
            }
        }

        [[nodiscard]] constexpr Bitboard shifted(Offset offset) const
        {
            Bitboard bbCpy(*this);
            bbCpy.shift(offset);
            return bbCpy;
        }

        [[nodiscard]] constexpr Bitboard operator~() const
        {
            Bitboard bb = *this;
            bb.m_squares = ~m_squares;
            return bb;
        }

        constexpr Bitboard& operator^=(Color c)
        {
            m_squares ^= Bitboard(c).m_squares;
            return *this;
        }

        constexpr Bitboard& operator&=(Color c)
        {
            m_squares &= Bitboard(c).m_squares;
            return *this;
        }

        constexpr Bitboard& operator|=(Color c)
        {
            m_squares |= Bitboard(c).m_squares;
            return *this;
        }

        [[nodiscard]] constexpr Bitboard operator^(Color c) const
        {
            Bitboard bb = *this;
            bb ^= c;
            return bb;
        }

        [[nodiscard]] constexpr Bitboard operator&(Color c) const
        {
            Bitboard bb = *this;
            bb &= c;
            return bb;
        }

        [[nodiscard]] constexpr Bitboard operator|(Color c) const
        {
            Bitboard bb = *this;
            bb |= c;
            return bb;
        }

        constexpr Bitboard& operator^=(Square sq)
        {
            m_squares ^= Bitboard(sq).m_squares;
            return *this;
        }

        constexpr Bitboard& operator&=(Square sq)
        {
            m_squares &= Bitboard(sq).m_squares;
            return *this;
        }

        constexpr Bitboard& operator|=(Square sq)
        {
            m_squares |= Bitboard(sq).m_squares;
            return *this;
        }

        [[nodiscard]] constexpr Bitboard operator^(Square sq) const
        {
            Bitboard bb = *this;
            bb ^= sq;
            return bb;
        }

        [[nodiscard]] constexpr Bitboard operator&(Square sq) const
        {
            Bitboard bb = *this;
            bb &= sq;
            return bb;
        }

        [[nodiscard]] constexpr Bitboard operator|(Square sq) const
        {
            Bitboard bb = *this;
            bb |= sq;
            return bb;
        }

        [[nodiscard]] constexpr friend Bitboard operator^(Square sq, Bitboard bb)
        {
            return bb ^ sq;
        }

        [[nodiscard]] constexpr friend Bitboard operator&(Square sq, Bitboard bb)
        {
            return bb & sq;
        }

        [[nodiscard]] constexpr friend Bitboard operator|(Square sq, Bitboard bb)
        {
            return bb | sq;
        }

        constexpr Bitboard& operator^=(Bitboard rhs)
        {
            m_squares ^= rhs.m_squares;
            return *this;
        }

        constexpr Bitboard& operator&=(Bitboard rhs)
        {
            m_squares &= rhs.m_squares;
            return *this;
        }

        constexpr Bitboard& operator|=(Bitboard rhs)
        {
            m_squares |= rhs.m_squares;
            return *this;
        }

        [[nodiscard]] constexpr Bitboard operator^(Bitboard sq) const
        {
            Bitboard bb = *this;
            bb ^= sq;
            return bb;
        }

        [[nodiscard]] constexpr Bitboard operator&(Bitboard sq) const
        {
            Bitboard bb = *this;
            bb &= sq;
            return bb;
        }

        [[nodiscard]] constexpr Bitboard operator|(Bitboard sq) const
        {
            Bitboard bb = *this;
            bb |= sq;
            return bb;
        }

        [[nodiscard]] inline int count() const
        {
            return static_cast<int>(intrin::popcount(m_squares));
        }

        [[nodiscard]] constexpr bool moreThanOne() const
        {
            return !!(m_squares & (m_squares - 1));
        }

        [[nodiscard]] constexpr bool exactlyOne() const
        {
            return m_squares != 0 && !moreThanOne();
        }

        [[nodiscard]] constexpr bool any() const
        {
            return !!m_squares;
        }

        [[nodiscard]] inline Square first() const
        {
            assert(m_squares != 0);

            return fromOrdinal<Square>(intrin::lsb(m_squares));
        }

        [[nodiscard]] inline Square nth(int n) const
        {
            assert(count() > n);

            Bitboard cpy = *this;
            while (n--) cpy.popFirst();
            return cpy.first();
        }

        [[nodiscard]] inline Square last() const
        {
            assert(m_squares != 0);

            return fromOrdinal<Square>(intrin::msb(m_squares));
        }

        [[nodiscard]] constexpr std::uint64_t bits() const
        {
            return m_squares;
        }

        constexpr void popFirst()
        {
            assert(m_squares != 0);

            m_squares &= m_squares - 1;
        }

        constexpr Bitboard& operator=(const Bitboard& other) = default;

    private:
        std::uint64_t m_squares;
    };

    [[nodiscard]] constexpr Bitboard operator^(Square sq0, Square sq1)
    {
        return Bitboard::square(sq0) ^ sq1;
    }

    [[nodiscard]] constexpr Bitboard operator&(Square sq0, Square sq1)
    {
        return Bitboard::square(sq0) & sq1;
    }

    [[nodiscard]] constexpr Bitboard operator|(Square sq0, Square sq1)
    {
        return Bitboard::square(sq0) | sq1;
    }

    [[nodiscard]] constexpr Bitboard operator""_bb(unsigned long long bits)
    {
        return Bitboard::fromBits(bits);
    }

    namespace bb
    {
        namespace fancy_magics
        {
            // Implementation based on https://github.com/syzygy1/Cfish

            alignas(64) constexpr EnumArray<Square, std::uint64_t> g_rookMagics{ {
                0x0A80004000801220ull,
                0x8040004010002008ull,
                0x2080200010008008ull,
                0x1100100008210004ull,
                0xC200209084020008ull,
                0x2100010004000208ull,
                0x0400081000822421ull,
                0x0200010422048844ull,
                0x0800800080400024ull,
                0x0001402000401000ull,
                0x3000801000802001ull,
                0x4400800800100083ull,
                0x0904802402480080ull,
                0x4040800400020080ull,
                0x0018808042000100ull,
                0x4040800080004100ull,
                0x0040048001458024ull,
                0x00A0004000205000ull,
                0x3100808010002000ull,
                0x4825010010000820ull,
                0x5004808008000401ull,
                0x2024818004000A00ull,
                0x0005808002000100ull,
                0x2100060004806104ull,
                0x0080400880008421ull,
                0x4062220600410280ull,
                0x010A004A00108022ull,
                0x0000100080080080ull,
                0x0021000500080010ull,
                0x0044000202001008ull,
                0x0000100400080102ull,
                0xC020128200040545ull,
                0x0080002000400040ull,
                0x0000804000802004ull,
                0x0000120022004080ull,
                0x010A386103001001ull,
                0x9010080080800400ull,
                0x8440020080800400ull,
                0x0004228824001001ull,
                0x000000490A000084ull,
                0x0080002000504000ull,
                0x200020005000C000ull,
                0x0012088020420010ull,
                0x0010010080080800ull,
                0x0085001008010004ull,
                0x0002000204008080ull,
                0x0040413002040008ull,
                0x0000304081020004ull,
                0x0080204000800080ull,
                0x3008804000290100ull,
                0x1010100080200080ull,
                0x2008100208028080ull,
                0x5000850800910100ull,
                0x8402019004680200ull,
                0x0120911028020400ull,
                0x0000008044010200ull,
                0x0020850200244012ull,
                0x0020850200244012ull,
                0x0000102001040841ull,
                0x140900040A100021ull,
                0x000200282410A102ull,
                0x000200282410A102ull,
                0x000200282410A102ull,
                0x4048240043802106ull
                    } };

            alignas(64) constexpr EnumArray<Square, std::uint64_t> g_bishopMagics{ {
                0x40106000A1160020ull,
                0x0020010250810120ull,
                0x2010010220280081ull,
                0x002806004050C040ull,
                0x0002021018000000ull,
                0x2001112010000400ull,
                0x0881010120218080ull,
                0x1030820110010500ull,
                0x0000120222042400ull,
                0x2000020404040044ull,
                0x8000480094208000ull,
                0x0003422A02000001ull,
                0x000A220210100040ull,
                0x8004820202226000ull,
                0x0018234854100800ull,
                0x0100004042101040ull,
                0x0004001004082820ull,
                0x0010000810010048ull,
                0x1014004208081300ull,
                0x2080818802044202ull,
                0x0040880C00A00100ull,
                0x0080400200522010ull,
                0x0001000188180B04ull,
                0x0080249202020204ull,
                0x1004400004100410ull,
                0x00013100A0022206ull,
                0x2148500001040080ull,
                0x4241080011004300ull,
                0x4020848004002000ull,
                0x10101380D1004100ull,
                0x0008004422020284ull,
                0x01010A1041008080ull,
                0x0808080400082121ull,
                0x0808080400082121ull,
                0x0091128200100C00ull,
                0x0202200802010104ull,
                0x8C0A020200440085ull,
                0x01A0008080B10040ull,
                0x0889520080122800ull,
                0x100902022202010Aull,
                0x04081A0816002000ull,
                0x0000681208005000ull,
                0x8170840041008802ull,
                0x0A00004200810805ull,
                0x0830404408210100ull,
                0x2602208106006102ull,
                0x1048300680802628ull,
                0x2602208106006102ull,
                0x0602010120110040ull,
                0x0941010801043000ull,
                0x000040440A210428ull,
                0x0008240020880021ull,
                0x0400002012048200ull,
                0x00AC102001210220ull,
                0x0220021002009900ull,
                0x84440C080A013080ull,
                0x0001008044200440ull,
                0x0004C04410841000ull,
                0x2000500104011130ull,
                0x1A0C010011C20229ull,
                0x0044800112202200ull,
                0x0434804908100424ull,
                0x0300404822C08200ull,
                0x48081010008A2A80ull
            } };

            alignas(64) static EnumArray<Square, Bitboard> g_rookMasks;
            alignas(64) static EnumArray<Square, std::uint8_t> g_rookShifts;
            alignas(64) static EnumArray<Square, const Bitboard*> g_rookAttacks;

            alignas(64) static EnumArray<Square, Bitboard> g_bishopMasks;
            alignas(64) static EnumArray<Square, std::uint8_t> g_bishopShifts;
            alignas(64) static EnumArray<Square, const Bitboard*> g_bishopAttacks;

            alignas(64) static std::array<Bitboard, 102400> g_allRookAttacks;
            alignas(64) static std::array<Bitboard, 5248> g_allBishopAttacks;

            inline Bitboard bishopAttacks(Square s, Bitboard occupied)
            {
                const std::size_t idx =
                    (occupied & fancy_magics::g_bishopMasks[s]).bits()
                    * fancy_magics::g_bishopMagics[s]
                    >> fancy_magics::g_bishopShifts[s];

                return fancy_magics::g_bishopAttacks[s][idx];
            }

            inline Bitboard rookAttacks(Square s, Bitboard occupied)
            {
                const std::size_t idx =
                    (occupied & fancy_magics::g_rookMasks[s]).bits()
                    * fancy_magics::g_rookMagics[s]
                    >> fancy_magics::g_rookShifts[s];

                return fancy_magics::g_rookAttacks[s][idx];
            }
        }

        [[nodiscard]] constexpr Bitboard square(Square sq)
        {
            return Bitboard::square(sq);
        }

        [[nodiscard]] constexpr Bitboard rank(Rank rank)
        {
            return Bitboard::rank(rank);
        }

        [[nodiscard]] constexpr Bitboard file(File file)
        {
            return Bitboard::file(file);
        }

        [[nodiscard]] constexpr Bitboard color(Color c)
        {
            return Bitboard::color(c);
        }

        [[nodiscard]] constexpr Bitboard before(Square sq)
        {
            return Bitboard::fromBits(nbitmask<std::uint64_t>[ordinal(sq)]);
        }

        constexpr Bitboard lightSquares = bb::color(Color::White);
        constexpr Bitboard darkSquares = bb::color(Color::Black);

        constexpr Bitboard fileA = bb::file(chess::fileA);
        constexpr Bitboard fileB = bb::file(chess::fileB);
        constexpr Bitboard fileC = bb::file(chess::fileC);
        constexpr Bitboard fileD = bb::file(chess::fileD);
        constexpr Bitboard fileE = bb::file(chess::fileE);
        constexpr Bitboard fileF = bb::file(chess::fileF);
        constexpr Bitboard fileG = bb::file(chess::fileG);
        constexpr Bitboard fileH = bb::file(chess::fileH);

        constexpr Bitboard rank1 = bb::rank(chess::rank1);
        constexpr Bitboard rank2 = bb::rank(chess::rank2);
        constexpr Bitboard rank3 = bb::rank(chess::rank3);
        constexpr Bitboard rank4 = bb::rank(chess::rank4);
        constexpr Bitboard rank5 = bb::rank(chess::rank5);
        constexpr Bitboard rank6 = bb::rank(chess::rank6);
        constexpr Bitboard rank7 = bb::rank(chess::rank7);
        constexpr Bitboard rank8 = bb::rank(chess::rank8);

        constexpr Bitboard a1 = bb::square(chess::a1);
        constexpr Bitboard a2 = bb::square(chess::a2);
        constexpr Bitboard a3 = bb::square(chess::a3);
        constexpr Bitboard a4 = bb::square(chess::a4);
        constexpr Bitboard a5 = bb::square(chess::a5);
        constexpr Bitboard a6 = bb::square(chess::a6);
        constexpr Bitboard a7 = bb::square(chess::a7);
        constexpr Bitboard a8 = bb::square(chess::a8);

        constexpr Bitboard b1 = bb::square(chess::b1);
        constexpr Bitboard b2 = bb::square(chess::b2);
        constexpr Bitboard b3 = bb::square(chess::b3);
        constexpr Bitboard b4 = bb::square(chess::b4);
        constexpr Bitboard b5 = bb::square(chess::b5);
        constexpr Bitboard b6 = bb::square(chess::b6);
        constexpr Bitboard b7 = bb::square(chess::b7);
        constexpr Bitboard b8 = bb::square(chess::b8);

        constexpr Bitboard c1 = bb::square(chess::c1);
        constexpr Bitboard c2 = bb::square(chess::c2);
        constexpr Bitboard c3 = bb::square(chess::c3);
        constexpr Bitboard c4 = bb::square(chess::c4);
        constexpr Bitboard c5 = bb::square(chess::c5);
        constexpr Bitboard c6 = bb::square(chess::c6);
        constexpr Bitboard c7 = bb::square(chess::c7);
        constexpr Bitboard c8 = bb::square(chess::c8);

        constexpr Bitboard d1 = bb::square(chess::d1);
        constexpr Bitboard d2 = bb::square(chess::d2);
        constexpr Bitboard d3 = bb::square(chess::d3);
        constexpr Bitboard d4 = bb::square(chess::d4);
        constexpr Bitboard d5 = bb::square(chess::d5);
        constexpr Bitboard d6 = bb::square(chess::d6);
        constexpr Bitboard d7 = bb::square(chess::d7);
        constexpr Bitboard d8 = bb::square(chess::d8);

        constexpr Bitboard e1 = bb::square(chess::e1);
        constexpr Bitboard e2 = bb::square(chess::e2);
        constexpr Bitboard e3 = bb::square(chess::e3);
        constexpr Bitboard e4 = bb::square(chess::e4);
        constexpr Bitboard e5 = bb::square(chess::e5);
        constexpr Bitboard e6 = bb::square(chess::e6);
        constexpr Bitboard e7 = bb::square(chess::e7);
        constexpr Bitboard e8 = bb::square(chess::e8);

        constexpr Bitboard f1 = bb::square(chess::f1);
        constexpr Bitboard f2 = bb::square(chess::f2);
        constexpr Bitboard f3 = bb::square(chess::f3);
        constexpr Bitboard f4 = bb::square(chess::f4);
        constexpr Bitboard f5 = bb::square(chess::f5);
        constexpr Bitboard f6 = bb::square(chess::f6);
        constexpr Bitboard f7 = bb::square(chess::f7);
        constexpr Bitboard f8 = bb::square(chess::f8);

        constexpr Bitboard g1 = bb::square(chess::g1);
        constexpr Bitboard g2 = bb::square(chess::g2);
        constexpr Bitboard g3 = bb::square(chess::g3);
        constexpr Bitboard g4 = bb::square(chess::g4);
        constexpr Bitboard g5 = bb::square(chess::g5);
        constexpr Bitboard g6 = bb::square(chess::g6);
        constexpr Bitboard g7 = bb::square(chess::g7);
        constexpr Bitboard g8 = bb::square(chess::g8);

        constexpr Bitboard h1 = bb::square(chess::h1);
        constexpr Bitboard h2 = bb::square(chess::h2);
        constexpr Bitboard h3 = bb::square(chess::h3);
        constexpr Bitboard h4 = bb::square(chess::h4);
        constexpr Bitboard h5 = bb::square(chess::h5);
        constexpr Bitboard h6 = bb::square(chess::h6);
        constexpr Bitboard h7 = bb::square(chess::h7);
        constexpr Bitboard h8 = bb::square(chess::h8);

        [[nodiscard]] Bitboard between(Square s1, Square s2);

        [[nodiscard]] Bitboard line(Square s1, Square s2);

        template <PieceType PieceTypeV>
        [[nodiscard]] Bitboard pseudoAttacks(Square sq);

        [[nodiscard]] Bitboard pseudoAttacks(PieceType pt, Square sq);

        template <PieceType PieceTypeV>
        Bitboard attacks(Square sq, Bitboard occupied)
        {
            static_assert(PieceTypeV != PieceType::None && PieceTypeV != PieceType::Pawn);

            assert(sq.isOk());

            if constexpr (PieceTypeV == PieceType::Bishop)
            {
                return fancy_magics::bishopAttacks(sq, occupied);
            }
            else if constexpr (PieceTypeV == PieceType::Rook)
            {
                return fancy_magics::rookAttacks(sq, occupied);
            }
            else if constexpr (PieceTypeV == PieceType::Queen)
            {
                return
                    fancy_magics::bishopAttacks(sq, occupied)
                    | fancy_magics::rookAttacks(sq, occupied);
            }
            else
            {
                return pseudoAttacks<PieceTypeV>(sq);
            }
        }

        [[nodiscard]] inline Bitboard attacks(PieceType pt, Square sq, Bitboard occupied)
        {
            assert(sq.isOk());

            switch (pt)
            {
            case PieceType::Bishop:
                return attacks<PieceType::Bishop>(sq, occupied);
            case PieceType::Rook:
                return attacks<PieceType::Rook>(sq, occupied);
            case PieceType::Queen:
                return attacks<PieceType::Queen>(sq, occupied);
            default:
                return pseudoAttacks(pt, sq);
            }
        }

        [[nodiscard]] inline Bitboard pawnAttacks(Bitboard pawns, Color color);

        [[nodiscard]] inline Bitboard westPawnAttacks(Bitboard pawns, Color color);

        [[nodiscard]] inline Bitboard eastPawnAttacks(Bitboard pawns, Color color);

        [[nodiscard]] inline bool isAttackedBySlider(
            Square sq,
            Bitboard bishops,
            Bitboard rooks,
            Bitboard queens,
            Bitboard occupied
        );

        namespace detail
        {
            static constexpr std::array<Offset, 8> knightOffsets{ { {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {-2, -1}, {-2, 1}, {2, -1}, {2, 1} } };
            static constexpr std::array<Offset, 8> kingOffsets{ { {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1} } };

            enum Direction
            {
                North = 0,
                NorthEast,
                East,
                SouthEast,
                South,
                SouthWest,
                West,
                NorthWest
            };

            constexpr std::array<Offset, 8> offsets = { {
                { 0, 1 },
                { 1, 1 },
                { 1, 0 },
                { 1, -1 },
                { 0, -1 },
                { -1, -1 },
                { -1, 0 },
                { -1, 1 }
            } };

            static constexpr std::array<Offset, 4> bishopOffsets{
                offsets[NorthEast],
                offsets[SouthEast],
                offsets[SouthWest],
                offsets[NorthWest]
            };
            static constexpr std::array<Offset, 4> rookOffsets{
                offsets[North],
                offsets[East],
                offsets[South],
                offsets[West]
            };

            [[nodiscard]] static EnumArray<Square, Bitboard> generatePseudoAttacks_Pawn()
            {
                // pseudo attacks don't make sense for pawns
                return {};
            }

            [[nodiscard]] static EnumArray<Square, Bitboard> generatePseudoAttacks_Knight()
            {
                EnumArray<Square, Bitboard> bbs{};

                for (Square fromSq = chess::a1; fromSq != Square::none(); ++fromSq)
                {
                    Bitboard bb{};

                    for (auto&& offset : knightOffsets)
                    {
                        const SquareCoords toSq = fromSq.coords() + offset;
                        if (toSq.isOk())
                        {
                            bb |= Square(toSq);
                        }
                    }

                    bbs[fromSq] = bb;
                }

                return bbs;
            }

            [[nodiscard]] static Bitboard generateSliderPseudoAttacks(const std::array<Offset, 4> & offsets_, Square fromSq)
            {
                assert(fromSq.isOk());

                Bitboard bb{};

                for (auto&& offset : offsets_)
                {
                    SquareCoords fromSqC = fromSq.coords();

                    for (;;)
                    {
                        fromSqC += offset;

                        if (!fromSqC.isOk())
                        {
                            break;
                        }

                        bb |= Square(fromSqC);
                    }
                }

                return bb;
            }

            [[nodiscard]] static EnumArray<Square, Bitboard> generatePseudoAttacks_Bishop()
            {
                EnumArray<Square, Bitboard> bbs{};

                for (Square fromSq = chess::a1; fromSq != Square::none(); ++fromSq)
                {
                    bbs[fromSq] = generateSliderPseudoAttacks(bishopOffsets, fromSq);
                }

                return bbs;
            }

            [[nodiscard]] static EnumArray<Square, Bitboard> generatePseudoAttacks_Rook()
            {
                EnumArray<Square, Bitboard> bbs{};

                for (Square fromSq = chess::a1; fromSq != Square::none(); ++fromSq)
                {
                    bbs[fromSq] = generateSliderPseudoAttacks(rookOffsets, fromSq);
                }

                return bbs;
            }

            [[nodiscard]] static EnumArray<Square, Bitboard> generatePseudoAttacks_Queen()
            {
                EnumArray<Square, Bitboard> bbs{};

                for (Square fromSq = chess::a1; fromSq != Square::none(); ++fromSq)
                {
                    bbs[fromSq] =
                        generateSliderPseudoAttacks(bishopOffsets, fromSq)
                        | generateSliderPseudoAttacks(rookOffsets, fromSq);
                }

                return bbs;
            }

            [[nodiscard]] static EnumArray<Square, Bitboard> generatePseudoAttacks_King()
            {
                EnumArray<Square, Bitboard> bbs{};

                for (Square fromSq = chess::a1; fromSq != Square::none(); ++fromSq)
                {
                    Bitboard bb{};

                    for (auto&& offset : kingOffsets)
                    {
                        const SquareCoords toSq = fromSq.coords() + offset;
                        if (toSq.isOk())
                        {
                            bb |= Square(toSq);
                        }
                    }

                    bbs[fromSq] = bb;
                }

                return bbs;
            }

            [[nodiscard]] static EnumArray2<PieceType, Square, Bitboard> generatePseudoAttacks()
            {
                return EnumArray2<PieceType, Square, Bitboard>{
                    generatePseudoAttacks_Pawn(),
                        generatePseudoAttacks_Knight(),
                        generatePseudoAttacks_Bishop(),
                        generatePseudoAttacks_Rook(),
                        generatePseudoAttacks_Queen(),
                        generatePseudoAttacks_King()
                };
            }

            static const EnumArray2<PieceType, Square, Bitboard>& pseudoAttacks()
            {
                static const EnumArray2<PieceType, Square, Bitboard> s_pseudoAttacks = generatePseudoAttacks();
                return s_pseudoAttacks;
            }

            [[nodiscard]] static Bitboard generatePositiveRayAttacks(Direction dir, Square fromSq)
            {
                assert(fromSq.isOk());

                Bitboard bb{};

                const auto offset = offsets[dir];
                SquareCoords fromSqC = fromSq.coords();
                for (;;)
                {
                    fromSqC += offset;

                    if (!fromSqC.isOk())
                    {
                        break;
                    }

                    bb |= Square(fromSqC);
                }

                return bb;
            }

            // classical slider move generation approach https://www.chessprogramming.org/Classical_Approach

            [[nodiscard]] static EnumArray<Square, Bitboard> generatePositiveRayAttacks(Direction dir)
            {
                EnumArray<Square, Bitboard> bbs{};

                for (Square fromSq = chess::a1; fromSq != Square::none(); ++fromSq)
                {
                    bbs[fromSq] = generatePositiveRayAttacks(dir, fromSq);
                }

                return bbs;
            }

            [[nodiscard]] static std::array<EnumArray<Square, Bitboard>, 8> generatePositiveRayAttacks()
            {
                std::array<EnumArray<Square, Bitboard>, 8> bbs{};

                bbs[North] = generatePositiveRayAttacks(North);
                bbs[NorthEast] = generatePositiveRayAttacks(NorthEast);
                bbs[East] = generatePositiveRayAttacks(East);
                bbs[SouthEast] = generatePositiveRayAttacks(SouthEast);
                bbs[South] = generatePositiveRayAttacks(South);
                bbs[SouthWest] = generatePositiveRayAttacks(SouthWest);
                bbs[West] = generatePositiveRayAttacks(West);
                bbs[NorthWest] = generatePositiveRayAttacks(NorthWest);

                return bbs;
            }


            static const std::array<EnumArray<Square, Bitboard>, 8>& positiveRayAttacks()
            {
                static const std::array<EnumArray<Square, Bitboard>, 8> s_positiveRayAttacks = generatePositiveRayAttacks();
                return s_positiveRayAttacks;
            }

            template <Direction DirV>
            [[nodiscard]] static Bitboard slidingAttacks(Square sq, Bitboard occupied)
            {
                assert(sq.isOk());

                Bitboard attacks = positiveRayAttacks()[DirV][sq];

                if constexpr (DirV == NorthWest || DirV == North || DirV == NorthEast || DirV == East)
                {
                    Bitboard blocker = (attacks & occupied) | h8; // set highest bit (H8) so msb never fails
                    return attacks ^ positiveRayAttacks()[DirV][blocker.first()];
                }
                else
                {
                    Bitboard blocker = (attacks & occupied) | a1;
                    return attacks ^ positiveRayAttacks()[DirV][blocker.last()];
                }
            }

            template Bitboard slidingAttacks<Direction::North>(Square, Bitboard);
            template Bitboard slidingAttacks<Direction::NorthEast>(Square, Bitboard);
            template Bitboard slidingAttacks<Direction::East>(Square, Bitboard);
            template Bitboard slidingAttacks<Direction::SouthEast>(Square, Bitboard);
            template Bitboard slidingAttacks<Direction::South>(Square, Bitboard);
            template Bitboard slidingAttacks<Direction::SouthWest>(Square, Bitboard);
            template Bitboard slidingAttacks<Direction::West>(Square, Bitboard);
            template Bitboard slidingAttacks<Direction::NorthWest>(Square, Bitboard);

            template <PieceType PieceTypeV>
            [[nodiscard]] inline Bitboard pieceSlidingAttacks(Square sq, Bitboard occupied)
            {
                static_assert(
                    PieceTypeV == PieceType::Rook
                    || PieceTypeV == PieceType::Bishop
                    || PieceTypeV == PieceType::Queen);

                assert(sq.isOk());

                if constexpr (PieceTypeV == PieceType::Bishop)
                {
                    return
                        detail::slidingAttacks<detail::NorthEast>(sq, occupied)
                        | detail::slidingAttacks<detail::SouthEast>(sq, occupied)
                        | detail::slidingAttacks<detail::SouthWest>(sq, occupied)
                        | detail::slidingAttacks<detail::NorthWest>(sq, occupied);
                }
                else if constexpr (PieceTypeV == PieceType::Rook)
                {
                    return
                        detail::slidingAttacks<detail::North>(sq, occupied)
                        | detail::slidingAttacks<detail::East>(sq, occupied)
                        | detail::slidingAttacks<detail::South>(sq, occupied)
                        | detail::slidingAttacks<detail::West>(sq, occupied);
                }
                else // if constexpr (PieceTypeV == PieceType::Queen)
                {
                    return
                        detail::slidingAttacks<detail::North>(sq, occupied)
                        | detail::slidingAttacks<detail::NorthEast>(sq, occupied)
                        | detail::slidingAttacks<detail::East>(sq, occupied)
                        | detail::slidingAttacks<detail::SouthEast>(sq, occupied)
                        | detail::slidingAttacks<detail::South>(sq, occupied)
                        | detail::slidingAttacks<detail::SouthWest>(sq, occupied)
                        | detail::slidingAttacks<detail::West>(sq, occupied)
                        | detail::slidingAttacks<detail::NorthWest>(sq, occupied);
                }
            }

            static Bitboard generateBetween(Square s1, Square s2)
            {
                Bitboard bb = Bitboard::none();

                if (s1 == s2)
                {
                    return bb;
                }

                const int fd = s2.file() - s1.file();
                const int rd = s2.rank() - s1.rank();

                if (fd == 0 || rd == 0 || fd == rd || fd == -rd)
                {
                    // s1 and s2 lie on a line.
                    const int fileStep = (fd > 0) - (fd < 0);
                    const int rankStep = (rd > 0) - (rd < 0);
                    const auto step = FlatSquareOffset(fileStep, rankStep);
                    s1 += step; // omit s1
                    while(s1 != s2) // omit s2
                    {
                        bb |= s1;
                        s1 += step;
                    }
                }

                return bb;
            }

            static Bitboard generateLine(Square s1, Square s2)
            {
                for (PieceType pt : { PieceType::Bishop, PieceType::Rook })
                {
                    const Bitboard s1Attacks = pseudoAttacks()[pt][s1];
                    if (s1Attacks.isSet(s2))
                    {
                        const Bitboard s2Attacks = pseudoAttacks()[pt][s2];
                        return (s1Attacks & s2Attacks) | s1 | s2;
                    }
                }

                return Bitboard::none();
            }

            static const EnumArray2<Square, Square, Bitboard> between = []()
            {
                EnumArray2<Square, Square, Bitboard> between_;

                for (Square s1 : values<Square>())
                {
                    for (Square s2 : values<Square>())
                    {
                        between_[s1][s2] = generateBetween(s1, s2);
                    }
                }

                return between_;
            }();

            static const EnumArray2<Square, Square, Bitboard> line = []()
            {
                EnumArray2<Square, Square, Bitboard> line_;

                for (Square s1 : values<Square>())
                {
                    for (Square s2 : values<Square>())
                    {
                        line_[s1][s2] = generateLine(s1, s2);
                    }
                }

                return line_;
            }();
        }

        namespace fancy_magics
        {
            enum struct MagicsType
            {
                Rook,
                Bishop
            };

            template <MagicsType TypeV>
            [[nodiscard]] inline Bitboard slidingAttacks(Square sq, Bitboard occupied)
            {
                if (TypeV == MagicsType::Rook)
                {
                    return chess::bb::detail::pieceSlidingAttacks<PieceType::Rook>(sq, occupied);
                }

                if (TypeV == MagicsType::Bishop)
                {
                    return chess::bb::detail::pieceSlidingAttacks<PieceType::Bishop>(sq, occupied);
                }

                return Bitboard::none();
            }

            template <MagicsType TypeV, std::size_t SizeV>
            [[nodiscard]] inline bool initMagics(
                const EnumArray<Square, std::uint64_t>& magics,
                std::array<Bitboard, SizeV>& table,
                EnumArray<Square, Bitboard>& masks,
                EnumArray<Square, std::uint8_t>& shifts,
                EnumArray<Square, const Bitboard*>& attacks
            )
            {
                std::size_t size = 0;
                for (Square sq : values<Square>())
                {
                    const Bitboard edges =
                        ((bb::rank1 | bb::rank8) & ~Bitboard::rank(sq.rank()))
                        | ((bb::fileA | bb::fileH) & ~Bitboard::file(sq.file()));

                    Bitboard* currentAttacks = table.data() + size;

                    attacks[sq] = currentAttacks;
                    masks[sq] = slidingAttacks<TypeV>(sq, Bitboard::none()) & ~edges;
                    shifts[sq] = 64 - masks[sq].count();

                    Bitboard occupied = Bitboard::none();
                    do
                    {
                        const std::size_t idx =
                            (occupied & masks[sq]).bits()
                            * magics[sq]
                            >> shifts[sq];

                        currentAttacks[idx] = slidingAttacks<TypeV>(sq, occupied);

                        ++size;
                        occupied = Bitboard::fromBits(occupied.bits() - masks[sq].bits()) & masks[sq];
                    } while (occupied.any());
                }

                return true;
            }

            static bool g_isRookMagicsInitialized =
                initMagics<MagicsType::Rook>(g_rookMagics, g_allRookAttacks, g_rookMasks, g_rookShifts, g_rookAttacks);

            static bool g_isBishopMagicsInitialized =
                initMagics<MagicsType::Bishop>(g_bishopMagics, g_allBishopAttacks, g_bishopMasks, g_bishopShifts, g_bishopAttacks);
        }

        [[nodiscard]] inline Bitboard between(Square s1, Square s2)
        {
            return detail::between[s1][s2];
        }

        [[nodiscard]] inline Bitboard line(Square s1, Square s2)
        {
            return detail::line[s1][s2];
        }

        template <PieceType PieceTypeV>
        [[nodiscard]] inline Bitboard pseudoAttacks(Square sq)
        {
            static_assert(PieceTypeV != PieceType::None && PieceTypeV != PieceType::Pawn);

            assert(sq.isOk());

            return detail::pseudoAttacks()[PieceTypeV][sq];
        }

        [[nodiscard]] inline Bitboard pseudoAttacks(PieceType pt, Square sq)
        {
            assert(sq.isOk());

            return detail::pseudoAttacks()[pt][sq];
        }

        [[nodiscard]] inline Bitboard pawnAttacks(Bitboard pawns, Color color)
        {
            if (color == Color::White)
            {
                return pawns.shifted<1, 1>() | pawns.shifted<-1, 1>();
            }
            else
            {
                return pawns.shifted<1, -1>() | pawns.shifted<-1, -1>();
            }
        }

        [[nodiscard]] inline Bitboard westPawnAttacks(Bitboard pawns, Color color)
        {
            if (color == Color::White)
            {
                return pawns.shifted<-1, 1>();
            }
            else
            {
                return pawns.shifted<-1, -1>();
            }
        }

        [[nodiscard]] inline Bitboard eastPawnAttacks(Bitboard pawns, Color color)
        {
            if (color == Color::White)
            {
                return pawns.shifted<1, 1>();
            }
            else
            {
                return pawns.shifted<1, -1>();
            }
        }

        [[nodiscard]] inline bool isAttackedBySlider(
            Square sq,
            Bitboard bishops,
            Bitboard rooks,
            Bitboard queens,
            Bitboard occupied
        )
        {
            const Bitboard opponentBishopLikePieces = (bishops | queens);
            const Bitboard bishopAttacks = bb::attacks<PieceType::Bishop>(sq, occupied);
            if ((bishopAttacks & opponentBishopLikePieces).any())
            {
                return true;
            }

            const Bitboard opponentRookLikePieces = (rooks | queens);
            const Bitboard rookAttacks = bb::attacks<PieceType::Rook>(sq, occupied);
            return (rookAttacks & opponentRookLikePieces).any();
        }
    }

    struct CastlingTraits
    {
        static constexpr EnumArray2<Color, CastleType, Square> rookDestination = { { {{ f1, d1 }}, {{ f8, d8 }} } };
        static constexpr EnumArray2<Color, CastleType, Square> kingDestination = { { {{ g1, c1 }}, {{ g8, c8 }} } };

        static constexpr EnumArray2<Color, CastleType, Square> rookStart = { { {{ h1, a1 }}, {{ h8, a8 }} } };

        static constexpr EnumArray<Color, Square> kingStart = { { e1, e8 } };

        static constexpr EnumArray2<Color, CastleType, Bitboard> castlingPath = {
            {
                {{ Bitboard::square(f1) | g1, Bitboard::square(b1) | c1 | d1 }},
                {{ Bitboard::square(f8) | g8, Bitboard::square(b8) | c8 | d8 }}
            }
        };

        static constexpr EnumArray2<Color, CastleType, Square> squarePassedByKing = {
            {
                {{ f1, d1 }},
                {{ f8, d8 }}
            }
        };

        static constexpr EnumArray2<Color, CastleType, CastlingRights> castlingRights = {
            {
                {{ CastlingRights::WhiteKingSide, CastlingRights::WhiteQueenSide }},
                {{ CastlingRights::BlackKingSide, CastlingRights::BlackQueenSide }}
            }
        };

        // Move has to be a legal castling move.
        static constexpr CastleType moveCastlingType(const Move& move)
        {
            return (move.to.file() == fileH) ? CastleType::Short : CastleType::Long;
        }

        // Move must be a legal castling move.
        static constexpr CastlingRights moveCastlingRight(Move move)
        {
            if (move.to == h1) return CastlingRights::WhiteKingSide;
            if (move.to == a1) return CastlingRights::WhiteQueenSide;
            if (move.to == h8) return CastlingRights::WhiteKingSide;
            if (move.to == a8) return CastlingRights::WhiteQueenSide;
            return CastlingRights::None;
        }
    };

    namespace parser_bits
    {
        [[nodiscard]] constexpr bool isFile(char c)
        {
            return c >= 'a' && c <= 'h';
        }

        [[nodiscard]] constexpr bool isRank(char c)
        {
            return c >= '1' && c <= '8';
        }

        [[nodiscard]] constexpr Rank parseRank(char c)
        {
            assert(isRank(c));

            return fromOrdinal<Rank>(c - '1');
        }

        [[nodiscard]] constexpr File parseFile(char c)
        {
            assert(isFile(c));

            return fromOrdinal<File>(c - 'a');
        }

        [[nodiscard]] constexpr bool isSquare(const char* s)
        {
            return isFile(s[0]) && isRank(s[1]);
        }

        [[nodiscard]] constexpr Square parseSquare(const char* s)
        {
            const File file = parseFile(s[0]);
            const Rank rank = parseRank(s[1]);
            return Square(file, rank);
        }

        [[nodiscard]] constexpr std::optional<Square> tryParseSquare(std::string_view s)
        {
            if (s.size() != 2) return {};
            if (!isSquare(s.data())) return {};
            return parseSquare(s.data());
        }

        [[nodiscard]] constexpr std::optional<Square> tryParseEpSquare(std::string_view s)
        {
            if (s == std::string_view("-")) return Square::none();
            return tryParseSquare(s);
        }

        [[nodiscard]] constexpr std::optional<CastlingRights> tryParseCastlingRights(std::string_view s)
        {
            if (s == std::string_view("-")) return CastlingRights::None;

            CastlingRights rights = CastlingRights::None;

            for (auto& c : s)
            {
                CastlingRights toAdd = CastlingRights::None;
                switch (c)
                {
                case 'K':
                    toAdd = CastlingRights::WhiteKingSide;
                    break;
                case 'Q':
                    toAdd = CastlingRights::WhiteQueenSide;
                    break;
                case 'k':
                    toAdd = CastlingRights::BlackKingSide;
                    break;
                case 'q':
                    toAdd = CastlingRights::BlackQueenSide;
                    break;
                }

                // If there are duplicated castling rights specification we bail.
                // If there is an invalid character we bail.
                // (It always contains None)
                if (contains(rights, toAdd)) return {};
                else rights |= toAdd;
            }

            return rights;
        }

        [[nodiscard]] constexpr CastlingRights readCastlingRights(const char*& s)
        {
            CastlingRights rights = CastlingRights::None;

            while (*s != ' ')
            {
                switch (*s)
                {
                case 'K':
                    rights |= CastlingRights::WhiteKingSide;
                    break;
                case 'Q':
                    rights |= CastlingRights::WhiteQueenSide;
                    break;
                case 'k':
                    rights |= CastlingRights::BlackKingSide;
                    break;
                case 'q':
                    rights |= CastlingRights::BlackQueenSide;
                    break;
                }

                ++s;
            }

            return rights;
        }

        FORCEINLINE inline void appendCastlingRightsToString(CastlingRights rights, std::string& str)
        {
            if (rights == CastlingRights::None)
            {
                str += '-';
            }
            else
            {
                if (contains(rights, CastlingRights::WhiteKingSide)) str += 'K';
                if (contains(rights, CastlingRights::WhiteQueenSide)) str += 'Q';
                if (contains(rights, CastlingRights::BlackKingSide)) str += 'k';
                if (contains(rights, CastlingRights::BlackQueenSide)) str += 'q';
            }
        }

        FORCEINLINE inline void appendSquareToString(Square sq, std::string& str)
        {
            str += static_cast<char>('a' + ordinal(sq.file()));
            str += static_cast<char>('1' + ordinal(sq.rank()));
        }

        FORCEINLINE inline void appendEpSquareToString(Square sq, std::string& str)
        {
            if (sq == Square::none())
            {
                str += '-';
            }
            else
            {
                appendSquareToString(sq, str);
            }
        }

        FORCEINLINE inline void appendRankToString(Rank r, std::string& str)
        {
            str += static_cast<char>('1' + ordinal(r));
        }

        FORCEINLINE inline void appendFileToString(File f, std::string& str)
        {
            str += static_cast<char>('a' + ordinal(f));
        }

        [[nodiscard]] FORCEINLINE inline bool isDigit(char c)
        {
            return c >= '0' && c <= '9';
        }

        [[nodiscard]] inline std::uint16_t parseUInt16(std::string_view sv)
        {
            assert(sv.size() > 0);
            assert(sv.size() <= 5);

            std::uint16_t v = 0;

            std::size_t idx = 0;
            switch (sv.size())
            {
            case 5:
                v += (sv[idx++] - '0') * 10000;
            case 4:
                v += (sv[idx++] - '0') * 1000;
            case 3:
                v += (sv[idx++] - '0') * 100;
            case 2:
                v += (sv[idx++] - '0') * 10;
            case 1:
                v += sv[idx] - '0';
                break;

            default:
                assert(false);
            }

            return v;
        }

        [[nodiscard]] inline std::optional<std::uint16_t> tryParseUInt16(std::string_view sv)
        {
            if (sv.size() == 0 || sv.size() > 5) return std::nullopt;

            std::uint32_t v = 0;

            std::size_t idx = 0;
            switch (sv.size())
            {
            case 5:
                v += (sv[idx++] - '0') * 10000;
            case 4:
                v += (sv[idx++] - '0') * 1000;
            case 3:
                v += (sv[idx++] - '0') * 100;
            case 2:
                v += (sv[idx++] - '0') * 10;
            case 1:
                v += sv[idx] - '0';
                break;

            default:
                assert(false);
            }

            if (v > std::numeric_limits<std::uint16_t>::max())
            {
                return std::nullopt;
            }

            return static_cast<std::uint16_t>(v);
        }
    }


    struct Board
    {
        constexpr Board() noexcept :
            m_pieces{},
            m_pieceBB{},
            m_piecesByColorBB{},
            m_pieceCount{}
        {
            m_pieces.fill(Piece::none());
            m_pieceBB.fill(Bitboard::none());
            m_pieceBB[Piece::none()] = Bitboard::all();
            m_piecesByColorBB.fill(Bitboard::none());
            m_pieceCount.fill(0);
            m_pieceCount[Piece::none()] = 64;
        }

        [[nodiscard]] inline bool isValid() const
        {
            if (piecesBB(whiteKing).count() != 1) return false;
            if (piecesBB(blackKing).count() != 1) return false;
            if (((piecesBB(whitePawn) | piecesBB(blackPawn)) & (bb::rank(rank1) | bb::rank(rank8))).any()) return false;
            return true;
        }

        [[nodiscard]] inline std::string fen() const;

        [[nodiscard]] inline bool trySet(std::string_view boardState)
        {
            File f = fileA;
            Rank r = rank8;
            bool lastWasSkip = false;
            for (auto c : boardState)
            {
                Piece piece = Piece::none();
                switch (c)
                {
                case 'r':
                    piece = Piece(PieceType::Rook, Color::Black);
                    break;
                case 'n':
                    piece = Piece(PieceType::Knight, Color::Black);
                    break;
                case 'b':
                    piece = Piece(PieceType::Bishop, Color::Black);
                    break;
                case 'q':
                    piece = Piece(PieceType::Queen, Color::Black);
                    break;
                case 'k':
                    piece = Piece(PieceType::King, Color::Black);
                    break;
                case 'p':
                    piece = Piece(PieceType::Pawn, Color::Black);
                    break;

                case 'R':
                    piece = Piece(PieceType::Rook, Color::White);
                    break;
                case 'N':
                    piece = Piece(PieceType::Knight, Color::White);
                    break;
                case 'B':
                    piece = Piece(PieceType::Bishop, Color::White);
                    break;
                case 'Q':
                    piece = Piece(PieceType::Queen, Color::White);
                    break;
                case 'K':
                    piece = Piece(PieceType::King, Color::White);
                    break;
                case 'P':
                    piece = Piece(PieceType::Pawn, Color::White);
                    break;

                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                {
                    if (lastWasSkip) return false;
                    lastWasSkip = true;

                    const int skip = c - '0';
                    f += skip;
                    if (f > fileH + 1) return false;
                    break;
                }

                case '/':
                    lastWasSkip = false;
                    if (f != fileH + 1) return false;
                    f = fileA;
                    --r;
                    break;

                default:
                    return false;
                }

                if (piece != Piece::none())
                {
                    lastWasSkip = false;

                    const Square sq(f, r);
                    if (!sq.isOk()) return false;

                    place(piece, sq);
                    ++f;
                }
            }

            if (f != fileH + 1) return false;
            if (r != rank1) return false;

            return isValid();
        }

        // returns side to move
        [[nodiscard]] constexpr const char* set(const char* fen)
        {
            assert(fen != nullptr);

            File f = fileA;
            Rank r = rank8;
            auto current = fen;
            bool done = false;
            while (*current != '\0')
            {
                Piece piece = Piece::none();
                switch (*current)
                {
                case 'r':
                    piece = Piece(PieceType::Rook, Color::Black);
                    break;
                case 'n':
                    piece = Piece(PieceType::Knight, Color::Black);
                    break;
                case 'b':
                    piece = Piece(PieceType::Bishop, Color::Black);
                    break;
                case 'q':
                    piece = Piece(PieceType::Queen, Color::Black);
                    break;
                case 'k':
                    piece = Piece(PieceType::King, Color::Black);
                    break;
                case 'p':
                    piece = Piece(PieceType::Pawn, Color::Black);
                    break;

                case 'R':
                    piece = Piece(PieceType::Rook, Color::White);
                    break;
                case 'N':
                    piece = Piece(PieceType::Knight, Color::White);
                    break;
                case 'B':
                    piece = Piece(PieceType::Bishop, Color::White);
                    break;
                case 'Q':
                    piece = Piece(PieceType::Queen, Color::White);
                    break;
                case 'K':
                    piece = Piece(PieceType::King, Color::White);
                    break;
                case 'P':
                    piece = Piece(PieceType::Pawn, Color::White);
                    break;

                case ' ':
                    done = true;
                    break;

                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                {
                    const int skip = (*current) - '0';
                    f += skip;
                    break;
                }

                case '/':
                    f = fileA;
                    --r;
                    break;

                default:
                    break;
                }

                if (done)
                {
                    break;
                }

                if (piece != Piece::none())
                {
                    place(piece, Square(f, r));
                    ++f;
                }

                ++current;
            }

            return current;
        }

        static constexpr Board fromFen(const char* fen)
        {
            Board board;
            (void)board.set(fen);
            return board;
        }

        [[nodiscard]] constexpr friend bool operator==(const Board& lhs, const Board& rhs) noexcept
        {
            bool equal = true;
            for (Square sq = a1; sq <= h8; ++sq)
            {
                if (lhs.m_pieces[sq] != rhs.m_pieces[sq])
                {
                    equal = false;
                    break;
                }
            }

            assert(bbsEqual(lhs, rhs) == equal);

            return equal;
        }

        constexpr void place(Piece piece, Square sq)
        {
            assert(sq.isOk());

            auto oldPiece = m_pieces[sq];
            m_pieceBB[oldPiece] ^= sq;
            if (oldPiece != Piece::none())
            {
                m_piecesByColorBB[oldPiece.color()] ^= sq;
            }
            m_pieces[sq] = piece;
            m_pieceBB[piece] |= sq;
            m_piecesByColorBB[piece.color()] |= sq;
            --m_pieceCount[oldPiece];
            ++m_pieceCount[piece];
        }

        // returns captured piece
        // doesn't check validity
        inline constexpr Piece doMove(Move move)
        {
            if (move.type == MoveType::Normal)
            {
                const Piece capturedPiece = m_pieces[move.to];
                const Piece piece = m_pieces[move.from];

                const Bitboard frombb = Bitboard::square(move.from);
                const Bitboard tobb = Bitboard::square(move.to);
                const Bitboard xormove = frombb ^ tobb;

                m_pieces[move.to] = piece;
                m_pieces[move.from] = Piece::none();

                m_pieceBB[piece] ^= xormove;

                m_piecesByColorBB[piece.color()] ^= xormove;

                if (capturedPiece == Piece::none())
                {
                    m_pieceBB[Piece::none()] ^= xormove;
                }
                else
                {
                    m_pieceBB[capturedPiece] ^= tobb;
                    m_pieceBB[Piece::none()] ^= frombb;

                    m_piecesByColorBB[capturedPiece.color()] ^= tobb;

                    --m_pieceCount[capturedPiece];
                    ++m_pieceCount[Piece::none()];
                }

                return capturedPiece;
            }

            return doMoveColdPath(move);
        }

        inline constexpr Piece doMoveColdPath(Move move)
        {
            if (move.type == MoveType::Promotion)
            {
                // We split it even though it's similar just because
                // the normal case is much more common.
                const Piece capturedPiece = m_pieces[move.to];
                const Piece fromPiece = m_pieces[move.from];
                const Piece toPiece = move.promotedPiece;

                m_pieces[move.to] = toPiece;
                m_pieces[move.from] = Piece::none();

                m_pieceBB[fromPiece] ^= move.from;
                m_pieceBB[toPiece] ^= move.to;

                m_pieceBB[capturedPiece] ^= move.to;
                m_pieceBB[Piece::none()] ^= move.from;

                m_piecesByColorBB[fromPiece.color()] ^= move.to;
                m_piecesByColorBB[fromPiece.color()] ^= move.from;
                if (capturedPiece != Piece::none())
                {
                    m_piecesByColorBB[capturedPiece.color()] ^= move.to;
                    --m_pieceCount[capturedPiece];
                    ++m_pieceCount[Piece::none()];
                }

                --m_pieceCount[fromPiece];
                ++m_pieceCount[toPiece];

                return capturedPiece;
            }
            else if (move.type == MoveType::EnPassant)
            {
                const Piece movedPiece = m_pieces[move.from];
                const Piece capturedPiece(PieceType::Pawn, !movedPiece.color());
                const Square capturedPieceSq(move.to.file(), move.from.rank());

                // on ep move there are 3 squares involved
                m_pieces[move.to] = movedPiece;
                m_pieces[move.from] = Piece::none();
                m_pieces[capturedPieceSq] = Piece::none();

                m_pieceBB[movedPiece] ^= move.from;
                m_pieceBB[movedPiece] ^= move.to;

                m_pieceBB[Piece::none()] ^= move.from;
                m_pieceBB[Piece::none()] ^= move.to;

                m_pieceBB[capturedPiece] ^= capturedPieceSq;
                m_pieceBB[Piece::none()] ^= capturedPieceSq;

                m_piecesByColorBB[movedPiece.color()] ^= move.to;
                m_piecesByColorBB[movedPiece.color()] ^= move.from;
                m_piecesByColorBB[capturedPiece.color()] ^= capturedPieceSq;

                --m_pieceCount[capturedPiece];
                ++m_pieceCount[Piece::none()];

                return capturedPiece;
            }
            else // if (move.type == MoveType::Castle)
            {
                const Square rookFromSq = move.to;
                const Square kingFromSq = move.from;

                const Piece rook = m_pieces[rookFromSq];
                const Piece king = m_pieces[kingFromSq];
                const Color color = king.color();

                const CastleType castleType = CastlingTraits::moveCastlingType(move);
                const Square rookToSq = CastlingTraits::rookDestination[color][castleType];
                const Square kingToSq = CastlingTraits::kingDestination[color][castleType];

                // 4 squares are involved
                m_pieces[rookFromSq] = Piece::none();
                m_pieces[kingFromSq] = Piece::none();
                m_pieces[rookToSq] = rook;
                m_pieces[kingToSq] = king;

                m_pieceBB[rook] ^= rookFromSq;
                m_pieceBB[rook] ^= rookToSq;

                m_pieceBB[king] ^= kingFromSq;
                m_pieceBB[king] ^= kingToSq;

                m_pieceBB[Piece::none()] ^= rookFromSq;
                m_pieceBB[Piece::none()] ^= rookToSq;

                m_pieceBB[Piece::none()] ^= kingFromSq;
                m_pieceBB[Piece::none()] ^= kingToSq;

                m_piecesByColorBB[color] ^= rookFromSq;
                m_piecesByColorBB[color] ^= rookToSq;
                m_piecesByColorBB[color] ^= kingFromSq;
                m_piecesByColorBB[color] ^= kingToSq;

                return Piece::none();
            }
        }

        constexpr void undoMove(Move move, Piece capturedPiece)
        {
            if (move.type == MoveType::Normal || move.type == MoveType::Promotion)
            {
                const Piece toPiece = m_pieces[move.to];
                const Piece fromPiece = move.promotedPiece == Piece::none() ? toPiece : Piece(PieceType::Pawn, toPiece.color());

                m_pieces[move.from] = fromPiece;
                m_pieces[move.to] = capturedPiece;

                m_pieceBB[fromPiece] ^= move.from;
                m_pieceBB[toPiece] ^= move.to;

                m_pieceBB[capturedPiece] ^= move.to;
                m_pieceBB[Piece::none()] ^= move.from;

                m_piecesByColorBB[fromPiece.color()] ^= move.to;
                m_piecesByColorBB[fromPiece.color()] ^= move.from;
                if (capturedPiece != Piece::none())
                {
                    m_piecesByColorBB[capturedPiece.color()] ^= move.to;
                    ++m_pieceCount[capturedPiece];
                    --m_pieceCount[Piece::none()];
                }

                if (move.type == MoveType::Promotion)
                {
                    --m_pieceCount[toPiece];
                    ++m_pieceCount[fromPiece];
                }
            }
            else if (move.type == MoveType::EnPassant)
            {
                const Piece movedPiece = m_pieces[move.to];
                const Piece capturedPiece_(PieceType::Pawn, !movedPiece.color());
                const Square capturedPieceSq(move.to.file(), move.from.rank());

                m_pieces[move.to] = Piece::none();
                m_pieces[move.from] = movedPiece;
                m_pieces[capturedPieceSq] = capturedPiece_;

                m_pieceBB[movedPiece] ^= move.from;
                m_pieceBB[movedPiece] ^= move.to;

                m_pieceBB[Piece::none()] ^= move.from;
                m_pieceBB[Piece::none()] ^= move.to;

                // on ep move there are 3 squares involved
                m_pieceBB[capturedPiece_] ^= capturedPieceSq;
                m_pieceBB[Piece::none()] ^= capturedPieceSq;

                m_piecesByColorBB[movedPiece.color()] ^= move.to;
                m_piecesByColorBB[movedPiece.color()] ^= move.from;
                m_piecesByColorBB[capturedPiece_.color()] ^= capturedPieceSq;

                ++m_pieceCount[capturedPiece_];
                --m_pieceCount[Piece::none()];
            }
            else // if (move.type == MoveType::Castle)
            {
                const Square rookFromSq = move.to;
                const Square kingFromSq = move.from;

                const Color color = move.to.rank() == rank1 ? Color::White : Color::Black;

                const CastleType castleType = CastlingTraits::moveCastlingType(move);
                const Square rookToSq = CastlingTraits::rookDestination[color][castleType];
                const Square kingToSq = CastlingTraits::kingDestination[color][castleType];

                const Piece rook = m_pieces[rookToSq];
                const Piece king = m_pieces[kingToSq];

                // 4 squares are involved
                m_pieces[rookFromSq] = rook;
                m_pieces[kingFromSq] = king;
                m_pieces[rookToSq] = Piece::none();
                m_pieces[kingToSq] = Piece::none();

                m_pieceBB[rook] ^= rookFromSq;
                m_pieceBB[rook] ^= rookToSq;

                m_pieceBB[king] ^= kingFromSq;
                m_pieceBB[king] ^= kingToSq;

                m_pieceBB[Piece::none()] ^= rookFromSq;
                m_pieceBB[Piece::none()] ^= rookToSq;

                m_pieceBB[Piece::none()] ^= kingFromSq;
                m_pieceBB[Piece::none()] ^= kingToSq;

                m_piecesByColorBB[color] ^= rookFromSq;
                m_piecesByColorBB[color] ^= rookToSq;
                m_piecesByColorBB[color] ^= kingFromSq;
                m_piecesByColorBB[color] ^= kingToSq;
            }
        }

        // Returns whether a given square is attacked by any piece
        // of `attackerColor` side.
        [[nodiscard]] inline bool isSquareAttacked(Square sq, Color attackerColor) const;

        // Returns whether a given square is attacked by any piece
        // of `attackerColor` side after `move` is made.
        // Move must be pseudo legal.
        [[nodiscard]] inline bool isSquareAttackedAfterMove(Move move, Square sq, Color attackerColor) const;

        // Move must be pseudo legal.
        // Must not be a king move.
        [[nodiscard]] inline bool createsDiscoveredAttackOnOwnKing(Move move) const;

        // Returns whether a piece on a given square is attacked
        // by any enemy piece. False if square is empty.
        [[nodiscard]] inline bool isPieceAttacked(Square sq) const;

        // Returns whether a piece on a given square is attacked
        // by any enemy piece after `move` is made. False if square is empty.
        // Move must be pseudo legal.
        [[nodiscard]] inline bool isPieceAttackedAfterMove(Move move, Square sq) const;

        // Returns whether the king of the moving side is attacked
        // by any enemy piece after a move is made.
        // Move must be pseudo legal.
        [[nodiscard]] inline bool isOwnKingAttackedAfterMove(Move move) const;

        // Return a bitboard with all (pseudo legal) attacks by the piece on
        // the given square. Empty if no piece on the square.
        [[nodiscard]] inline Bitboard attacks(Square sq) const;

        // Returns a bitboard with all squared that have pieces
        // that attack a given square (pseudo legally)
        [[nodiscard]] inline Bitboard attackers(Square sq, Color attackerColor) const;

        [[nodiscard]] constexpr Piece pieceAt(Square sq) const
        {
            assert(sq.isOk());

            return m_pieces[sq];
        }

        [[nodiscard]] constexpr Bitboard piecesBB(Color c) const
        {
            return m_piecesByColorBB[c];
        }

        [[nodiscard]] inline Square kingSquare(Color c) const
        {
            return piecesBB(Piece(PieceType::King, c)).first();
        }

        [[nodiscard]] constexpr Bitboard piecesBB(Piece pc) const
        {
            return m_pieceBB[pc];
        }

        [[nodiscard]] constexpr Bitboard piecesBB() const
        {
            Bitboard bb{};

            // don't collect from null piece
            return piecesBB(Color::White) | piecesBB(Color::Black);

            return bb;
        }

        [[nodiscard]] constexpr std::uint8_t pieceCount(Piece pt) const
        {
            return m_pieceCount[pt];
        }

        [[nodiscard]] constexpr bool isPromotion(Square from, Square to) const
        {
            assert(from.isOk() && to.isOk());

            return m_pieces[from].type() == PieceType::Pawn && (to.rank() == rank1 || to.rank() == rank8);
        }

        const Piece* piecesRaw() const;

    private:
        EnumArray<Square, Piece> m_pieces;
        EnumArray<Piece, Bitboard> m_pieceBB;
        EnumArray<Color, Bitboard> m_piecesByColorBB;
        EnumArray<Piece, uint8_t> m_pieceCount;

        // NOTE: currently we don't track it because it's not
        // required to perform ep if we don't need to check validity
        // Square m_epSquare = Square::none();

        [[nodiscard]] static constexpr bool bbsEqual(const Board& lhs, const Board& rhs) noexcept
        {
            for (Piece pc : values<Piece>())
            {
                if (lhs.m_pieceBB[pc] != rhs.m_pieceBB[pc])
                {
                    return false;
                }
            }

            return true;
        }
    };

    struct Position;

    struct CompressedPosition;

    struct PositionHash128
    {
        std::uint64_t high;
        std::uint64_t low;
    };

    struct Position;

    struct MoveLegalityChecker
    {
        MoveLegalityChecker(const Position& position);

        [[nodiscard]] bool isPseudoLegalMoveLegal(const Move& move) const;

    private:
        const Position* m_position;
        Bitboard m_checkers;
        Bitboard m_ourBlockersForKing;
        Bitboard m_potentialCheckRemovals;
        Square m_ksq;
    };

    struct Position : public Board
    {
        using BaseType = Board;

        constexpr Position() noexcept :
            Board(),
            m_sideToMove(Color::White),
            m_epSquare(Square::none()),
            m_castlingRights(CastlingRights::All),
            m_rule50Counter(0),
            m_ply(0)
        {
        }

        constexpr Position(const Board& board, Color sideToMove, Square epSquare, CastlingRights castlingRights) :
            Board(board),
            m_sideToMove(sideToMove),
            m_epSquare(epSquare),
            m_castlingRights(castlingRights),
            m_rule50Counter(0),
            m_ply(0)
        {
        }

        inline void set(std::string_view fen);

        // Returns false if the fen was not valid
        // If the returned value was false the position
        // is in unspecified state.
        [[nodiscard]] inline bool trySet(std::string_view fen);

        [[nodiscard]] static inline Position fromFen(std::string_view fen);

        [[nodiscard]] static inline std::optional<Position> tryFromFen(std::string_view fen);

        [[nodiscard]] static inline Position startPosition();

        [[nodiscard]] inline std::string fen() const;

        [[nodiscard]] MoveLegalityChecker moveLegalityChecker() const
        {
            return { *this };
        }

        constexpr void setEpSquareUnchecked(Square sq)
        {
            m_epSquare = sq;
        }

        void setEpSquare(Square sq)
        {
            m_epSquare = sq;
            nullifyEpSquareIfNotPossible();
        }

        constexpr void setSideToMove(Color color)
        {
            m_sideToMove = color;
        }

        constexpr void addCastlingRights(CastlingRights rights)
        {
            m_castlingRights |= rights;
        }

        constexpr void setCastlingRights(CastlingRights rights)
        {
            m_castlingRights = rights;
        }

        constexpr void setRule50Counter(std::uint8_t v)
        {
            m_rule50Counter = v;
        }

        constexpr void setPly(std::uint16_t ply)
        {
            m_ply = ply;
        }

        inline ReverseMove doMove(const Move& move);

        constexpr void undoMove(const ReverseMove& reverseMove)
        {
            const Move& move = reverseMove.move;
            BaseType::undoMove(move, reverseMove.capturedPiece);

            m_epSquare = reverseMove.oldEpSquare;
            m_castlingRights = reverseMove.oldCastlingRights;

            m_sideToMove = !m_sideToMove;

            --m_ply;
            if (m_rule50Counter > 0)
            {
                m_rule50Counter -= 1;
            }
        }

        [[nodiscard]] constexpr Color sideToMove() const
        {
            return m_sideToMove;
        }

        [[nodiscard]] inline std::uint8_t rule50Counter() const
        {
            return m_rule50Counter;
        }

        [[nodiscard]] inline std::uint16_t ply() const
        {
            return m_ply;
        }

        [[nodiscard]] inline std::uint16_t fullMove() const
        {
            return m_ply / 2 + 1;
        }

        inline void setFullMove(std::uint16_t hm)
        {
            m_ply = 2 * (hm - 1) + (m_sideToMove == Color::Black);
        }

        [[nodiscard]] inline bool isCheck() const;

        [[nodiscard]] inline Bitboard checkers() const;

        [[nodiscard]] inline bool isCheckAfterMove(Move move) const;

        [[nodiscard]] inline bool isMoveLegal(Move move) const;

        [[nodiscard]] inline bool isPseudoLegalMoveLegal(Move move) const;

        [[nodiscard]] inline bool isMovePseudoLegal(Move move) const;

        // Returns all pieces that block a slider
        // from attacking our king. When two or more
        // pieces block a single slider then none
        // of these pieces are included.
        [[nodiscard]] inline Bitboard blockersForKing(Color color) const;

        [[nodiscard]] constexpr Square epSquare() const
        {
            return m_epSquare;
        }

        [[nodiscard]] constexpr CastlingRights castlingRights() const
        {
            return m_castlingRights;
        }

        [[nodiscard]] constexpr bool friend operator==(const Position& lhs, const Position& rhs) noexcept
        {
            return
                lhs.m_sideToMove == rhs.m_sideToMove
                && lhs.m_epSquare == rhs.m_epSquare
                && lhs.m_castlingRights == rhs.m_castlingRights
                && static_cast<const Board&>(lhs) == static_cast<const Board&>(rhs);
        }

        [[nodiscard]] constexpr bool friend operator!=(const Position& lhs, const Position& rhs) noexcept
        {
            return !(lhs == rhs);
        }

        // these are supposed to be used only for testing
        // that's why there's this assert in afterMove

        [[nodiscard]] constexpr Position beforeMove(const ReverseMove& reverseMove) const
        {
            Position cpy(*this);
            cpy.undoMove(reverseMove);
            return cpy;
        }

        [[nodiscard]] inline Position afterMove(Move move) const;

        [[nodiscard]] constexpr bool isEpPossible() const
        {
            return m_epSquare != Square::none();
        }

        [[nodiscard]] inline CompressedPosition compress() const;

    protected:
        Color m_sideToMove;
        Square m_epSquare;
        CastlingRights m_castlingRights;
        std::uint8_t m_rule50Counter;
        std::uint16_t m_ply;

        static_assert(sizeof(Color) + sizeof(Square) + sizeof(CastlingRights) + sizeof(std::uint8_t) == 4);

        [[nodiscard]] inline bool isEpPossible(Square epSquare, Color sideToMove) const;

        [[nodiscard]] inline bool isEpPossibleColdPath(Square epSquare, Bitboard pawnsAttackingEpSquare, Color sideToMove) const;

        inline void nullifyEpSquareIfNotPossible();
    };

    struct CompressedPosition
    {
        friend struct Position;

        // Occupied bitboard has bits set for
        // each square with a piece on it.
        // Each packedState byte holds 2 values (nibbles).
        // First one at low bits, second one at high bits.
        // Values correspond to consecutive squares
        // in bitboard iteration order.
        // Nibble values:
        // these are the same as for Piece
        // knights, bishops, queens can just be copied
        //  0 : white pawn
        //  1 : black pawn
        //  2 : white knight
        //  3 : black knight
        //  4 : white bishop
        //  5 : black bishop
        //  6 : white rook
        //  7 : black rook
        //  8 : white queen
        //  9 : black queen
        // 10 : white king
        // 11 : black king
        //
        // these are special
        // 12 : pawn with ep square behind (white or black, depending on rank)
        // 13 : white rook with coresponding castling rights
        // 14 : black rook with coresponding castling rights
        // 15 : black king and black is side to move
        //
        // Let N be the number of bits set in occupied bitboard.
        // Only N nibbles are present. (N+1)/2 bytes are initialized.

        static CompressedPosition readFromBigEndian(const unsigned char* data)
        {
            CompressedPosition pos{};
            pos.m_occupied = Bitboard::fromBits(
                (std::uint64_t)data[0] << 56
                | (std::uint64_t)data[1] << 48
                | (std::uint64_t)data[2] << 40
                | (std::uint64_t)data[3] << 32
                | (std::uint64_t)data[4] << 24
                | (std::uint64_t)data[5] << 16
                | (std::uint64_t)data[6] << 8
                | (std::uint64_t)data[7]
                );
            std::memcpy(pos.m_packedState, data + 8, 16);
            return pos;
        }

        constexpr CompressedPosition() :
            m_occupied{},
            m_packedState{}
        {
        }

        [[nodiscard]] friend bool operator<(const CompressedPosition& lhs, const CompressedPosition& rhs)
        {
            if (lhs.m_occupied.bits() < rhs.m_occupied.bits()) return true;
            if (lhs.m_occupied.bits() > rhs.m_occupied.bits()) return false;

            return std::strcmp(reinterpret_cast<const char*>(lhs.m_packedState), reinterpret_cast<const char*>(rhs.m_packedState)) < 0;
        }

        [[nodiscard]] friend bool operator==(const CompressedPosition& lhs, const CompressedPosition& rhs)
        {
            return lhs.m_occupied == rhs.m_occupied
                && std::strcmp(reinterpret_cast<const char*>(lhs.m_packedState), reinterpret_cast<const char*>(rhs.m_packedState)) == 0;
        }

        [[nodiscard]] inline Position decompress() const;

        [[nodiscard]] constexpr Bitboard pieceBB() const
        {
            return m_occupied;
        }

        void writeToBigEndian(unsigned char* data)
        {
            const auto occupied = m_occupied.bits();
            *data++ = occupied >> 56;
            *data++ = (occupied >> 48) & 0xFF;
            *data++ = (occupied >> 40) & 0xFF;
            *data++ = (occupied >> 32) & 0xFF;
            *data++ = (occupied >> 24) & 0xFF;
            *data++ = (occupied >> 16) & 0xFF;
            *data++ = (occupied >> 8) & 0xFF;
            *data++ = occupied & 0xFF;
            std::memcpy(data, m_packedState, 16);
        }

    private:
        Bitboard m_occupied;
        std::uint8_t m_packedState[16];
    };

    namespace movegen
    {
        // For a pseudo-legal move the following are true:
        //  - the moving piece has the pos.sideToMove() color
        //  - the destination square is either empty or has a piece of the opposite color
        //  - if it is a pawn move it is valid (but may be illegal due to discovered checks)
        //  - if it is not a pawn move then the destination square is contained in attacks()
        //  - if it is a castling it is legal
        //  - a move other than castling may create a discovered attack on the king
        //  - a king may walk into a check

        template <typename FuncT>
        inline void forEachPseudoLegalPawnMove(const Position& pos, Square from, FuncT&& f)
        {
            const Color sideToMove = pos.sideToMove();
            const Square epSquare = pos.epSquare();
            const Bitboard ourPieces = pos.piecesBB(sideToMove);
            const Bitboard theirPieces = pos.piecesBB(!sideToMove);
            const Bitboard occupied = ourPieces | theirPieces;

            Bitboard attackTargets = theirPieces;
            if (epSquare != Square::none())
            {
                attackTargets |= epSquare;
            }

            const Bitboard attacks = bb::pawnAttacks(Bitboard::square(from), sideToMove) & attackTargets;

            const Rank secondToLastRank = sideToMove == Color::White ? rank7 : rank2;
            const auto forward = sideToMove == Color::White ? FlatSquareOffset(0, 1) : FlatSquareOffset(0, -1);

            // promotions
            if (from.rank() == secondToLastRank)
            {
                // capture promotions
                for (Square toSq : attacks)
                {
                    for (PieceType pt : { PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen })
                    {
                        Move move{ from, toSq, MoveType::Promotion, Piece(pt, sideToMove) };
                        f(move);
                    }
                }

                // push promotions
                const Square toSq = from + forward;
                if (!occupied.isSet(toSq))
                {
                    for (PieceType pt : { PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen })
                    {
                        Move move{ from, toSq, MoveType::Promotion, Piece(pt, sideToMove) };
                        f(move);
                    }
                }
            }
            else
            {
                // captures
                for (Square toSq : attacks)
                {
                    Move move{ from, toSq, (toSq == epSquare) ? MoveType::EnPassant : MoveType::Normal };
                    f(move);
                }

                const Square toSq = from + forward;

                // single push
                if (!occupied.isSet(toSq))
                {
                    const Rank startRank = sideToMove == Color::White ? rank2 : rank7;
                    if (from.rank() == startRank)
                    {
                        // double push
                        const Square toSq2 = toSq + forward;
                        if (!occupied.isSet(toSq2))
                        {
                            Move move{ from, toSq2 };
                            f(move);
                        }
                    }

                    Move move{ from, toSq };
                    f(move);
                }
            }
        }

        template <Color SideToMoveV, typename FuncT>
        inline void forEachPseudoLegalPawnMove(const Position& pos, FuncT&& f)
        {
            const Square epSquare = pos.epSquare();
            const Bitboard ourPieces = pos.piecesBB(SideToMoveV);
            const Bitboard theirPieces = pos.piecesBB(!SideToMoveV);
            const Bitboard occupied = ourPieces | theirPieces;
            const Bitboard pawns = pos.piecesBB(Piece(PieceType::Pawn, SideToMoveV));

            const Bitboard secondToLastRank = SideToMoveV == Color::White ? bb::rank7 : bb::rank2;
            const Bitboard secondRank = SideToMoveV == Color::White ? bb::rank2 : bb::rank7;

            const auto singlePawnMoveDestinationOffset = SideToMoveV == Color::White ? FlatSquareOffset(0, 1) : FlatSquareOffset(0, -1);
            const auto doublePawnMoveDestinationOffset = SideToMoveV == Color::White ? FlatSquareOffset(0, 2) : FlatSquareOffset(0, -2);

            {
                const int backward = SideToMoveV == Color::White ? -1 : 1;
                const int backward2 = backward * 2;

                const Bitboard doublePawnMoveStarts =
                    pawns
                    & secondRank
                    & ~(occupied.shiftedVertically(backward) | occupied.shiftedVertically(backward2));

                const Bitboard singlePawnMoveStarts =
                    pawns
                    & ~secondToLastRank
                    & ~occupied.shiftedVertically(backward);

                for (Square from : doublePawnMoveStarts)
                {
                    const Square to = from + doublePawnMoveDestinationOffset;
                    f(Move::normal(from, to));
                }

                for (Square from : singlePawnMoveStarts)
                {
                    const Square to = from + singlePawnMoveDestinationOffset;
                    f(Move::normal(from, to));
                }
            }

            {
                const Bitboard lastRank = SideToMoveV == Color::White ? bb::rank8 : bb::rank1;
                const FlatSquareOffset westCaptureOffset = SideToMoveV == Color::White ? FlatSquareOffset(-1, 1) : FlatSquareOffset(-1, -1);
                const FlatSquareOffset eastCaptureOffset = SideToMoveV == Color::White ? FlatSquareOffset(1, 1) : FlatSquareOffset(1, -1);

                const Bitboard pawnsWithWestCapture = bb::eastPawnAttacks(theirPieces & ~lastRank, !SideToMoveV) & pawns;
                const Bitboard pawnsWithEastCapture = bb::westPawnAttacks(theirPieces & ~lastRank, !SideToMoveV) & pawns;

                for (Square from : pawnsWithWestCapture)
                {
                    f(Move::normal(from, from + westCaptureOffset));
                }

                for (Square from : pawnsWithEastCapture)
                {
                    f(Move::normal(from, from + eastCaptureOffset));
                }
            }

            if (epSquare != Square::none())
            {
                const Bitboard pawnsThatCanCapture = bb::pawnAttacks(Bitboard::square(epSquare), !SideToMoveV) & pawns;
                for (Square from : pawnsThatCanCapture)
                {
                    f(Move::enPassant(from, epSquare));
                }
            }

            for (Square from : pawns & secondToLastRank)
            {
                const Bitboard attacks = bb::pawnAttacks(Bitboard::square(from), SideToMoveV) & theirPieces;

                // capture promotions
                for (Square to : attacks)
                {
                    for (PieceType pt : { PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen })
                    {
                        Move move{ from, to, MoveType::Promotion, Piece(pt, SideToMoveV) };
                        f(move);
                    }
                }

                // push promotions
                const Square to = from + singlePawnMoveDestinationOffset;
                if (!occupied.isSet(to))
                {
                    for (PieceType pt : { PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen })
                    {
                        Move move{ from, to, MoveType::Promotion, Piece(pt, SideToMoveV) };
                        f(move);
                    }
                }
            }
        }

        template <typename FuncT>
        inline void forEachPseudoLegalPawnMove(const Position& pos, FuncT&& f)
        {
            if (pos.sideToMove() == Color::White)
            {
                forEachPseudoLegalPawnMove<Color::White>(pos, std::forward<FuncT>(f));
            }
            else
            {
                forEachPseudoLegalPawnMove<Color::Black>(pos, std::forward<FuncT>(f));
            }
        }

        template <PieceType PieceTypeV, typename FuncT>
        inline void forEachPseudoLegalPieceMove(const Position& pos, Square from, FuncT&& f)
        {
            static_assert(PieceTypeV != PieceType::None);

            if constexpr (PieceTypeV == PieceType::Pawn)
            {
                forEachPseudoLegalPawnMove(pos, from, f);
            }
            else
            {
                const Color sideToMove = pos.sideToMove();
                const Bitboard ourPieces = pos.piecesBB(sideToMove);
                const Bitboard theirPieces = pos.piecesBB(!sideToMove);
                const Bitboard occupied = ourPieces | theirPieces;
                const Bitboard attacks = bb::attacks<PieceTypeV>(from, occupied) & ~ourPieces;

                for (Square toSq : attacks)
                {
                    Move move{ from, toSq };
                    f(move);
                }
            }
        }

        template <PieceType PieceTypeV, typename FuncT>
        inline void forEachPseudoLegalPieceMove(const Position& pos, FuncT&& f)
        {
            static_assert(PieceTypeV != PieceType::None);

            if constexpr (PieceTypeV == PieceType::Pawn)
            {
                forEachPseudoLegalPawnMove(pos, f);
            }
            else
            {
                const Color sideToMove = pos.sideToMove();
                const Bitboard ourPieces = pos.piecesBB(sideToMove);
                const Bitboard theirPieces = pos.piecesBB(!sideToMove);
                const Bitboard occupied = ourPieces | theirPieces;
                const Bitboard pieces = pos.piecesBB(Piece(PieceTypeV, sideToMove));
                for (Square fromSq : pieces)
                {
                    const Bitboard attacks = bb::attacks<PieceTypeV>(fromSq, occupied) & ~ourPieces;
                    for (Square toSq : attacks)
                    {
                        Move move{ fromSq, toSq };
                        f(move);
                    }
                }
            }
        }

        template <typename FuncT>
        inline void forEachCastlingMove(const Position& pos, FuncT&& f)
        {
            CastlingRights rights = pos.castlingRights();
            if (rights == CastlingRights::None)
            {
                return;
            }

            const Color sideToMove = pos.sideToMove();
            const Bitboard ourPieces = pos.piecesBB(sideToMove);
            const Bitboard theirPieces = pos.piecesBB(!sideToMove);
            const Bitboard occupied = ourPieces | theirPieces;

            // we first reduce the set of legal castlings by checking the paths for pieces
            if (sideToMove == Color::White)
            {
                if ((CastlingTraits::castlingPath[Color::White][CastleType::Short] & occupied).any()) rights &= ~CastlingRights::WhiteKingSide;
                if ((CastlingTraits::castlingPath[Color::White][CastleType::Long] & occupied).any()) rights &= ~CastlingRights::WhiteQueenSide;
                rights &= ~CastlingRights::Black;
            }
            else
            {
                if ((CastlingTraits::castlingPath[Color::Black][CastleType::Short] & occupied).any()) rights &= ~CastlingRights::BlackKingSide;
                if ((CastlingTraits::castlingPath[Color::Black][CastleType::Long] & occupied).any()) rights &= ~CastlingRights::BlackQueenSide;
                rights &= ~CastlingRights::White;
            }

            if (rights == CastlingRights::None)
            {
                return;
            }

            // King must not be in check. Done here because it is quite expensive.
            const Square ksq = pos.kingSquare(sideToMove);
            if (pos.isSquareAttacked(ksq, !sideToMove))
            {
                return;
            }

            // Loop through all possible castlings.
            for (CastleType castlingType : values<CastleType>())
            {
                const CastlingRights right = CastlingTraits::castlingRights[sideToMove][castlingType];

                if (!contains(rights, right))
                {
                    continue;
                }

                // If we have this castling right
                // we check whether the king passes an attacked square.
                const Square passedSquare = CastlingTraits::squarePassedByKing[sideToMove][castlingType];
                if (pos.isSquareAttacked(passedSquare, !sideToMove))
                {
                    continue;
                }

                // If it's a castling move then the change in square occupation
                // cannot have an effect because otherwise there would be
                // a slider attacker attacking the castling king.
                if (pos.isSquareAttacked(CastlingTraits::kingDestination[sideToMove][castlingType], !sideToMove))
                {
                    continue;
                }

                // If not we can castle.
                Move move = Move::castle(castlingType, sideToMove);
                f(move);
            }
        }

        // Calls a given function for all pseudo legal moves for the position.
        // `pos` must be a legal chess position
        template <typename FuncT>
        inline void forEachPseudoLegalMove(const Position& pos, FuncT&& func)
        {
            forEachPseudoLegalPieceMove<PieceType::Pawn>(pos, func);
            forEachPseudoLegalPieceMove<PieceType::Knight>(pos, func);
            forEachPseudoLegalPieceMove<PieceType::Bishop>(pos, func);
            forEachPseudoLegalPieceMove<PieceType::Rook>(pos, func);
            forEachPseudoLegalPieceMove<PieceType::Queen>(pos, func);
            forEachPseudoLegalPieceMove<PieceType::King>(pos, func);
            forEachCastlingMove(pos, func);
        }

        // Calls a given function for all legal moves for the position.
        // `pos` must be a legal chess position
        template <typename FuncT>
        inline void forEachLegalMove(const Position& pos, FuncT&& func)
        {
            auto funcIfLegal = [&func, checker = pos.moveLegalityChecker()](Move move) {
                if (checker.isPseudoLegalMoveLegal(move))
                {
                    func(move);
                }
            };

            forEachPseudoLegalPieceMove<PieceType::Pawn>(pos, funcIfLegal);
            forEachPseudoLegalPieceMove<PieceType::Knight>(pos, funcIfLegal);
            forEachPseudoLegalPieceMove<PieceType::Bishop>(pos, funcIfLegal);
            forEachPseudoLegalPieceMove<PieceType::Rook>(pos, funcIfLegal);
            forEachPseudoLegalPieceMove<PieceType::Queen>(pos, funcIfLegal);
            forEachPseudoLegalPieceMove<PieceType::King>(pos, funcIfLegal);
            forEachCastlingMove(pos, func);
        }

        // Generates all pseudo legal moves for the position.
        // `pos` must be a legal chess position
        [[nodiscard]] std::vector<Move> generatePseudoLegalMoves(const Position& pos);

        // Generates all legal moves for the position.
        // `pos` must be a legal chess position
        [[nodiscard]] std::vector<Move> generateLegalMoves(const Position& pos);
    }

    [[nodiscard]] inline bool Position::isCheck() const
    {
        return BaseType::isSquareAttacked(kingSquare(m_sideToMove), !m_sideToMove);
    }

    [[nodiscard]] inline Bitboard Position::checkers() const
    {
        return BaseType::attackers(kingSquare(m_sideToMove), !m_sideToMove);
    }

    [[nodiscard]] inline bool Position::isCheckAfterMove(Move move) const
    {
        return BaseType::isSquareAttackedAfterMove(move, kingSquare(!m_sideToMove), m_sideToMove);
    }

    [[nodiscard]] inline bool Position::isMoveLegal(Move move) const
    {
        return
            isMovePseudoLegal(move)
            && isPseudoLegalMoveLegal(move);
    }

    [[nodiscard]] inline bool Position::isPseudoLegalMoveLegal(Move move) const
    {
        return
            (move.type == MoveType::Castle)
            || !isOwnKingAttackedAfterMove(move);
    }

    [[nodiscard]] inline bool Position::isMovePseudoLegal(Move move) const
    {
        if (!move.from.isOk() || !move.to.isOk())
        {
            return false;
        }

        if (move.from == move.to)
        {
            return false;
        }

        if (move.type != MoveType::Promotion && move.promotedPiece != Piece::none())
        {
            return false;
        }

        const Piece movedPiece = pieceAt(move.from);
        if (movedPiece == Piece::none())
        {
            return false;
        }

        if (movedPiece.color() != m_sideToMove)
        {
            return false;
        }

        const Bitboard occupied = piecesBB();
        const Bitboard ourPieces = piecesBB(m_sideToMove);
        const bool isNormal = move.type == MoveType::Normal;

        switch (movedPiece.type())
        {
        case PieceType::Pawn:
        {
            bool isValid = false;
            // TODO: use iterators so we don't loop over all moves
            //       when we can avoid it.
            movegen::forEachPseudoLegalPawnMove(*this, move.from, [&isValid, &move](const Move& genMove) {
                if (move == genMove)
                {
                    isValid = true;
                }
                });
            return isValid;
        }

        case PieceType::Bishop:
            return isNormal && (bb::attacks<PieceType::Bishop>(move.from, occupied) & ~ourPieces).isSet(move.to);

        case PieceType::Knight:
            return isNormal && (bb::pseudoAttacks<PieceType::Knight>(move.from) & ~ourPieces).isSet(move.to);

        case PieceType::Rook:
            return isNormal && (bb::attacks<PieceType::Rook>(move.from, occupied) & ~ourPieces).isSet(move.to);

        case PieceType::Queen:
            return isNormal && (bb::attacks<PieceType::Queen>(move.from, occupied) & ~ourPieces).isSet(move.to);

        case PieceType::King:
        {
            if (move.type == MoveType::Castle)
            {
                bool isValid = false;
                movegen::forEachCastlingMove(*this, [&isValid, &move](const Move& genMove) {
                    if (move == genMove)
                    {
                        isValid = true;
                    }
                    });
                return isValid;
            }
            else
            {
                return isNormal && (bb::pseudoAttacks<PieceType::King>(move.from) & ~ourPieces).isSet(move.to);
            }
        }

        default:
            return false;
        }
    }

    [[nodiscard]] inline Bitboard Position::blockersForKing(Color color) const
    {
        const Color attackerColor = !color;

        const Bitboard occupied = piecesBB();

        const Bitboard bishops = piecesBB(Piece(PieceType::Bishop, attackerColor));
        const Bitboard rooks = piecesBB(Piece(PieceType::Rook, attackerColor));
        const Bitboard queens = piecesBB(Piece(PieceType::Queen, attackerColor));

        const Square ksq = kingSquare(color);

        const Bitboard opponentBishopLikePieces = (bishops | queens);
        const Bitboard bishopPseudoAttacks = bb::pseudoAttacks<PieceType::Bishop>(ksq);

        const Bitboard opponentRookLikePieces = (rooks | queens);
        const Bitboard rookPseudoAttacks = bb::pseudoAttacks<PieceType::Rook>(ksq);

        const Bitboard xrayers =
            (bishopPseudoAttacks & opponentBishopLikePieces)
            | (rookPseudoAttacks & opponentRookLikePieces);

        Bitboard allBlockers = Bitboard::none();

        for (Square xrayer : xrayers)
        {
            const Bitboard blockers = bb::between(xrayer, ksq) & occupied;
            if (blockers.exactlyOne())
            {
                allBlockers |= blockers;
            }
        }

        return allBlockers;
    }

    inline MoveLegalityChecker::MoveLegalityChecker(const Position& position) :
        m_position(&position),
        m_checkers(position.checkers()),
        m_ourBlockersForKing(
            position.blockersForKing(position.sideToMove())
            & position.piecesBB(position.sideToMove())
        ),
        m_ksq(position.kingSquare(position.sideToMove()))
    {
        if (m_checkers.exactlyOne())
        {
            const Bitboard knightCheckers = m_checkers & bb::pseudoAttacks<PieceType::Knight>(m_ksq);
            if (knightCheckers.any())
            {
                // We're checked by a knight, we have to remove it or move the king.
                m_potentialCheckRemovals = knightCheckers;
            }
            else
            {
                // If we're not checked by a knight we can block it.
                m_potentialCheckRemovals = bb::between(m_ksq, m_checkers.first()) | m_checkers;
            }
        }
        else
        {
            // Double check, king has to move.
            m_potentialCheckRemovals = Bitboard::none();
        }
    }

    [[nodiscard]] inline bool MoveLegalityChecker::isPseudoLegalMoveLegal(const Move& move) const
    {
        if (m_checkers.any())
        {
            if (move.from == m_ksq || move.type == MoveType::EnPassant)
            {
                return m_position->isPseudoLegalMoveLegal(move);
            }
            else
            {
                // This means there's only one check and we either
                // blocked it or removed the piece that attacked
                // our king. So the only threat is if it's a discovered check.
                return
                    m_potentialCheckRemovals.isSet(move.to)
                    && !m_ourBlockersForKing.isSet(move.from);
            }
        }
        else
        {
            if (move.from == m_ksq)
            {
                return m_position->isPseudoLegalMoveLegal(move);
            }
            else if (move.type == MoveType::EnPassant)
            {
                return !m_position->createsDiscoveredAttackOnOwnKing(move);
            }
            else if (m_ourBlockersForKing.isSet(move.from))
            {
                // If it was a blocker it may have only moved in line with our king.
                // Otherwise it's a discovered check.
                return bb::line(m_ksq, move.from).isSet(move.to);
            }
            else
            {
                return true;
            }
        }
    }

    static_assert(sizeof(CompressedPosition) == 24);
    static_assert(std::is_trivially_copyable_v<CompressedPosition>);

    namespace detail
    {
        [[nodiscard]] FORCEINLINE constexpr std::uint8_t compressOrdinaryPiece(const Position&, Square, Piece piece)
        {
            return static_cast<std::uint8_t>(ordinal(piece));
        }

        [[nodiscard]] FORCEINLINE constexpr std::uint8_t compressPawn(const Position& position, Square sq, Piece piece)
        {
            const Square epSquare = position.epSquare();
            if (epSquare == Square::none())
            {
                return static_cast<std::uint8_t>(ordinal(piece));
            }
            else
            {
                const Color sideToMove = position.sideToMove();
                const Rank rank = sq.rank();
                const File file = sq.file();
                // use bitwise operators, there is a lot of unpredictable branches but in
                // total the result is quite predictable
                if (
                    (file == epSquare.file())
                    && (
                    ((rank == rank4) & (sideToMove == Color::Black))
                        | ((rank == rank5) & (sideToMove == Color::White))
                        )
                    )
                {
                    return 12;
                }
                else
                {
                    return static_cast<std::uint8_t>(ordinal(piece));
                }
            }
        }

        [[nodiscard]] FORCEINLINE constexpr std::uint8_t compressRook(const Position& position, Square sq, Piece piece)
        {
            const CastlingRights castlingRights = position.castlingRights();
            const Color color = piece.color();

            if (color == Color::White
                && (
                (sq == a1 && contains(castlingRights, CastlingRights::WhiteQueenSide))
                    || (sq == h1 && contains(castlingRights, CastlingRights::WhiteKingSide))
                    )
                )
            {
                return 13;
            }
            else if (
                color == Color::Black
                && (
                (sq == a8 && contains(castlingRights, CastlingRights::BlackQueenSide))
                    || (sq == h8 && contains(castlingRights, CastlingRights::BlackKingSide))
                    )
                )
            {
                return 14;
            }
            else
            {
                return static_cast<std::uint8_t>(ordinal(piece));
            }
        }

        [[nodiscard]] FORCEINLINE constexpr std::uint8_t compressKing(const Position& position, Square /* sq */, Piece piece)
        {
            const Color color = piece.color();
            const Color sideToMove = position.sideToMove();

            if (color == Color::White)
            {
                return 10;
            }
            else if (sideToMove == Color::White)
            {
                return 11;
            }
            else
            {
                return 15;
            }
        }
    }

    namespace detail::lookup
    {
        static constexpr EnumArray<PieceType, std::uint8_t(*)(const Position&, Square, Piece)> pieceCompressorFunc = []() {
            EnumArray<PieceType, std::uint8_t(*)(const Position&, Square, Piece)> pieceCompressorFunc_{};

            pieceCompressorFunc_[PieceType::Knight] = detail::compressOrdinaryPiece;
            pieceCompressorFunc_[PieceType::Bishop] = detail::compressOrdinaryPiece;
            pieceCompressorFunc_[PieceType::Queen] = detail::compressOrdinaryPiece;

            pieceCompressorFunc_[PieceType::Pawn] = detail::compressPawn;
            pieceCompressorFunc_[PieceType::Rook] = detail::compressRook;
            pieceCompressorFunc_[PieceType::King] = detail::compressKing;

            pieceCompressorFunc_[PieceType::None] = [](const Position&, Square, Piece) -> std::uint8_t { /* should never happen */ return 0; };

            return pieceCompressorFunc_;
        }();
    }

    [[nodiscard]] inline CompressedPosition Position::compress() const
    {
        auto compressPiece = [this](Square sq, Piece piece) -> std::uint8_t {
            if (piece.type() == PieceType::Pawn) // it's likely to be a pawn
            {
                return detail::compressPawn(*this, sq, piece);
            }
            else
            {
                return detail::lookup::pieceCompressorFunc[piece.type()](*this, sq, piece);
            }
        };

        const Bitboard occ = piecesBB();

        CompressedPosition compressed;
        compressed.m_occupied = occ;

        auto it = occ.begin();
        auto end = occ.end();
        for (int i = 0;; ++i)
        {
            if (it == end) break;
            compressed.m_packedState[i] = compressPiece(*it, pieceAt(*it));
            ++it;

            if (it == end) break;
            compressed.m_packedState[i] |= compressPiece(*it, pieceAt(*it)) << 4;
            ++it;
        }

        return compressed;
    }

    [[nodiscard]] inline Position CompressedPosition::decompress() const
    {
        Position pos;
        pos.setCastlingRights(CastlingRights::None);

        auto decompressPiece = [&pos](Square sq, std::uint8_t nibble) {
            switch (nibble)
            {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            {
                pos.place(fromOrdinal<Piece>(nibble), sq);
                return;
            }

            case 12:
            {
                const Rank rank = sq.rank();
                if (rank == rank4)
                {
                    pos.place(whitePawn, sq);
                    pos.setEpSquareUnchecked(sq + Offset{ 0, -1 });
                }
                else // (rank == rank5)
                {
                    pos.place(blackPawn, sq);
                    pos.setEpSquareUnchecked(sq + Offset{ 0, 1 });
                }
                return;
            }

            case 13:
            {
                pos.place(whiteRook, sq);
                if (sq == a1)
                {
                    pos.addCastlingRights(CastlingRights::WhiteQueenSide);
                }
                else // (sq == H1)
                {
                    pos.addCastlingRights(CastlingRights::WhiteKingSide);
                }
                return;
            }

            case 14:
            {
                pos.place(blackRook, sq);
                if (sq == a8)
                {
                    pos.addCastlingRights(CastlingRights::BlackQueenSide);
                }
                else // (sq == H8)
                {
                    pos.addCastlingRights(CastlingRights::BlackKingSide);
                }
                return;
            }

            case 15:
            {
                pos.place(blackKing, sq);
                pos.setSideToMove(Color::Black);
                return;
            }

            }

            return;
        };

        const Bitboard occ = m_occupied;

        auto it = occ.begin();
        auto end = occ.end();
        for (int i = 0;; ++i)
        {
            if (it == end) break;
            decompressPiece(*it, m_packedState[i] & 0xF);
            ++it;

            if (it == end) break;
            decompressPiece(*it, m_packedState[i] >> 4);
            ++it;
        }

        return pos;
    }


    [[nodiscard]] bool Board::isSquareAttacked(Square sq, Color attackerColor) const
    {
        assert(sq.isOk());

        const Bitboard occupied = piecesBB();
        const Bitboard bishops = piecesBB(Piece(PieceType::Bishop, attackerColor));
        const Bitboard rooks = piecesBB(Piece(PieceType::Rook, attackerColor));
        const Bitboard queens = piecesBB(Piece(PieceType::Queen, attackerColor));

        const Bitboard allSliders = (bishops | rooks | queens);
        if ((bb::pseudoAttacks<PieceType::Queen>(sq) & allSliders).any())
        {
            if (bb::isAttackedBySlider(
                sq,
                bishops,
                rooks,
                queens,
                occupied
            ))
            {
                return true;
            }
        }

        const Bitboard king = piecesBB(Piece(PieceType::King, attackerColor));
        if ((bb::pseudoAttacks<PieceType::King>(sq) & king).any())
        {
            return true;
        }

        const Bitboard knights = piecesBB(Piece(PieceType::Knight, attackerColor));
        if ((bb::pseudoAttacks<PieceType::Knight>(sq) & knights).any())
        {
            return true;
        }

        const Bitboard pawns = piecesBB(Piece(PieceType::Pawn, attackerColor));
        const Bitboard pawnAttacks = bb::pawnAttacks(pawns, attackerColor);

        return pawnAttacks.isSet(sq);
    }

    [[nodiscard]] bool Board::isSquareAttackedAfterMove(Move move, Square sq, Color attackerColor) const
    {
        const Bitboard occupiedChange = Bitboard::square(move.from) | move.to;

        Bitboard occupied = (piecesBB() ^ move.from) | move.to;

        Bitboard bishops = piecesBB(Piece(PieceType::Bishop, attackerColor));
        Bitboard rooks = piecesBB(Piece(PieceType::Rook, attackerColor));
        Bitboard queens = piecesBB(Piece(PieceType::Queen, attackerColor));
        Bitboard king = piecesBB(Piece(PieceType::King, attackerColor));
        Bitboard knights = piecesBB(Piece(PieceType::Knight, attackerColor));
        Bitboard pawns = piecesBB(Piece(PieceType::Pawn, attackerColor));

        if (move.type == MoveType::EnPassant)
        {
            const Square capturedPawnSq(move.to.file(), move.from.rank());
            occupied ^= capturedPawnSq;
            pawns ^= capturedPawnSq;
        }
        else if (pieceAt(move.to) != Piece::none())
        {
            const Bitboard notCaptured = ~Bitboard::square(move.to);
            bishops &= notCaptured;
            rooks &= notCaptured;
            queens &= notCaptured;
            knights &= notCaptured;
            pawns &= notCaptured;
        }

        // Potential attackers may have moved.
        const Piece movedPiece = pieceAt(move.from);
        if (movedPiece.color() == attackerColor)
        {
            switch (movedPiece.type())
            {
            case PieceType::Pawn:
                pawns ^= occupiedChange;
                break;
            case PieceType::Knight:
                knights ^= occupiedChange;
                break;
            case PieceType::Bishop:
                bishops ^= occupiedChange;
                break;
            case PieceType::Rook:
                rooks ^= occupiedChange;
                break;
            case PieceType::Queen:
                queens ^= occupiedChange;
                break;
            case PieceType::King:
            {
                if (move.type == MoveType::Castle)
                {
                    const CastleType castleType = CastlingTraits::moveCastlingType(move);

                    king ^= move.from;
                    king ^= CastlingTraits::kingDestination[attackerColor][castleType];
                    rooks ^= move.to;
                    rooks ^= CastlingTraits::rookDestination[attackerColor][castleType];
                }
                else
                {
                    king ^= occupiedChange;
                }

                break;
            }
            case PieceType::None:
                assert(false);
            }
        }

        // If it's a castling move then the change in square occupation
        // cannot have an effect because otherwise there would be
        // a slider attacker attacking the castling king.
        // (It could have an effect in chess960 if the slider
        // attacker was behind the rook involved in castling,
        // but we don't care about chess960.)

        const Bitboard allSliders = (bishops | rooks | queens);
        if ((bb::pseudoAttacks<PieceType::Queen>(sq) & allSliders).any())
        {
            if (bb::isAttackedBySlider(
                sq,
                bishops,
                rooks,
                queens,
                occupied
            ))
            {
                return true;
            }
        }

        if ((bb::pseudoAttacks<PieceType::King>(sq) & king).any())
        {
            return true;
        }

        if ((bb::pseudoAttacks<PieceType::Knight>(sq) & knights).any())
        {
            return true;
        }

        const Bitboard pawnAttacks = bb::pawnAttacks(pawns, attackerColor);

        return pawnAttacks.isSet(sq);
    }

    [[nodiscard]] bool Board::createsDiscoveredAttackOnOwnKing(Move move) const
    {
        Bitboard occupied = (piecesBB() ^ move.from) | move.to;

        const Piece movedPiece = pieceAt(move.from);
        const Color kingColor = movedPiece.color();
        const Color attackerColor = !kingColor;
        const Square ksq = kingSquare(kingColor);

        Bitboard bishops = piecesBB(Piece(PieceType::Bishop, attackerColor));
        Bitboard rooks = piecesBB(Piece(PieceType::Rook, attackerColor));
        Bitboard queens = piecesBB(Piece(PieceType::Queen, attackerColor));

        if (move.type == MoveType::EnPassant)
        {
            const Square capturedPawnSq(move.to.file(), move.from.rank());
            occupied ^= capturedPawnSq;
        }
        else if (pieceAt(move.to) != Piece::none())
        {
            const Bitboard notCaptured = ~Bitboard::square(move.to);
            bishops &= notCaptured;
            rooks &= notCaptured;
            queens &= notCaptured;
        }

        const Bitboard allSliders = (bishops | rooks | queens);
        if ((bb::pseudoAttacks<PieceType::Queen>(ksq) & allSliders).any())
        {
            if (bb::isAttackedBySlider(
                ksq,
                bishops,
                rooks,
                queens,
                occupied
            ))
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool Board::isPieceAttacked(Square sq) const
    {
        const Piece piece = pieceAt(sq);

        if (piece == Piece::none())
        {
            return false;
        }

        return isSquareAttacked(sq, !piece.color());
    }

    [[nodiscard]] bool Board::isPieceAttackedAfterMove(Move move, Square sq) const
    {
        const Piece piece = pieceAt(sq);

        if (piece == Piece::none())
        {
            return false;
        }

        if (sq == move.from)
        {
            // We moved the piece we're interested in.
            // For every move the piece ends up on the move.to except
            // for the case of castling moves.
            // But we know pseudo legal castling moves
            // are already legal, so the king cannot be in check after.
            if (move.type == MoveType::Castle)
            {
                return false;
            }

            // So update the square we're interested in.
            sq = move.to;
        }

        return isSquareAttackedAfterMove(move, sq, !piece.color());
    }

    [[nodiscard]] bool Board::isOwnKingAttackedAfterMove(Move move) const
    {
        if (move.type == MoveType::Castle)
        {
            // Pseudo legal castling moves are already legal.
            // This is ensured by the move generator.
            return false;
        }

        const Piece movedPiece = pieceAt(move.from);

        return isPieceAttackedAfterMove(move, kingSquare(movedPiece.color()));
    }

    [[nodiscard]] Bitboard Board::attacks(Square sq) const
    {
        const Piece piece = pieceAt(sq);
        if (piece == Piece::none())
        {
            return Bitboard::none();
        }

        if (piece.type() == PieceType::Pawn)
        {
            return bb::pawnAttacks(Bitboard::square(sq), piece.color());
        }
        else
        {
            return bb::attacks(piece.type(), sq, piecesBB());
        }
    }

    [[nodiscard]] Bitboard Board::attackers(Square sq, Color attackerColor) const
    {
        // En-passant square is not included.

        Bitboard allAttackers = Bitboard::none();

        const Bitboard occupied = piecesBB();

        const Bitboard bishops = piecesBB(Piece(PieceType::Bishop, attackerColor));
        const Bitboard rooks = piecesBB(Piece(PieceType::Rook, attackerColor));
        const Bitboard queens = piecesBB(Piece(PieceType::Queen, attackerColor));

        const Bitboard bishopLikePieces = (bishops | queens);
        const Bitboard bishopAttacks = bb::attacks<PieceType::Bishop>(sq, occupied);
        allAttackers |= bishopAttacks & bishopLikePieces;

        const Bitboard rookLikePieces = (rooks | queens);
        const Bitboard rookAttacks = bb::attacks<PieceType::Rook>(sq, occupied);
        allAttackers |= rookAttacks & rookLikePieces;

        const Bitboard king = piecesBB(Piece(PieceType::King, attackerColor));
        allAttackers |= bb::pseudoAttacks<PieceType::King>(sq) & king;

        const Bitboard knights = piecesBB(Piece(PieceType::Knight, attackerColor));
        allAttackers |= bb::pseudoAttacks<PieceType::Knight>(sq) & knights;

        const Bitboard pawns = piecesBB(Piece(PieceType::Pawn, attackerColor));
        allAttackers |= bb::pawnAttacks(Bitboard::square(sq), !attackerColor) & pawns;

        return allAttackers;
    }

    inline const Piece* Board::piecesRaw() const
    {
        return m_pieces.data();
    }

    namespace detail::lookup
    {
        static constexpr EnumArray<Piece, char> fenPiece = []() {
            EnumArray<Piece, char> fenPiece_{};

            fenPiece_[whitePawn] = 'P';
            fenPiece_[blackPawn] = 'p';
            fenPiece_[whiteKnight] = 'N';
            fenPiece_[blackKnight] = 'n';
            fenPiece_[whiteBishop] = 'B';
            fenPiece_[blackBishop] = 'b';
            fenPiece_[whiteRook] = 'R';
            fenPiece_[blackRook] = 'r';
            fenPiece_[whiteQueen] = 'Q';
            fenPiece_[blackQueen] = 'q';
            fenPiece_[whiteKing] = 'K';
            fenPiece_[blackKing] = 'k';
            fenPiece_[Piece::none()] = 'X';

            return fenPiece_;
        }();
    }

    [[nodiscard]] inline std::string Board::fen() const
    {
        std::string fen;
        fen.reserve(96); // longest fen is probably in range of around 88

        Rank rank = rank8;
        File file = fileA;
        std::uint8_t emptyCounter = 0;

        for (;;)
        {
            const Square sq(file, rank);
            const Piece piece = m_pieces[sq];

            if (piece == Piece::none())
            {
                ++emptyCounter;
            }
            else
            {
                if (emptyCounter != 0)
                {
                    fen.push_back(static_cast<char>(emptyCounter) + '0');
                    emptyCounter = 0;
                }

                fen.push_back(detail::lookup::fenPiece[piece]);
            }

            ++file;
            if (file > fileH)
            {
                file = fileA;
                --rank;

                if (emptyCounter != 0)
                {
                    fen.push_back(static_cast<char>(emptyCounter) + '0');
                    emptyCounter = 0;
                }

                if (rank < rank1)
                {
                    break;
                }
                fen.push_back('/');
            }
        }

        return fen;
    }

    void Position::set(std::string_view fen)
    {
        (void)trySet(fen);
    }

    // Returns false if the fen was not valid
    // If the returned value was false the position
    // is in unspecified state.
    [[nodiscard]] bool Position::trySet(std::string_view fen)
    {
        // Lazily splits by ' '. Returns empty string views if at the end.
        auto nextPart = [fen, start = std::size_t{ 0 }]() mutable {
            std::size_t end = fen.find(' ', start);
            if (end == std::string::npos)
            {
                std::string_view substr = fen.substr(start);
                start = fen.size();
                return substr;
            }
            else
            {
                std::string_view substr = fen.substr(start, end - start);
                start = end + 1; // to skip whitespace
                return substr;
            }
        };

        if (!BaseType::trySet(nextPart())) return false;

        {
            const auto side = nextPart();
            if (side == std::string_view("w")) m_sideToMove = Color::White;
            else if (side == std::string_view("b")) m_sideToMove = Color::Black;
            else return false;

            if (isSquareAttacked(kingSquare(!m_sideToMove), m_sideToMove)) return false;
        }

        {
            const auto castlingRights = nextPart();
            auto castlingRightsOpt = parser_bits::tryParseCastlingRights(castlingRights);
            if (!castlingRightsOpt.has_value())
            {
                return false;
            }
            else
            {
                m_castlingRights = *castlingRightsOpt;
            }
        }

        {
            const auto epSquare = nextPart();
            auto epSquareOpt = parser_bits::tryParseEpSquare(epSquare);
            if (!epSquareOpt.has_value())
            {
                return false;
            }
            else
            {
                m_epSquare = *epSquareOpt;
            }
        }

        {
            const auto rule50 = nextPart();
            if (!rule50.empty())
            {
                m_rule50Counter = std::stoi(rule50.data());
            }
            else
            {
                m_rule50Counter = 0;
            }
        }

        {
            const auto fullMove = nextPart();
            if (!fullMove.empty())
            {
                m_ply = 2 * (std::stoi(fullMove.data()) - 1) + (m_sideToMove == Color::Black);
            }
            else
            {
                m_ply = 0;
            }
        }

        nullifyEpSquareIfNotPossible();

        return true;
    }

    [[nodiscard]] Position Position::fromFen(std::string_view fen)
    {
        Position pos{};
        pos.set(fen);
        return pos;
    }

    [[nodiscard]] std::optional<Position> Position::tryFromFen(std::string_view fen)
    {
        Position pos{};
        if (pos.trySet(fen)) return pos;
        else return {};
    }

    [[nodiscard]] Position Position::startPosition()
    {
        static const Position pos = fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return pos;
    }

    [[nodiscard]] std::string Position::fen() const
    {
        std::string fen = Board::fen();

        fen += ' ';
        fen += m_sideToMove == Color::White ? 'w' : 'b';

        fen += ' ';
        parser_bits::appendCastlingRightsToString(m_castlingRights, fen);

        fen += ' ';
        parser_bits::appendEpSquareToString(m_epSquare, fen);

        fen += ' ';
        fen += std::to_string(m_rule50Counter);

        fen += ' ';
        fen += std::to_string(fullMove());

        return fen;
    }

    namespace detail::lookup
    {
        static constexpr EnumArray<Square, CastlingRights> preservedCastlingRights = []() {
            EnumArray<Square, CastlingRights> preservedCastlingRights_{};
            for (CastlingRights& rights : preservedCastlingRights_)
            {
                rights = ~CastlingRights::None;
            }

            preservedCastlingRights_[e1] = ~CastlingRights::White;
            preservedCastlingRights_[e8] = ~CastlingRights::Black;

            preservedCastlingRights_[h1] = ~CastlingRights::WhiteKingSide;
            preservedCastlingRights_[a1] = ~CastlingRights::WhiteQueenSide;
            preservedCastlingRights_[h8] = ~CastlingRights::BlackKingSide;
            preservedCastlingRights_[a8] = ~CastlingRights::BlackQueenSide;

            return preservedCastlingRights_;
        }();
    }

    inline ReverseMove Position::doMove(const Move& move)
    {
        assert(move.from.isOk() && move.to.isOk());

        const PieceType movedPiece = pieceAt(move.from).type();

        m_ply += 1;
        m_rule50Counter += 1;

        if (move.type != MoveType::Castle && (movedPiece == PieceType::Pawn || pieceAt(move.to) != Piece::none()))
        {
            m_rule50Counter = 0;
        }

        const Square oldEpSquare = m_epSquare;
        const CastlingRights oldCastlingRights = m_castlingRights;
        m_castlingRights &= detail::lookup::preservedCastlingRights[move.from];
        m_castlingRights &= detail::lookup::preservedCastlingRights[move.to];

        m_epSquare = Square::none();
        // for double pushes move index differs by 16 or -16;
        if((movedPiece == PieceType::Pawn) & ((ordinal(move.to) ^ ordinal(move.from)) == 16))
        {
            m_epSquare = fromOrdinal<Square>((ordinal(move.to) + ordinal(move.from)) >> 1);
        }

        const Piece captured = BaseType::doMove(move);
        m_sideToMove = !m_sideToMove;

        nullifyEpSquareIfNotPossible();

        return { move, captured, oldEpSquare, oldCastlingRights };
    }

    [[nodiscard]] inline Position Position::afterMove(Move move) const
    {
        Position cpy(*this);
        auto pc = cpy.doMove(move);

        (void)pc;
        //assert(cpy.beforeMove(move, pc) == *this); // this assert would result in infinite recursion

        return cpy;
    }

    [[nodiscard]] inline bool Position::isEpPossible(Square epSquare, Color sideToMove) const
    {
        const Bitboard pawnsAttackingEpSquare =
            bb::pawnAttacks(Bitboard::square(epSquare), !sideToMove)
            & piecesBB(Piece(PieceType::Pawn, sideToMove));

        if (!pawnsAttackingEpSquare.any())
        {
            return false;
        }

        return isEpPossibleColdPath(epSquare, pawnsAttackingEpSquare, sideToMove);
    }

    [[nodiscard]] inline bool Position::isEpPossibleColdPath(Square epSquare, Bitboard pawnsAttackingEpSquare, Color sideToMove) const
    {
        if (pieceAt(epSquare) != Piece::none())
        {
            return false;
        }

        const auto forward =
            sideToMove == chess::Color::White
            ? FlatSquareOffset(0, 1)
            : FlatSquareOffset(0, -1);

        if (pieceAt(epSquare + forward) != Piece::none())
        {
            return false;
        }

        if (pieceAt(epSquare + -forward) != Piece(PieceType::Pawn, !sideToMove))
        {
            return false;
        }

        // only set m_epSquare when it matters, ie. when
        // the opposite side can actually capture
        for (Square sq : pawnsAttackingEpSquare)
        {
            // If we're here the previous move by other side
            // was a double pawn move so our king is either not in check
            // or is attacked only by the moved pawn - in which
            // case it can be captured by our pawn if it doesn't
            // create a discovered check on our king.
            // So overall we only have to check whether our king
            // ends up being uncovered to a slider attack.

            const Square ksq = kingSquare(sideToMove);

            const Bitboard bishops = piecesBB(Piece(PieceType::Bishop, !sideToMove));
            const Bitboard rooks = piecesBB(Piece(PieceType::Rook, !sideToMove));
            const Bitboard queens = piecesBB(Piece(PieceType::Queen, !sideToMove));

            const Bitboard relevantAttackers = bishops | rooks | queens;
            const Bitboard pseudoSliderAttacksFromKing = bb::pseudoAttacks<PieceType::Queen>(ksq);
            if ((relevantAttackers & pseudoSliderAttacksFromKing).isEmpty())
            {
                // It's enough that one pawn can capture.
                return true;
            }

            const Square capturedPawnSq(epSquare.file(), sq.rank());
            const Bitboard occupied = ((piecesBB() ^ sq) | epSquare) ^ capturedPawnSq;

            if (!bb::isAttackedBySlider(
                ksq,
                bishops,
                rooks,
                queens,
                occupied
            ))
            {
                // It's enough that one pawn can capture.
                return true;
            }
        }

        return false;
    }

    inline void Position::nullifyEpSquareIfNotPossible()
    {
        if (m_epSquare != Square::none() && !isEpPossible(m_epSquare, m_sideToMove))
        {
            m_epSquare = Square::none();
        }
    }

    namespace uci
    {
        [[nodiscard]] inline std::string moveToUci(const Position& pos, const Move& move);
        [[nodiscard]] inline Move uciToMove(const Position& pos, std::string_view sv);

        [[nodiscard]] inline std::string moveToUci(const Position& pos, const Move& move)
        {
            std::string s;

            parser_bits::appendSquareToString(move.from, s);

            if (move.type == MoveType::Castle)
            {
                const CastleType castleType = CastlingTraits::moveCastlingType(move);

                const Square kingDestination = CastlingTraits::kingDestination[pos.sideToMove()][castleType];
                parser_bits::appendSquareToString(kingDestination, s);
            }
            else
            {
                parser_bits::appendSquareToString(move.to, s);

                if (move.type == MoveType::Promotion)
                {
                    // lowercase piece symbol
                    s += EnumTraits<PieceType>::toChar(move.promotedPiece.type(), Color::Black);
                }
            }

            return s;
        }

        [[nodiscard]] inline Move uciToMove(const Position& pos, std::string_view sv)
        {
            const Square from = parser_bits::parseSquare(sv.data());
            const Square to = parser_bits::parseSquare(sv.data() + 2);

            if (sv.size() == 5)
            {
                const PieceType promotedPieceType = *fromChar<PieceType>(sv[4]);
                return Move::promotion(from, to, Piece(promotedPieceType, pos.sideToMove()));
            }
            else
            {
                if (
                    pos.pieceAt(from).type() == PieceType::King
                    && std::abs(from.file() - to.file()) > 1
                    )
                {
                    // uci king destinations are on files C or G.
                    const CastleType castleType =
                        (to.file() == fileG)
                        ? CastleType::Short
                        : CastleType::Long;

                    return Move::castle(castleType, pos.sideToMove());
                }
                else if (pos.pieceAt(from).type() == PieceType::Pawn && pos.epSquare() == to)
                {
                    return Move::enPassant(from, to);
                }
                else
                {
                    return Move::normal(from, to);
                }
            }
        }
    }
}

namespace binpack
{
    constexpr std::size_t KiB = 1024;
    constexpr std::size_t MiB = (1024*KiB);
    constexpr std::size_t GiB = (1024*MiB);

    constexpr std::size_t suggestedChunkSize = MiB;
    constexpr std::size_t maxMovelistSize = 10*KiB; // a safe upper bound
    constexpr std::size_t maxChunkSize = 100*MiB; // to prevent malformed files from causing huge allocations

    using namespace std::literals;

    namespace nodchip
    {
        // This namespace contains modified code from https://github.com/nodchip/Stockfish
        // which is released under GPL v3 license https://www.gnu.org/licenses/gpl-3.0.html

        using namespace std;

        struct StockfishMove
        {
            [[nodiscard]] static StockfishMove fromMove(chess::Move move)
            {
                StockfishMove sfm;

                sfm.m_raw = 0;

                unsigned moveFlag = 0;
                if (move.type == chess::MoveType::Promotion) moveFlag = 1;
                else if (move.type == chess::MoveType::EnPassant) moveFlag = 2;
                else if (move.type == chess::MoveType::Castle) moveFlag = 3;

                unsigned promotionIndex = 0;
                if (move.type == chess::MoveType::Promotion)
                {
                    promotionIndex = static_cast<int>(move.promotedPiece.type()) - static_cast<int>(chess::PieceType::Knight);
                }

                sfm.m_raw |= static_cast<std::uint16_t>(moveFlag);
                sfm.m_raw <<= 2;
                sfm.m_raw |= static_cast<std::uint16_t>(promotionIndex);
                sfm.m_raw <<= 6;
                sfm.m_raw |= static_cast<int>(move.from);
                sfm.m_raw <<= 6;
                sfm.m_raw |= static_cast<int>(move.to);

                return sfm;
            }

            [[nodiscard]] chess::Move toMove() const
            {
                const chess::Square to = static_cast<chess::Square>((m_raw & (0b111111 << 0) >> 0));
                const chess::Square from = static_cast<chess::Square>((m_raw & (0b111111 << 6)) >> 6);

                const unsigned promotionIndex = (m_raw & (0b11 << 12)) >> 12;
                const chess::PieceType promotionType = static_cast<chess::PieceType>(static_cast<int>(chess::PieceType::Knight) + promotionIndex);

                const unsigned moveFlag = (m_raw & (0b11 << 14)) >> 14;
                chess::MoveType type = chess::MoveType::Normal;
                if (moveFlag == 1) type = chess::MoveType::Promotion;
                else if (moveFlag == 2) type = chess::MoveType::EnPassant;
                else if (moveFlag == 3) type = chess::MoveType::Castle;

                if (type == chess::MoveType::Promotion)
                {
                    const chess::Color stm = to.rank() == chess::rank8 ? chess::Color::White : chess::Color::Black;
                    return chess::Move{from, to, type, chess::Piece(promotionType, stm)};
                }

                return chess::Move{from, to, type};
            }

            [[nodiscard]] std::string toString() const
            {
                const chess::Square to = static_cast<chess::Square>((m_raw & (0b111111 << 0) >> 0));
                const chess::Square from = static_cast<chess::Square>((m_raw & (0b111111 << 6)) >> 6);

                const unsigned promotionIndex = (m_raw & (0b11 << 12)) >> 12;
                const chess::PieceType promotionType = static_cast<chess::PieceType>(static_cast<int>(chess::PieceType::Knight) + promotionIndex);

                std::string r;
                chess::parser_bits::appendSquareToString(from, r);
                chess::parser_bits::appendSquareToString(to, r);
                if (promotionType != chess::PieceType::None)
                {
                    r += chess::EnumTraits<chess::PieceType>::toChar(promotionType, chess::Color::Black);
                }

                return r;
            }

        private:
            std::uint16_t m_raw;
        };
        static_assert(sizeof(StockfishMove) == sizeof(std::uint16_t));

        struct PackedSfen
        {
            uint8_t data[32];
        };

        struct PackedSfenValue
        {
            // phase
            PackedSfen sfen;

            // Evaluation value returned from Learner::search()
            int16_t score;

            // PV first move
            // Used when finding the match rate with the teacher
            StockfishMove move;

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
        static_assert(sizeof(PackedSfenValue) == 40);
        // Class that handles bitstream

        // useful when doing aspect encoding
        struct BitStream
        {
            // Set the memory to store the data in advance.
            // Assume that memory is cleared to 0.
            void  set_data(uint8_t* data_) { data = data_; reset(); }

            // Get the pointer passed in set_data().
            uint8_t* get_data() const { return data; }

            // Get the cursor.
            int get_cursor() const { return bit_cursor; }

            // reset the cursor
            void reset() { bit_cursor = 0; }

            // Write 1bit to the stream.
            // If b is non-zero, write out 1. If 0, write 0.
            void write_one_bit(int b)
            {
                if (b)
                    data[bit_cursor / 8] |= 1 << (bit_cursor & 7);

                ++bit_cursor;
            }

            // Get 1 bit from the stream.
            int read_one_bit()
            {
                int b = (data[bit_cursor / 8] >> (bit_cursor & 7)) & 1;
                ++bit_cursor;

                return b;
            }

            // write n bits of data
            // Data shall be written out from the lower order of d.
            void write_n_bit(int d, int n)
            {
                for (int i = 0; i <n; ++i)
                    write_one_bit(d & (1 << i));
            }

            // read n bits of data
            // Reverse conversion of write_n_bit().
            int read_n_bit(int n)
            {
                int result = 0;
                for (int i = 0; i < n; ++i)
                    result |= read_one_bit() ? (1 << i) : 0;

                return result;
            }

        private:
            // Next bit position to read/write.
            int bit_cursor;

            // data entity
            uint8_t* data;
        };


        // Huffman coding
        // * is simplified from mini encoding to make conversion easier.
        //
        // Huffman Encoding
        //
        // Empty  xxxxxxx0
        // Pawn   xxxxx001 + 1 bit (Color)
        // Knight xxxxx011 + 1 bit (Color)
        // Bishop xxxxx101 + 1 bit (Color)
        // Rook   xxxxx111 + 1 bit (Color)
        // Queen   xxxx1001 + 1 bit (Color)
        //
        // Worst case:
        // - 32 empty squares    32 bits
        // - 30 pieces           150 bits
        // - 2 kings             12 bits
        // - castling rights     4 bits
        // - ep square           7 bits
        // - rule50              7 bits
        // - game ply            16 bits
        // - TOTAL               228 bits < 256 bits

        struct HuffmanedPiece
        {
            int code; // how it will be coded
            int bits; // How many bits do you have
        };

        // NOTE: Order adjusted for this library because originally NO_PIECE had index 0
        constexpr HuffmanedPiece huffman_table[] =
        {
            {0b0001,4}, // PAWN     1
            {0b0011,4}, // KNIGHT   3
            {0b0101,4}, // BISHOP   5
            {0b0111,4}, // ROOK     7
            {0b1001,4}, // QUEEN    9
            {-1,-1},    // KING - unused
            {0b0000,1}, // NO_PIECE 0
        };

        // Class for compressing/decompressing sfen
        // sfen can be packed to 256bit (32bytes) by Huffman coding.
        // This is proven by mini. The above is Huffman coding.
        //
        // Internal format = 1-bit turn + 7-bit king position *2 + piece on board (Huffman coding) + hand piece (Huffman coding)
        // Side to move (White = 0, Black = 1) (1bit)
        // White King Position (6 bits)
        // Black King Position (6 bits)
        // Huffman Encoding of the board
        // Castling availability (1 bit x 4)
        // En passant square (1 or 1 + 6 bits)
        // Rule 50 (6 bits)
        // Game play (8 bits)
        //
        // TODO(someone): Rename SFEN to FEN.
        //
        struct SfenPacker
        {
            // Pack sfen and store in data[32].
            void pack(const chess::Position& pos)
            {
                memset(data, 0, 32 /* 256bit */);
                stream.set_data(data);

                // turn
                // Side to move.
                stream.write_one_bit((int)(pos.sideToMove()));

                // 7-bit positions for leading and trailing balls
                // White king and black king, 6 bits for each.
                stream.write_n_bit(static_cast<int>(pos.kingSquare(chess::Color::White)), 6);
                stream.write_n_bit(static_cast<int>(pos.kingSquare(chess::Color::Black)), 6);

                // Write the pieces on the board other than the kings.
                for (chess::Rank r = chess::rank8; r >= chess::rank1; --r)
                {
                    for (chess::File f = chess::fileA; f <= chess::fileH; ++f)
                    {
                        chess::Piece pc = pos.pieceAt(chess::Square(f, r));
                        if (pc.type() == chess::PieceType::King)
                            continue;
                        write_board_piece_to_stream(pc);
                    }
                }

                // TODO(someone): Support chess960.
                auto cr = pos.castlingRights();
                stream.write_one_bit(contains(cr, chess::CastlingRights::WhiteKingSide));
                stream.write_one_bit(contains(cr, chess::CastlingRights::WhiteQueenSide));
                stream.write_one_bit(contains(cr, chess::CastlingRights::BlackKingSide));
                stream.write_one_bit(contains(cr, chess::CastlingRights::BlackQueenSide));

                if (pos.epSquare() == chess::Square::none()) {
                    stream.write_one_bit(0);
                }
                else {
                    stream.write_one_bit(1);
                    stream.write_n_bit(static_cast<int>(pos.epSquare()), 6);
                }

                stream.write_n_bit(pos.rule50Counter(), 6);

                stream.write_n_bit(pos.fullMove(), 8);

                // Write high bits of half move. This is a fix for the
                // limited range of half move counter.
                // This is backwards compatibile.
                stream.write_n_bit(pos.fullMove() >> 8, 8);

                // Write the highest bit of rule50 at the end. This is a backwards
                // compatibile fix for rule50 having only 6 bits stored.
                // This bit is just ignored by the old parsers.
                stream.write_n_bit(pos.rule50Counter() >> 6, 1);

                assert(stream.get_cursor() <= 256);
            }

            // sfen packed by pack() (256bit = 32bytes)
            // Or sfen to decode with unpack()
            uint8_t *data; // uint8_t[32];

            BitStream stream;

            // Output the board pieces to stream.
            void write_board_piece_to_stream(chess::Piece pc)
            {
                // piece type
                chess::PieceType pr = pc.type();
                auto c = huffman_table[static_cast<int>(pr)];
                stream.write_n_bit(c.code, c.bits);

                if (pc == chess::Piece::none())
                    return;

                // first and second flag
                stream.write_one_bit(static_cast<int>(pc.color()));
            }

            // Read one board piece from stream
            [[nodiscard]] chess::Piece read_board_piece_from_stream()
            {
                int pr = static_cast<int>(chess::PieceType::None);
                int code = 0, bits = 0;
                while (true)
                {
                    code |= stream.read_one_bit() << bits;
                    ++bits;

                    assert(bits <= 6);

                    for (pr = static_cast<int>(chess::PieceType::Pawn); pr <= static_cast<int>(chess::PieceType::None); ++pr)
                        if (huffman_table[pr].code == code
                            && huffman_table[pr].bits == bits)
                            goto Found;
                }
            Found:;
                if (pr == static_cast<int>(chess::PieceType::None))
                    return chess::Piece::none();

                // first and second flag
                chess::Color c = (chess::Color)stream.read_one_bit();

                return chess::Piece(static_cast<chess::PieceType>(pr), c);
            }
        };


        [[nodiscard]] inline chess::Position pos_from_packed_sfen(const PackedSfen& sfen)
        {
            SfenPacker packer;
            auto& stream = packer.stream;
            stream.set_data(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&sfen)));

            chess::Position pos{};

            // Active color
            pos.setSideToMove((chess::Color)stream.read_one_bit());

            // First the position of the ball
            pos.place(chess::Piece(chess::PieceType::King, chess::Color::White), static_cast<chess::Square>(stream.read_n_bit(6)));
            pos.place(chess::Piece(chess::PieceType::King, chess::Color::Black), static_cast<chess::Square>(stream.read_n_bit(6)));

            // Piece placement
            for (chess::Rank r = chess::rank8; r >= chess::rank1; --r)
            {
                for (chess::File f = chess::fileA; f <= chess::fileH; ++f)
                {
                    auto sq = chess::Square(f, r);

                    // it seems there are already balls
                    chess::Piece pc;
                    if (pos.pieceAt(sq).type() != chess::PieceType::King)
                    {
                        assert(pos.pieceAt(sq) == chess::Piece::none());
                        pc = packer.read_board_piece_from_stream();
                    }
                    else
                    {
                        pc = pos.pieceAt(sq);
                    }

                    // There may be no pieces, so skip in that case.
                    if (pc == chess::Piece::none())
                        continue;

                    if (pc.type() != chess::PieceType::King)
                    {
                        pos.place(pc, sq);
                    }

                    assert(stream.get_cursor() <= 256);
                }
            }

            // Castling availability.
            chess::CastlingRights cr = chess::CastlingRights::None;
            if (stream.read_one_bit()) {
                cr |= chess::CastlingRights::WhiteKingSide;
            }
            if (stream.read_one_bit()) {
                cr |= chess::CastlingRights::WhiteQueenSide;
            }
            if (stream.read_one_bit()) {
                cr |= chess::CastlingRights::BlackKingSide;
            }
            if (stream.read_one_bit()) {
                cr |= chess::CastlingRights::BlackQueenSide;
            }
            pos.setCastlingRights(cr);

            // En passant square. Ignore if no pawn capture is possible
            if (stream.read_one_bit()) {
                chess::Square ep_square = static_cast<chess::Square>(stream.read_n_bit(6));
                pos.setEpSquare(ep_square);
            }

            // Halfmove clock
            std::uint8_t rule50 = stream.read_n_bit(6);

            // Fullmove number
            std::uint16_t fullmove = stream.read_n_bit(8);

            // Fullmove number, high bits
            // This was added as a fix for fullmove clock
            // overflowing at 256. This change is backwards compatibile.
            fullmove |= stream.read_n_bit(8) << 8;

            // Read the highest bit of rule50. This was added as a fix for rule50
            // counter having only 6 bits stored.
            // In older entries this will just be a zero bit.
            rule50 |= stream.read_n_bit(1) << 6;

            pos.setFullMove(fullmove);
            pos.setRule50Counter(rule50);

            assert(stream.get_cursor() <= 256);

            return pos;
        }
    }

    struct CompressedTrainingDataFile
    {
        struct Header
        {
            std::uint32_t chunkSize;
        };

        CompressedTrainingDataFile(std::string path, std::ios_base::openmode om = std::ios_base::app) :
            m_path(std::move(path)),
            m_file(m_path, std::ios_base::binary | std::ios_base::in | std::ios_base::out | om)
        {
            // Necessary for MAC because app mode makes it put the reading
            // head at the end.
            m_file.seekg(0);
        }

        void append(const char* data, std::uint32_t size)
        {
            writeChunkHeader({size});
            m_file.write(data, size);
        }

        [[nodiscard]] bool hasNextChunk()
        {
            if (!m_file)
            {
                return false;
            }

            m_file.peek();
            return !m_file.eof();
        }

        [[nodiscard]] std::vector<unsigned char> readNextChunk()
        {
            auto size = readChunkHeader().chunkSize;
            std::vector<unsigned char> data(size);
            m_file.read(reinterpret_cast<char*>(data.data()), size);
            return data;
        }

    private:
        std::string m_path;
        std::fstream m_file;

        void writeChunkHeader(Header h)
        {
            unsigned char header[8];
            header[0] = 'B';
            header[1] = 'I';
            header[2] = 'N';
            header[3] = 'P';
            header[4] = h.chunkSize;
            header[5] = h.chunkSize >> 8;
            header[6] = h.chunkSize >> 16;
            header[7] = h.chunkSize >> 24;
            m_file.write(reinterpret_cast<const char*>(header), 8);
        }

        [[nodiscard]] Header readChunkHeader()
        {
            unsigned char header[8];
            m_file.read(reinterpret_cast<char*>(header), 8);
            if (header[0] != 'B' || header[1] != 'I' || header[2] != 'N' || header[3] != 'P')
            {
                assert(false);
                // throw std::runtime_error("Invalid binpack file or chunk.");
            }

            const std::uint32_t size =
                header[4]
                | (header[5] << 8)
                | (header[6] << 16)
                | (header[7] << 24);

            if (size > maxChunkSize)
            {
                assert(false);
                // throw std::runtime_error("Chunks size larger than supported. Malformed file?");
            }

            return { size };
        }
    };

    [[nodiscard]] inline std::uint16_t signedToUnsigned(std::int16_t a)
    {
        std::uint16_t r;
        std::memcpy(&r, &a, sizeof(std::uint16_t));
        if (r & 0x8000)
        {
            r ^= 0x7FFF;
        }
        r = (r << 1) | (r >> 15);
        return r;
    }

    [[nodiscard]] inline std::int16_t unsignedToSigned(std::uint16_t r)
    {
        std::int16_t a;
        r = (r << 15) | (r >> 1);
        if (r & 0x8000)
        {
            r ^= 0x7FFF;
        }
        std::memcpy(&a, &r, sizeof(std::uint16_t));
        return a;
    }

    struct TrainingDataEntry
    {
        chess::Position pos;
        chess::Move move;
        std::int16_t score;
        std::uint16_t ply;
        std::int16_t result;

        [[nodiscard]] bool isValid() const
        {
            return pos.isMoveLegal(move);
        }
    };

    [[nodiscard]] inline TrainingDataEntry packedSfenValueToTrainingDataEntry(const nodchip::PackedSfenValue& psv)
    {
        TrainingDataEntry ret;

        ret.pos = nodchip::pos_from_packed_sfen(psv.sfen);
        ret.move = psv.move.toMove();
        ret.score = psv.score;
        ret.ply = psv.gamePly;
        ret.result = psv.game_result;

        return ret;
    }

    [[nodiscard]] inline nodchip::PackedSfenValue trainingDataEntryToPackedSfenValue(const TrainingDataEntry& plain)
    {
        nodchip::PackedSfenValue ret;

        nodchip::SfenPacker sp;
        sp.data = reinterpret_cast<uint8_t*>(&ret.sfen);
        sp.pack(plain.pos);

        ret.score = plain.score;
        ret.move = nodchip::StockfishMove::fromMove(plain.move);
        ret.gamePly = plain.ply;
        ret.game_result = plain.result;
        ret.padding = 0xff; // for consistency with the .bin format.

        return ret;
    }

    [[nodiscard]] inline bool isContinuation(const TrainingDataEntry& lhs, const TrainingDataEntry& rhs)
    {
        return
            lhs.result == -rhs.result
            && lhs.ply + 1 == rhs.ply
            && lhs.pos.afterMove(lhs.move) == rhs.pos;
    }

    struct PackedTrainingDataEntry
    {
        unsigned char bytes[32];
    };

    [[nodiscard]] inline std::size_t usedBitsSafe(std::size_t value)
    {
        if (value == 0) return 0;
        return chess::util::usedBits(value - 1);
    }

    static constexpr std::size_t scoreVleBlockSize = 4;

    struct PackedMoveScoreListReader
    {
        TrainingDataEntry entry;
        std::uint16_t numPlies;
        unsigned char* movetext;

        PackedMoveScoreListReader(const TrainingDataEntry& entry_, unsigned char* movetext_, std::uint16_t numPlies_) :
            entry(entry_),
            numPlies(numPlies_),
            movetext(movetext_),
            m_lastScore(-entry_.score)
        {

        }

        [[nodiscard]] std::uint8_t extractBitsLE8(std::size_t count)
        {
            if (count == 0) return 0;

            if (m_readBitsLeft == 0)
            {
                m_readOffset += 1;
                m_readBitsLeft = 8;
            }

            const std::uint8_t byte = movetext[m_readOffset] << (8 - m_readBitsLeft);
            std::uint8_t bits = byte >> (8 - count);

            if (count > m_readBitsLeft)
            {
                const auto spillCount = count - m_readBitsLeft;
                bits |= movetext[m_readOffset + 1] >> (8 - spillCount);

                m_readBitsLeft += 8;
                m_readOffset += 1;
            }

            m_readBitsLeft -= count;

            return bits;
        }

        [[nodiscard]] std::uint16_t extractVle16(std::size_t blockSize)
        {
            auto mask = (1 << blockSize) - 1;
            std::uint16_t v = 0;
            std::size_t offset = 0;
            for(;;)
            {
                std::uint16_t block = extractBitsLE8(blockSize + 1);
                v |= ((block & mask) << offset);
                if (!(block >> blockSize))
                {
                    break;
                }

                offset += blockSize;
            }
            return v;
        }

        [[nodiscard]] TrainingDataEntry nextEntry()
        {
            entry.pos.doMove(entry.move);
            auto [move, score] = nextMoveScore(entry.pos);
            entry.move = move;
            entry.score = score;
            entry.ply += 1;
            entry.result = -entry.result;
            return entry;
        }

        [[nodiscard]] bool hasNext() const
        {
            return m_numReadPlies < numPlies;
        }

        [[nodiscard]] std::pair<chess::Move, std::int16_t> nextMoveScore(const chess::Position& pos)
        {
            chess::Move move;
            std::int16_t score;

            const chess::Color sideToMove = pos.sideToMove();
            const chess::Bitboard ourPieces = pos.piecesBB(sideToMove);
            const chess::Bitboard theirPieces = pos.piecesBB(!sideToMove);
            const chess::Bitboard occupied = ourPieces | theirPieces;

            const auto pieceId = extractBitsLE8(usedBitsSafe(ourPieces.count()));
            const auto from = chess::Square(chess::nthSetBitIndex(ourPieces.bits(), pieceId));

            const auto pt = pos.pieceAt(from).type();
            switch (pt)
            {
            case chess::PieceType::Pawn:
            {
                const chess::Rank promotionRank = pos.sideToMove() == chess::Color::White ? chess::rank7 : chess::rank2;
                const chess::Rank startRank = pos.sideToMove() == chess::Color::White ? chess::rank2 : chess::rank7;
                const auto forward = sideToMove == chess::Color::White ? chess::FlatSquareOffset(0, 1) : chess::FlatSquareOffset(0, -1);

                const chess::Square epSquare = pos.epSquare();

                chess::Bitboard attackTargets = theirPieces;
                if (epSquare != chess::Square::none())
                {
                    attackTargets |= epSquare;
                }

                chess::Bitboard destinations = chess::bb::pawnAttacks(chess::Bitboard::square(from), sideToMove) & attackTargets;

                const chess::Square sqForward = from + forward;
                if (!occupied.isSet(sqForward))
                {
                    destinations |= sqForward;
                    if (
                        from.rank() == startRank
                        && !occupied.isSet(sqForward + forward)
                        )
                    {
                        destinations |= sqForward + forward;
                    }
                }

                const auto destinationsCount = destinations.count();
                if (from.rank() == promotionRank)
                {
                    const auto moveId = extractBitsLE8(usedBitsSafe(destinationsCount * 4ull));
                    const chess::Piece promotedPiece = chess::Piece(
                        chess::fromOrdinal<chess::PieceType>(ordinal(chess::PieceType::Knight) + (moveId % 4ull)),
                        sideToMove
                    );
                    const auto to = chess::Square(chess::nthSetBitIndex(destinations.bits(), moveId / 4ull));

                    move = chess::Move::promotion(from, to, promotedPiece);
                    break;
                }
                else
                {
                    auto moveId = extractBitsLE8(usedBitsSafe(destinationsCount));
                    const auto to = chess::Square(chess::nthSetBitIndex(destinations.bits(), moveId));
                    if (to == epSquare)
                    {
                        move = chess::Move::enPassant(from, to);
                        break;
                    }
                    else
                    {
                        move = chess::Move::normal(from, to);
                        break;
                    }
                }
            }
            case chess::PieceType::King:
            {
                const chess::CastlingRights ourCastlingRightsMask =
                    sideToMove == chess::Color::White
                    ? chess::CastlingRights::White
                    : chess::CastlingRights::Black;

                const chess::CastlingRights castlingRights = pos.castlingRights();

                const chess::Bitboard attacks = chess::bb::pseudoAttacks<chess::PieceType::King>(from) & ~ourPieces;
                const std::size_t attacksSize = attacks.count();
                const std::size_t numCastlings = chess::intrin::popcount(ordinal(castlingRights & ourCastlingRightsMask));

                const auto moveId = extractBitsLE8(usedBitsSafe(attacksSize + numCastlings));

                if (moveId >= attacksSize)
                {
                    const std::size_t idx = moveId - attacksSize;

                    const chess::CastleType castleType =
                        idx == 0
                        && chess::contains(castlingRights, chess::CastlingTraits::castlingRights[sideToMove][chess::CastleType::Long])
                        ? chess::CastleType::Long
                        : chess::CastleType::Short;

                    move = chess::Move::castle(castleType, sideToMove);
                    break;
                }
                else
                {
                    auto to = chess::Square(chess::nthSetBitIndex(attacks.bits(), moveId));
                    move = chess::Move::normal(from, to);
                    break;
                }
                break;
            }
            default:
            {
                const chess::Bitboard attacks = chess::bb::attacks(pt, from, occupied) & ~ourPieces;
                const auto moveId = extractBitsLE8(usedBitsSafe(attacks.count()));
                auto to = chess::Square(chess::nthSetBitIndex(attacks.bits(), moveId));
                move = chess::Move::normal(from, to);
                break;
            }
            }

            score = m_lastScore + unsignedToSigned(extractVle16(scoreVleBlockSize));
            m_lastScore = -score;

            ++m_numReadPlies;

            return {move, score};
        }

        [[nodiscard]] std::size_t numReadBytes()
        {
            return m_readOffset + (m_readBitsLeft != 8);
        }

    private:
        std::size_t m_readBitsLeft = 8;
        std::size_t m_readOffset = 0;
        std::int16_t m_lastScore = 0;
        std::uint16_t m_numReadPlies = 0;
    };

    struct PackedMoveScoreList
    {
        std::uint16_t numPlies = 0;
        std::vector<unsigned char> movetext;

        void clear(const TrainingDataEntry& e)
        {
            numPlies = 0;
            movetext.clear();
            m_bitsLeft = 0;
            m_lastScore = -e.score;
        }

        void addBitsLE8(std::uint8_t bits, std::size_t count)
        {
            if (count == 0) return;

            if (m_bitsLeft == 0)
            {
                movetext.emplace_back(bits << (8 - count));
                m_bitsLeft = 8;
            }
            else if (count <= m_bitsLeft)
            {
                movetext.back() |= bits << (m_bitsLeft - count);
            }
            else
            {
                const auto spillCount = count - m_bitsLeft;
                movetext.back() |= bits >> spillCount;
                movetext.emplace_back(bits << (8 - spillCount));
                m_bitsLeft += 8;
            }

            m_bitsLeft -= count;
        }

        void addBitsVle16(std::uint16_t v, std::size_t blockSize)
        {
            auto mask = (1 << blockSize) - 1;
            for(;;)
            {
                std::uint8_t block = (v & mask) | ((v > mask) << blockSize);
                addBitsLE8(block, blockSize + 1);
                v >>= blockSize;
                if (v == 0) break;
            }
        }


        void addMoveScore(const chess::Position& pos, chess::Move move, std::int16_t score)
        {
            const chess::Color sideToMove = pos.sideToMove();
            const chess::Bitboard ourPieces = pos.piecesBB(sideToMove);
            const chess::Bitboard theirPieces = pos.piecesBB(!sideToMove);
            const chess::Bitboard occupied = ourPieces | theirPieces;

            const std::uint8_t pieceId = (pos.piecesBB(sideToMove) & chess::bb::before(move.from)).count();
            std::size_t numMoves = 0;
            int moveId = 0;
            const auto pt = pos.pieceAt(move.from).type();
            switch (pt)
            {
            case chess::PieceType::Pawn:
            {
                const chess::Rank secondToLastRank = pos.sideToMove() == chess::Color::White ? chess::rank7 : chess::rank2;
                const chess::Rank startRank = pos.sideToMove() == chess::Color::White ? chess::rank2 : chess::rank7;
                const auto forward = sideToMove == chess::Color::White ? chess::FlatSquareOffset(0, 1) : chess::FlatSquareOffset(0, -1);

                const chess::Square epSquare = pos.epSquare();

                chess::Bitboard attackTargets = theirPieces;
                if (epSquare != chess::Square::none())
                {
                    attackTargets |= epSquare;
                }

                chess::Bitboard destinations = chess::bb::pawnAttacks(chess::Bitboard::square(move.from), sideToMove) & attackTargets;

                const chess::Square sqForward = move.from + forward;
                if (!occupied.isSet(sqForward))
                {
                    destinations |= sqForward;

                    if (
                        move.from.rank() == startRank
                        && !occupied.isSet(sqForward + forward)
                        )
                    {
                        destinations |= sqForward + forward;
                    }
                }

                moveId = (destinations & chess::bb::before(move.to)).count();
                numMoves = destinations.count();
                if (move.from.rank() == secondToLastRank)
                {
                    const auto promotionIndex = (ordinal(move.promotedPiece.type()) - ordinal(chess::PieceType::Knight));
                    moveId = moveId * 4 + promotionIndex;
                    numMoves *= 4;
                }

                break;
            }
            case chess::PieceType::King:
            {
                const chess::CastlingRights ourCastlingRightsMask =
                    sideToMove == chess::Color::White
                    ? chess::CastlingRights::White
                    : chess::CastlingRights::Black;

                const chess::CastlingRights castlingRights = pos.castlingRights();

                const chess::Bitboard attacks = chess::bb::pseudoAttacks<chess::PieceType::King>(move.from) & ~ourPieces;
                const auto attacksSize = attacks.count();
                const auto numCastlingRights = chess::intrin::popcount(ordinal(castlingRights & ourCastlingRightsMask));

                numMoves += attacksSize;
                numMoves += numCastlingRights;

                if (move.type == chess::MoveType::Castle)
                {
                    const auto longCastlingRights = chess::CastlingTraits::castlingRights[sideToMove][chess::CastleType::Long];

                    moveId = attacksSize - 1;

                    if (chess::contains(castlingRights, longCastlingRights))
                    {
                        // We have to add one no matter if it's the used one or not.
                        moveId += 1;
                    }

                    if (chess::CastlingTraits::moveCastlingType(move) == chess::CastleType::Short)
                    {
                        moveId += 1;
                    }
                }
                else
                {
                    moveId = (attacks & chess::bb::before(move.to)).count();
                }
                break;
            }
            default:
            {
                const chess::Bitboard attacks = chess::bb::attacks(pt, move.from, occupied) & ~ourPieces;

                moveId = (attacks & chess::bb::before(move.to)).count();
                numMoves = attacks.count();
            }
            }

            const std::size_t numPieces = ourPieces.count();
            addBitsLE8(pieceId, usedBitsSafe(numPieces));
            addBitsLE8(moveId, usedBitsSafe(numMoves));

            std::uint16_t scoreDelta = signedToUnsigned(score - m_lastScore);
            addBitsVle16(scoreDelta, scoreVleBlockSize);
            m_lastScore = -score;

            ++numPlies;
        }

    private:
        std::size_t m_bitsLeft = 0;
        std::int16_t m_lastScore = 0;
    };


    [[nodiscard]] inline PackedTrainingDataEntry packEntry(const TrainingDataEntry& plain)
    {
        PackedTrainingDataEntry packed;

        auto compressedPos = plain.pos.compress();
        auto compressedMove = plain.move.compress();

        static_assert(sizeof(compressedPos) + sizeof(compressedMove) + 6 == sizeof(PackedTrainingDataEntry));

        std::size_t offset = 0;
        compressedPos.writeToBigEndian(packed.bytes);
        offset += sizeof(compressedPos);
        compressedMove.writeToBigEndian(packed.bytes + offset);
        offset += sizeof(compressedMove);
        std::uint16_t pr = plain.ply | (signedToUnsigned(plain.result) << 14);
        packed.bytes[offset++] = signedToUnsigned(plain.score) >> 8;
        packed.bytes[offset++] = signedToUnsigned(plain.score);
        packed.bytes[offset++] = pr >> 8;
        packed.bytes[offset++] = pr;
        packed.bytes[offset++] = plain.pos.rule50Counter() >> 8;
        packed.bytes[offset++] = plain.pos.rule50Counter();

        return packed;
    }

    [[nodiscard]] inline TrainingDataEntry unpackEntry(const PackedTrainingDataEntry& packed)
    {
        TrainingDataEntry plain;

        std::size_t offset = 0;
        auto compressedPos = chess::CompressedPosition::readFromBigEndian(packed.bytes);
        plain.pos = compressedPos.decompress();
        offset += sizeof(compressedPos);
        auto compressedMove = chess::CompressedMove::readFromBigEndian(packed.bytes + offset);
        plain.move = compressedMove.decompress();
        offset += sizeof(compressedMove);
        plain.score = unsignedToSigned((packed.bytes[offset] << 8) | packed.bytes[offset+1]);
        offset += 2;
        std::uint16_t pr = (packed.bytes[offset] << 8) | packed.bytes[offset+1];
        plain.ply = pr & 0x3FFF;
        plain.pos.setPly(plain.ply);
        plain.result = unsignedToSigned(pr >> 14);
        offset += 2;
        plain.pos.setRule50Counter((packed.bytes[offset] << 8) | packed.bytes[offset+1]);

        return plain;
    }

    struct CompressedTrainingDataEntryWriter
    {
        static constexpr std::size_t chunkSize = suggestedChunkSize;

        CompressedTrainingDataEntryWriter(std::string path, std::ios_base::openmode om = std::ios_base::app) :
            m_outputFile(path, om),
            m_lastEntry{},
            m_movelist{},
            m_packedSize(0),
            m_packedEntries(chunkSize + maxMovelistSize),
            m_isFirst(true)
        {
            m_lastEntry.ply = 0xFFFF; // so it's never a continuation
            m_lastEntry.result = 0x7FFF;
        }

        void addTrainingDataEntry(const TrainingDataEntry& e)
        {
            bool isCont = isContinuation(m_lastEntry, e);
            if (isCont)
            {
                // add to movelist
                m_movelist.addMoveScore(e.pos, e.move, e.score);
            }
            else
            {
                if (!m_isFirst)
                {
                    writeMovelist();
                }

                if (m_packedSize >= chunkSize)
                {
                    m_outputFile.append(m_packedEntries.data(), m_packedSize);
                    m_packedSize = 0;
                }

                auto packed = packEntry(e);
                std::memcpy(m_packedEntries.data() + m_packedSize, &packed, sizeof(PackedTrainingDataEntry));
                m_packedSize += sizeof(PackedTrainingDataEntry);

                m_movelist.clear(e);

                m_isFirst = false;
            }

            m_lastEntry = e;
        }

        ~CompressedTrainingDataEntryWriter()
        {
            if (m_packedSize > 0)
            {
                if (!m_isFirst)
                {
                    writeMovelist();
                }

                m_outputFile.append(m_packedEntries.data(), m_packedSize);
                m_packedSize = 0;
            }
        }

    private:
        CompressedTrainingDataFile m_outputFile;
        TrainingDataEntry m_lastEntry;
        PackedMoveScoreList m_movelist;
        std::size_t m_packedSize;
        std::vector<char> m_packedEntries;
        bool m_isFirst;

        void writeMovelist()
        {
            m_packedEntries[m_packedSize++] = m_movelist.numPlies >> 8;
            m_packedEntries[m_packedSize++] = m_movelist.numPlies;
            if (m_movelist.numPlies > 0)
            {
                std::memcpy(m_packedEntries.data() + m_packedSize, m_movelist.movetext.data(), m_movelist.movetext.size());
                m_packedSize += m_movelist.movetext.size();
            }
        };
    };

    struct CompressedTrainingDataEntryReader
    {
        static constexpr std::size_t chunkSize = suggestedChunkSize;

        CompressedTrainingDataEntryReader(std::string path, std::ios_base::openmode om = std::ios_base::app) :
            m_inputFile(path, om),
            m_chunk(),
            m_movelistReader(std::nullopt),
            m_offset(0),
            m_isEnd(false)
        {
            if (!m_inputFile.hasNextChunk())
            {
                m_isEnd = true;
            }
            else
            {
                m_chunk = m_inputFile.readNextChunk();
            }
        }

        [[nodiscard]] bool hasNext()
        {
            return !m_isEnd;
        }

        [[nodiscard]] TrainingDataEntry next()
        {
            if (m_movelistReader.has_value())
            {
                const auto e = m_movelistReader->nextEntry();

                if (!m_movelistReader->hasNext())
                {
                    m_offset += m_movelistReader->numReadBytes();
                    m_movelistReader.reset();

                    fetchNextChunkIfNeeded();
                }

                return e;
            }

            PackedTrainingDataEntry packed;
            std::memcpy(&packed, m_chunk.data() + m_offset, sizeof(PackedTrainingDataEntry));
            m_offset += sizeof(PackedTrainingDataEntry);

            const std::uint16_t numPlies = (m_chunk[m_offset] << 8) | m_chunk[m_offset + 1];
            m_offset += 2;

            const auto e = unpackEntry(packed);

            if (numPlies > 0)
            {
                m_movelistReader.emplace(e, reinterpret_cast<unsigned char*>(m_chunk.data()) + m_offset, numPlies);
            }
            else
            {
                fetchNextChunkIfNeeded();
            }

            return e;
        }

    private:
        CompressedTrainingDataFile m_inputFile;
        std::vector<unsigned char> m_chunk;
        std::optional<PackedMoveScoreListReader> m_movelistReader;
        std::size_t m_offset;
        bool m_isEnd;

        void fetchNextChunkIfNeeded()
        {
            if (m_offset + sizeof(PackedTrainingDataEntry) + 2 > m_chunk.size())
            {
                if (m_inputFile.hasNextChunk())
                {
                    m_chunk = m_inputFile.readNextChunk();
                    m_offset = 0;
                }
                else
                {
                    m_isEnd = true;
                }
            }
        }
    };

    inline void emitPlainEntry(std::string& buffer, const TrainingDataEntry& plain)
    {
        buffer += "fen ";
        buffer += plain.pos.fen();
        buffer += '\n';

        buffer += "move ";
        buffer += chess::uci::moveToUci(plain.pos, plain.move);
        buffer += '\n';

        buffer += "score ";
        buffer += std::to_string(plain.score);
        buffer += '\n';

        buffer += "ply ";
        buffer += std::to_string(plain.ply);
        buffer += '\n';

        buffer += "result ";
        buffer += std::to_string(plain.result);
        buffer += "\ne\n";
    }

    inline void emitBinEntry(std::vector<char>& buffer, const TrainingDataEntry& plain)
    {
        auto psv = trainingDataEntryToPackedSfenValue(plain);
        const char* data = reinterpret_cast<const char*>(&psv);
        buffer.insert(buffer.end(), data, data+sizeof(psv));
    }

    inline void convertPlainToBinpack(std::string inputPath, std::string outputPath, std::ios_base::openmode om, bool validate)
    {
        constexpr std::size_t reportEveryNPositions = 100'000;

        std::cout << "Converting " << inputPath << " to " << outputPath << '\n';

        CompressedTrainingDataEntryWriter writer(outputPath, om);
        TrainingDataEntry e;

        std::string key;
        std::string value;
        std::string move;

        std::ifstream inputFile(inputPath);
        const auto base = inputFile.tellg();
        std::size_t numProcessedPositions = 0;

        for(;;)
        {
            inputFile >> key;
            if (!inputFile)
            {
                break;
            }

            if (key == "e"sv)
            {
                e.move = chess::uci::uciToMove(e.pos, move);
                if (validate && !e.isValid())
                {
                    std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                    return;
                }

                writer.addTrainingDataEntry(e);

                ++numProcessedPositions;
                const auto cur = inputFile.tellg();
                if (numProcessedPositions % reportEveryNPositions == 0)
                {
                    std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
                }

                continue;
            }

            inputFile >> std::ws;
            std::getline(inputFile, value, '\n');

            if (key == "fen"sv) e.pos = chess::Position::fromFen(value.c_str());
            if (key == "move"sv) move = value;
            if (key == "score"sv) e.score = std::stoi(value);
            if (key == "ply"sv) e.ply = std::stoi(value);
            if (key == "result"sv) e.result = std::stoi(value);
        }

        std::cout << "Finished. Converted " << numProcessedPositions << " positions.\n";
    }

    inline void convertBinpackToPlain(std::string inputPath, std::string outputPath, std::ios_base::openmode om, bool validate)
    {
        constexpr std::size_t bufferSize = MiB;

        std::cout << "Converting " << inputPath << " to " << outputPath << '\n';

        CompressedTrainingDataEntryReader reader(inputPath);
        std::ofstream outputFile(outputPath, om);
        const auto base = outputFile.tellp();
        std::size_t numProcessedPositions = 0;
        std::string buffer;
        buffer.reserve(bufferSize * 2);

        while(reader.hasNext())
        {
            auto e = reader.next();
            if (validate && !e.isValid())
            {
                std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                return;
            }

            emitPlainEntry(buffer, e);

            ++numProcessedPositions;

            if (buffer.size() > bufferSize)
            {
                outputFile << buffer;
                buffer.clear();

                const auto cur = outputFile.tellp();
                std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
            }
        }

        if (!buffer.empty())
        {
            outputFile << buffer;

            const auto cur = outputFile.tellp();
            std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
        }

        std::cout << "Finished. Converted " << numProcessedPositions << " positions.\n";
    }


    inline void convertBinToBinpack(std::string inputPath, std::string outputPath, std::ios_base::openmode om, bool validate)
    {
        constexpr std::size_t reportEveryNPositions = 100'000;

        std::cout << "Converting " << inputPath << " to " << outputPath << '\n';

        CompressedTrainingDataEntryWriter writer(outputPath, om);

        std::ifstream inputFile(inputPath, std::ios_base::binary);
        const auto base = inputFile.tellg();
        std::size_t numProcessedPositions = 0;

        nodchip::PackedSfenValue psv;
        for(;;)
        {
            inputFile.read(reinterpret_cast<char*>(&psv), sizeof(psv));
            if (inputFile.gcount() != 40)
            {
                break;
            }

            auto e = packedSfenValueToTrainingDataEntry(psv);
            if (validate && !e.isValid())
            {
                std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                std::cerr << static_cast<int>(e.move.type) << '\n';
                return;
            }

            writer.addTrainingDataEntry(e);

            ++numProcessedPositions;
            const auto cur = inputFile.tellg();
            if (numProcessedPositions % reportEveryNPositions == 0)
            {
                std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
            }
        }

        std::cout << "Finished. Converted " << numProcessedPositions << " positions.\n";
    }

    inline void convertBinpackToBin(std::string inputPath, std::string outputPath, std::ios_base::openmode om, bool validate)
    {
        constexpr std::size_t bufferSize = MiB;

        std::cout << "Converting " << inputPath << " to " << outputPath << '\n';

        CompressedTrainingDataEntryReader reader(inputPath);
        std::ofstream outputFile(outputPath, std::ios_base::binary | om);
        const auto base = outputFile.tellp();
        std::size_t numProcessedPositions = 0;
        std::vector<char> buffer;
        buffer.reserve(bufferSize * 2);

        while(reader.hasNext())
        {
            auto e = reader.next();
            if (validate && !e.isValid())
            {
                std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                return;
            }

            emitBinEntry(buffer, e);

            ++numProcessedPositions;

            if (buffer.size() > bufferSize)
            {
                outputFile.write(buffer.data(), buffer.size());
                buffer.clear();

                const auto cur = outputFile.tellp();
                std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
            }
        }

        if (!buffer.empty())
        {
            outputFile.write(buffer.data(), buffer.size());

            const auto cur = outputFile.tellp();
            std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
        }

        std::cout << "Finished. Converted " << numProcessedPositions << " positions.\n";
    }

    inline void convertBinToPlain(std::string inputPath, std::string outputPath, std::ios_base::openmode om, bool validate)
    {
        constexpr std::size_t bufferSize = MiB;

        std::cout << "Converting " << inputPath << " to " << outputPath << '\n';

        std::ifstream inputFile(inputPath, std::ios_base::binary);
        const auto base = inputFile.tellg();
        std::size_t numProcessedPositions = 0;

        std::ofstream outputFile(outputPath, om);
        std::string buffer;
        buffer.reserve(bufferSize * 2);

        nodchip::PackedSfenValue psv;
        for(;;)
        {
            inputFile.read(reinterpret_cast<char*>(&psv), sizeof(psv));
            if (inputFile.gcount() != 40)
            {
                break;
            }

            auto e = packedSfenValueToTrainingDataEntry(psv);
            if (validate && !e.isValid())
            {
                std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                return;
            }

            emitPlainEntry(buffer, e);

            ++numProcessedPositions;

            if (buffer.size() > bufferSize)
            {
                outputFile << buffer;
                buffer.clear();

                const auto cur = outputFile.tellp();
                std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
            }
        }

        if (!buffer.empty())
        {
            outputFile << buffer;

            const auto cur = outputFile.tellp();
            std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
        }

        std::cout << "Finished. Converted " << numProcessedPositions << " positions.\n";
    }

    inline void convertPlainToBin(std::string inputPath, std::string outputPath, std::ios_base::openmode om, bool validate)
    {
        constexpr std::size_t bufferSize = MiB;

        std::cout << "Converting " << inputPath << " to " << outputPath << '\n';

        std::ofstream outputFile(outputPath, std::ios_base::binary | om);
        std::vector<char> buffer;
        buffer.reserve(bufferSize * 2);

        TrainingDataEntry e;

        std::string key;
        std::string value;
        std::string move;

        std::ifstream inputFile(inputPath);
        const auto base = inputFile.tellg();
        std::size_t numProcessedPositions = 0;

        for(;;)
        {
            inputFile >> key;
            if (!inputFile)
            {
                break;
            }

            if (key == "e"sv)
            {
                e.move = chess::uci::uciToMove(e.pos, move);
                if (validate && !e.isValid())
                {
                    std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                    return;
                }

                emitBinEntry(buffer, e);

                ++numProcessedPositions;

                if (buffer.size() > bufferSize)
                {
                    outputFile.write(buffer.data(), buffer.size());
                    buffer.clear();

                    const auto cur = outputFile.tellp();
                    std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
                }

                continue;
            }

            inputFile >> std::ws;
            std::getline(inputFile, value, '\n');

            if (key == "fen"sv) e.pos = chess::Position::fromFen(value.c_str());
            if (key == "move"sv) move = value;
            if (key == "score"sv) e.score = std::stoi(value);
            if (key == "ply"sv) e.ply = std::stoi(value);
            if (key == "result"sv) e.result = std::stoi(value);
        }

        if (!buffer.empty())
        {
            outputFile.write(buffer.data(), buffer.size());

            const auto cur = outputFile.tellp();
            std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
        }

        std::cout << "Finished. Converted " << numProcessedPositions << " positions.\n";
    }

    inline void validatePlain(std::string inputPath)
    {
        constexpr std::size_t reportSize = 1000000;

        std::cout << "Validating " << inputPath << '\n';

        TrainingDataEntry e;

        std::string key;
        std::string value;
        std::string move;

        std::ifstream inputFile(inputPath);
        const auto base = inputFile.tellg();
        std::size_t numProcessedPositions = 0;
        std::size_t numProcessedPositionsBatch = 0;

        for(;;)
        {
            inputFile >> key;
            if (!inputFile)
            {
                break;
            }

            if (key == "e"sv)
            {
                e.move = chess::uci::uciToMove(e.pos, move);
                if (!e.isValid())
                {
                    std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                    return;
                }

                ++numProcessedPositions;
                ++numProcessedPositionsBatch;

                if (numProcessedPositionsBatch >= reportSize)
                {
                    numProcessedPositionsBatch -= reportSize;
                    const auto cur = inputFile.tellg();
                    std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
                }

                continue;
            }

            inputFile >> std::ws;
            std::getline(inputFile, value, '\n');

            if (key == "fen"sv) e.pos = chess::Position::fromFen(value.c_str());
            if (key == "move"sv) move = value;
            if (key == "score"sv) e.score = std::stoi(value);
            if (key == "ply"sv) e.ply = std::stoi(value);
            if (key == "result"sv) e.result = std::stoi(value);
        }

        if (numProcessedPositionsBatch)
        {
            const auto cur = inputFile.tellg();
            std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
        }

        std::cout << "Finished. Validated " << numProcessedPositions << " positions.\n";
    }

    inline void validateBin(std::string inputPath)
    {
        constexpr std::size_t reportSize = 1000000;

        std::cout << "Validating " << inputPath << '\n';

        std::ifstream inputFile(inputPath, std::ios_base::binary);
        const auto base = inputFile.tellg();
        std::size_t numProcessedPositions = 0;
        std::size_t numProcessedPositionsBatch = 0;

        nodchip::PackedSfenValue psv;
        for(;;)
        {
            inputFile.read(reinterpret_cast<char*>(&psv), sizeof(psv));
            if (inputFile.gcount() != 40)
            {
                break;
            }

            auto e = packedSfenValueToTrainingDataEntry(psv);
            if (!e.isValid())
            {
                std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                return;
            }

            ++numProcessedPositions;
            ++numProcessedPositionsBatch;

            if (numProcessedPositionsBatch >= reportSize)
            {
                numProcessedPositionsBatch -= reportSize;
                const auto cur = inputFile.tellg();
                std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
            }
        }

        if (numProcessedPositionsBatch)
        {
            const auto cur = inputFile.tellg();
            std::cout << "Processed " << (cur - base) << " bytes and " << numProcessedPositions << " positions.\n";
        }

        std::cout << "Finished. Validated " << numProcessedPositions << " positions.\n";
    }

    inline void validateBinpack(std::string inputPath)
    {
        constexpr std::size_t reportSize = 1000000;

        std::cout << "Validating " << inputPath << '\n';

        CompressedTrainingDataEntryReader reader(inputPath);
        std::size_t numProcessedPositions = 0;
        std::size_t numProcessedPositionsBatch = 0;

        while(reader.hasNext())
        {
            auto e = reader.next();
            if (!e.isValid())
            {
                std::cerr << "Illegal move " << chess::uci::moveToUci(e.pos, e.move) << " for position " << e.pos.fen() << '\n';
                return;
            }

            ++numProcessedPositions;
            ++numProcessedPositionsBatch;

            if (numProcessedPositionsBatch >= reportSize)
            {
                numProcessedPositionsBatch -= reportSize;
                std::cout << "Processed " << numProcessedPositions << " positions.\n";
            }
        }

        if (numProcessedPositionsBatch)
        {
            std::cout << "Processed " << numProcessedPositions << " positions.\n";
        }

        std::cout << "Finished. Validated " << numProcessedPositions << " positions.\n";
    }
}
