/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

struct OptionsMap;

/// UCIOption class implements an option as defined by UCI protocol
class UCIOption {

  typedef void (Fn)(const UCIOption&);

public:
  UCIOption(Fn* = NULL);
  UCIOption(bool v, Fn* = NULL);
  UCIOption(const char* v, Fn* = NULL);
  UCIOption(int v, int min, int max, Fn* = NULL);

  void operator=(const std::string& v);

  operator int() const {
    assert(type == "check" || type == "spin");
    return (type == "spin" ? atoi(currentValue.c_str()) : currentValue == "true");
  }

  operator std::string() const {
    assert(type == "string");
    return currentValue;
  }

private:
  friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

  std::string defaultValue, currentValue, type;
  int min, max;
  size_t idx;
  Fn* on_change;
};


/// Custom comparator because UCI options should be case insensitive
struct CaseInsensitiveLess {
  bool operator() (const std::string&, const std::string&) const;
};


/// Our options container is actually a map with a customized c'tor
struct OptionsMap : public std::map<std::string, UCIOption, CaseInsensitiveLess> {
  OptionsMap();
};

extern std::ostream& operator<<(std::ostream&, const OptionsMap&);
extern OptionsMap Options;

#endif // !defined(UCIOPTION_H_INCLUDED)
