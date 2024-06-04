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

#ifndef UCIOPTION_H_INCLUDED
#define UCIOPTION_H_INCLUDED

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>

namespace Stockfish {
// Define a custom comparator, because the UCI options should be case-insensitive
struct CaseInsensitiveLess {
    bool operator()(const std::string&, const std::string&) const;
};

class OptionsMap;

// The Option class implements each option as specified by the UCI protocol
class Option {
   public:
    using OnChange = std::function<std::optional<std::string>(const Option&)>;

    Option(const OptionsMap*);
    Option(OnChange = nullptr);
    Option(bool v, OnChange = nullptr);
    Option(const char* v, OnChange = nullptr);
    Option(double v, int minv, int maxv, OnChange = nullptr);
    Option(const char* v, const char* cur, OnChange = nullptr);

    Option& operator=(const std::string&);
    operator int() const;
    operator std::string() const;
    bool operator==(const char*) const;
    bool operator!=(const char*) const;

    friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

   private:
    friend class OptionsMap;
    friend class Engine;
    friend class Tune;

    void operator<<(const Option&);

    std::string       defaultValue, currentValue, type;
    int               min, max;
    size_t            idx;
    OnChange          on_change;
    const OptionsMap* parent = nullptr;
};

class OptionsMap {
   public:
    using InfoListener = std::function<void(std::optional<std::string>)>;

    OptionsMap()                             = default;
    OptionsMap(const OptionsMap&)            = delete;
    OptionsMap(OptionsMap&&)                 = delete;
    OptionsMap& operator=(const OptionsMap&) = delete;
    OptionsMap& operator=(OptionsMap&&)      = delete;

    void add_info_listener(InfoListener&&);

    void setoption(std::istringstream&);

    Option  operator[](const std::string&) const;
    Option& operator[](const std::string&);

    std::size_t count(const std::string&) const;

   private:
    friend class Engine;
    friend class Option;

    friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

    // The options container is defined as a std::map
    using OptionsStore = std::map<std::string, Option, CaseInsensitiveLess>;

    OptionsStore options_map;
    InfoListener info;
};

}
#endif  // #ifndef UCIOPTION_H_INCLUDED
