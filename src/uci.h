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

#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include <map>
#include <set>
#include <vector>
#include <string>

#include "types.h"

namespace Stockfish {

class Position;

namespace UCI {

class Option;

enum struct OptionType {
  String,
  Button,
  Check,
  Spin,
  Combo
};

std::string option_type_to_string(OptionType t);

/// Custom comparator because UCI options should be case insensitive
struct CaseInsensitiveLess {
  bool operator() (const std::string&, const std::string&) const;
};

struct OptionsMap {

  void clear();
  bool exists(const std::string& name);
  void add(const std::string& name, Option&& option);
  void set(const std::string& name, const std::string& value);
  const Option& get(const std::string& name);
  int get_int(const std::string& name);
  double get_double(const std::string& name);
  std::string get_string(const std::string& name);
  bool get_bool(const std::string& name);

  friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

private:
  using MapType = std::map<std::string, Option, CaseInsensitiveLess>;
  using IterType = typename MapType::const_iterator;

  MapType unordered;
  std::vector<IterType> ordered;
};

/// Option class implements an option as defined by UCI protocol
class Option {

  typedef void (*OnChange)(const Option&);

public:
  static Option string(const std::string& v);
  static Option button(OnChange ptr);
  static Option check(bool v);
  static Option spin(int v, int min, int max);
  static Option combo(const std::string& v, const std::string& allowedValues);

  Option&& on_change(OnChange ptr) &&;
  Option&  on_change(OnChange ptr) &;
  Option&& allow_empty(bool allow) &&;
  Option&  allow_empty(bool allow) &;

  Option(const Option&) = delete;
  Option(Option&&) = default;
  Option& operator=(const Option&) = delete;
  Option& operator=(Option&&) = default;

  Option& operator=(const std::string&);

  int get_int() const;
  double get_double() const;
  std::string get_string() const;
  bool get_bool() const;

private:
  Option(OptionType t);

  friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

  OptionType type;
  std::set<std::string, CaseInsensitiveLess> allowedComboValues;
  std::string defaultValue;
  std::string currentValue;
  int min = 0;
  int max = 0;
  OnChange onChange = nullptr;
  bool allowEmpty = false;
};

void loop(int argc, char* argv[]);
std::string value(Value v);
std::string square(Square s);
std::string move(Move m, bool chess960);
std::string pv(const Position& pos, Depth depth, Value alpha, Value beta);
std::string wdl(Value v, int ply);
Move to_move(const Position& pos, std::string& str);

void init();

extern OptionsMap Options;

} // namespace UCI

} // namespace Stockfish

#endif // #ifndef UCI_H_INCLUDED
