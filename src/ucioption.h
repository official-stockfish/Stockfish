/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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

#if !defined(UCIOPTION_H_INCLUDED)
#define UCIOPTION_H_INCLUDED

#include <cassert>
#include <cstdlib>
#include <map>
#include <string>

class UCIOption {
public:
  UCIOption() {} // To be used in a std::map
  UCIOption(const char* defaultValue);
  UCIOption(bool defaultValue, std::string type = "check");
  UCIOption(int defaultValue, int minValue, int maxValue);

  void set_value(const std::string& v);
  template<typename T> T value() const;

private:
  friend class OptionsMap;

  std::string defaultValue, currentValue, type;
  int minValue, maxValue;
  size_t idx;
};


/// Custom comparator because UCI options should not be case sensitive
struct CaseInsensitiveLess {
  bool operator() (const std::string&, const std::string&) const;
};


/// Our options container is actually a map with a customized c'tor
class OptionsMap : public std::map<std::string, UCIOption, CaseInsensitiveLess> {
public:
  OptionsMap();
  std::string print_all() const;
};

extern OptionsMap Options;


/// Option::value() definition and specializations
template<typename T>
T UCIOption::value() const {

  assert(type == "spin");
  return T(atoi(currentValue.c_str()));
}

template<>
inline std::string UCIOption::value<std::string>() const {

  assert(type == "string");
  return currentValue;
}

template<>
inline bool UCIOption::value<bool>() const {

  assert(type == "check" || type == "button");
  return currentValue == "true";
}

#endif // !defined(UCIOPTION_H_INCLUDED)
