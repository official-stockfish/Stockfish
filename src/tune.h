/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#ifndef TUNE_H_INCLUDED
#define TUNE_H_INCLUDED

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>  // IWYU pragma: keep
#include <utility>
#include <vector>

namespace Stockfish {

class OptionsMap;

using Range    = std::pair<int, int>;  // Option's min-max values
using RangeFun = Range(int);

// Default Range function, to calculate Option's min-max values
inline Range default_range(int v) { return v > 0 ? Range(0, 2 * v) : Range(2 * v, 0); }

struct SetRange {
    explicit SetRange(RangeFun f) :
        fun(f) {}
    SetRange(int min, int max) :
        fun(nullptr),
        range(min, max) {}
    Range operator()(int v) const { return fun ? fun(v) : range; }

    RangeFun* fun;
    Range     range;
};

#define SetDefaultRange SetRange(default_range)


// Tune class implements the 'magic' code that makes the setup of a fishtest tuning
// session as easy as it can be. Mainly you have just to remove const qualifiers
// from the variables you want to tune and flag them for tuning, so if you have:
//
//   const Value myValue[][2] = { { V(100), V(20) }, { V(7), V(78) } };
//
// If you have a my_post_update() function to run after values have been updated,
// and a my_range() function to set custom Option's min-max values, then you just
// remove the 'const' qualifiers and write somewhere below in the file:
//
//   TUNE(SetRange(my_range), myValue, my_post_update);
//
// You can also set the range directly, and restore the default at the end
//
//   TUNE(SetRange(-100, 100), myValue, SetDefaultRange);
//
// In case update function is slow and you have many parameters, you can add:
//
//   UPDATE_ON_LAST();
//
// And the values update, including post update function call, will be done only
// once, after the engine receives the last UCI option, that is the one defined
// and created as the last one, so the GUI should send the options in the same
// order in which have been defined.

class Tune {

    using PostUpdate = void();  // Post-update function

    Tune() { read_results(); }
    Tune(const Tune&)           = delete;
    void operator=(const Tune&) = delete;
    void read_results();

    static Tune& instance() {
        static Tune t;
        return t;
    }  // Singleton

    // Use polymorphism to accommodate Entry of different types in the same vector
    struct EntryBase {
        virtual ~EntryBase()       = default;
        virtual void init_option() = 0;
        virtual void read_option() = 0;
    };

    template<typename T>
    struct Entry: public EntryBase {

        static_assert(!std::is_const_v<T>, "Parameter cannot be const!");

        static_assert(std::is_same_v<T, int> || std::is_same_v<T, PostUpdate>,
                      "Parameter type not supported!");

        Entry(const std::string& n, T& v, const SetRange& r) :
            name(n),
            value(v),
            range(r) {}
        void operator=(const Entry&) = delete;  // Because 'value' is a reference
        void init_option() override;
        void read_option() override;

        std::string name;
        T&          value;
        SetRange    range;
    };

    // Our facility to fill the container, each Entry corresponds to a parameter
    // to tune. We use variadic templates to deal with an unspecified number of
    // entries, each one of a possible different type.
    static std::string next(std::string& names, bool pop = true);

    int add(const SetRange&, std::string&&) { return 0; }

    template<typename T, typename... Args>
    int add(const SetRange& range, std::string&& names, T& value, Args&&... args) {
        list.push_back(std::unique_ptr<EntryBase>(new Entry<T>(next(names), value, range)));
        return add(range, std::move(names), args...);
    }

    // Template specialization for arrays: recursively handle multi-dimensional arrays
    template<typename T, size_t N, typename... Args>
    int add(const SetRange& range, std::string&& names, T (&value)[N], Args&&... args) {
        for (size_t i = 0; i < N; i++)
            add(range, next(names, i == N - 1) + "[" + std::to_string(i) + "]", value[i]);
        return add(range, std::move(names), args...);
    }

    // Template specialization for SetRange
    template<typename... Args>
    int add(const SetRange&, std::string&& names, SetRange& value, Args&&... args) {
        return add(value, (next(names), std::move(names)), args...);
    }

    static void make_option(OptionsMap* options, const std::string& n, int v, const SetRange& r);

    std::vector<std::unique_ptr<EntryBase>> list;

   public:
    template<typename... Args>
    static int add(const std::string& names, Args&&... args) {
        return instance().add(SetDefaultRange, names.substr(1, names.size() - 2),
                              args...);  // Remove trailing parenthesis
    }
    static void init(OptionsMap& o) {
        options = &o;
        for (auto& e : instance().list)
            e->init_option();
        read_options();
    }  // Deferred, due to UCIEngine::Options access
    static void read_options() {
        for (auto& e : instance().list)
            e->read_option();
    }

    static bool        update_on_last;
    static OptionsMap* options;
};

// Some macro magic :-) we define a dummy int variable that the compiler initializes calling Tune::add()
#define STRINGIFY(x) #x
#define UNIQUE2(x, y) x##y
#define UNIQUE(x, y) UNIQUE2(x, y)  // Two indirection levels to expand __LINE__
#define TUNE(...) int UNIQUE(p, __LINE__) = Tune::add(STRINGIFY((__VA_ARGS__)), __VA_ARGS__)

#define UPDATE_ON_LAST() bool UNIQUE(p, __LINE__) = Tune::update_on_last = true

}  // namespace Stockfish

#endif  // #ifndef TUNE_H_INCLUDED
