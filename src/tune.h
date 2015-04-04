/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace UCI { class Option; }

typedef std::pair<int, int> Range; // Option's min-max values
typedef Range (RangeFun) (int);

RangeFun default_range;

struct SetRange {
  SetRange() = default;
  explicit SetRange(RangeFun f) : fun(f) {}
  SetRange(int min, int max) : fun(nullptr), range(min, max) {}
  Range operator()(int v) const { return fun ? fun(v) : range; }

  RangeFun* fun = default_range;
  Range range;
};

extern SetRange SetDefaultRange;


/// Tune class implements the 'magic' code that makes the setup of a fishtest
/// tuning session as easy as it can be. Mainly you have just to remove const
/// qualifiers from the variables you want to tune and flag them for tuning, so
/// if you have:
///
///   const Score myScore = S(10, 15);
///   const Value myValue[][2] = { { V(100), V(20) }, { V(7), V(78) } };
///
/// If you have a my_post_update() function to run after values have been updated,
/// and a my_range() function to set custom Option's min-max values, then you just
/// remove the 'const' qualifiers and write somewhere below in the file:
///
///   TUNE(SetRange(my_range), myScore, myValue, my_post_update);
///
/// You can also set the range directly, and restore the default at the end
///
///   TUNE(SetRange(-100, 100), myScore, SetDefaultRange);

class Tune {

  typedef void (PostUpdate) (); // Post-update function

  Tune() = default;
  Tune(const Tune&) = delete;
  void operator=(const Tune&) = delete;

  static Tune& instance() { static Tune t; return t; } // Singleton

  // Use polymorphism to accomodate Entry of different types in the same vector
  struct EntryBase {
    virtual ~EntryBase() = default;
    virtual void make_option() = 0;
    virtual void read_option() = 0;
    void make_option(const std::string& n, int v, const SetRange& r);
  };

  template<typename T>
  struct Entry : public EntryBase {

    static_assert(!std::is_const<T>::value, "Parameter cannot be const!");

    static_assert(   std::is_same<T,   int>::value
                  || std::is_same<T, Value>::value
                  || std::is_same<T, Score>::value
                  || std::is_same<T, PostUpdate>::value, "Parameter type not supported!");

    Entry(const std::string& n, T& v, const SetRange& r) : name(n), value(v), range(r) {}
    void operator=(const Entry&) = delete; // Because 'value' is a reference
    void make_option();
    void read_option();

    std::string name;
    T& value;
    SetRange range;
  };

  // Our facilty to fill the container, each Entry corresponds to a parameter to tune.
  // We use variadic templates to deal with an unspecified number of entries, each one
  // of a possible different type.
  void add(std::vector<std::string>&&) {}

  template<typename T, typename... Args>
  void add(std::vector<std::string>&& names, T& value, Args&&... args) {
    list.push_back(std::unique_ptr<EntryBase>(new Entry<T>(names.back(), value, range)));
    add((names.pop_back(), std::move(names)), args...);
  }

  // Template specialization for arrays: recursively handle multi-dimensional arrays
  template<typename T, size_t N, typename... Args>
  void add(std::vector<std::string>&& names, T (&value)[N], Args&&... args) {
    for (size_t i = 0; i < N; i++)
        add(std::vector<std::string>(1, names.back() + "_" + std::to_string(i)), value[i]);
    add((names.pop_back(), std::move(names)), args...);
  }

  // Template specialization for SetRange
  template<typename... Args>
  void add(std::vector<std::string>&& names, SetRange& value, Args&&... args) {
    range = value;
    add((names.pop_back(), std::move(names)), args...);
  }

  std::vector<std::unique_ptr<EntryBase>> list;
  SetRange range;

  friend struct Parse;

public:
  static void init() { for (auto& e : instance().list) e->make_option(); }
  static void on_tune(const UCI::Option&) { for (auto& e : instance().list) e->read_option(); }
};

/// Glue class that reads the raw input from TUNE macro and feeds Tune::add()
struct Parse {
  template<typename... Args>
  Parse(const std::string& names, Args&&... args) { Tune::instance().add(split(names), args...); }
  std::vector<std::string> split(const std::string& names);
};

// Some macro magic :-) Mainly we define a dummy variable of type Parse that just
// properly feeds Tune::add() in its c'tor.
#define STRINGIFY(x) #x
#define UNIQUE2(x,y) x ## y
#define UNIQUE(x,y) UNIQUE2(x,y) // Two indirection levels to expand __LINE__
#define TUNE(...) Parse UNIQUE(p, __LINE__)(STRINGIFY((__VA_ARGS__)), __VA_ARGS__)

#endif // #ifndef TUNE_H_INCLUDED
