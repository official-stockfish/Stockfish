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

#include <algorithm>
#include <cassert>
#include <ostream>
#include <sstream>
#include <cmath>

#include "evaluate.h"
#include "misc.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

using std::string;

namespace Stockfish {

namespace UCI {

OptionsMap Options; // Global object

/// 'On change' actions, triggered by an option's value change
void on_clear_hash(const Option&) { Search::clear(); }
void on_hash_size(const Option& o) { TT.resize(o.get_int()); }
void on_logger(const Option& o) { start_logger(o.get_string()); }
void on_threads(const Option& o) { Threads.set(o.get_int()); }
void on_tb_path(const Option& o) { Tablebases::init(o.get_string()); }
void on_use_NNUE(const Option& ) { Eval::NNUE::init(); }
void on_eval_file(const Option& ) { Eval::NNUE::init(); }

std::string option_type_to_string(OptionType t) {
  switch (t) {
    case OptionType::String:
      return "string";
    case OptionType::Button:
      return "button";
    case OptionType::Check:
      return "check";
    case OptionType::Spin:
      return "spin";
    case OptionType::Combo:
      return "combo";
  }

  assert(false);
  return "";
}

/// Our case insensitive less() function as required by UCI protocol
bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {

  return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
         [](char c1, char c2) { return tolower(c1) < tolower(c2); });
}

/// operator<<() is used to print all the options default values in chronological
/// insertion order (the idx field) and in the format defined by the UCI protocol.

std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {

  for (auto&& iter : om.ordered) {
      const std::string& name = iter->first;
      const Option& o = iter->second;
      os << "\noption name " << name << " type " << option_type_to_string(o.type);

      if (o.type == OptionType::String || o.type == OptionType::Check || o.type == OptionType::Combo)
          os << " default " << o.defaultValue;

      if (o.type == OptionType::Spin)
          os << " default " << static_cast<int>(std::round(std::stod(o.defaultValue)))
             << " min "     << o.min
             << " max "     << o.max;
  }

  return os;
}


/// Option class constructors and conversion operators

Option::Option(OptionType t) :
  type(t)
{ }

Option Option::string(const std::string& v) {
  Option opt(OptionType::String);
  opt.defaultValue = v;
  opt.currentValue = v;
  opt.allowEmpty = true;
  return opt;
}

Option Option::button(OnChange ptr) {
  Option opt(OptionType::Button);
  opt.allowEmpty = true;
  opt.onChange = ptr;
  return opt;
}

Option Option::check(bool v) {
  Option opt(OptionType::Check);
  opt.defaultValue = (v ? "true" : "false");
  opt.currentValue = opt.defaultValue;
  opt.allowEmpty = false;
  return opt;
}

Option Option::spin(int v, int min, int max) {
  Option opt(OptionType::Spin);
  opt.defaultValue = std::to_string(v);
  opt.currentValue = opt.defaultValue;
  opt.min = min;
  opt.max = max;
  opt.allowEmpty = false;
  return opt;
}

Option Option::combo(const std::string& v, const std::string& allowedValues) {
  Option opt(OptionType::Combo);
  opt.defaultValue = v;
  opt.currentValue = v;
  opt.allowEmpty = false;
  {
      std::string token;
      std::istringstream ss(allowedValues);
      while (ss >> token)
          if (token != "var")
              opt.allowedComboValues.emplace(token);
  }
  return opt;
}

Option& Option::on_change(OnChange ptr) & {
  onChange = ptr;
  return *this;
}

Option&& Option::on_change(OnChange ptr) && {
  onChange = ptr;
  return std::move(*this);
}

Option& Option::allow_empty(bool allow) & {
  allowEmpty = allow;
  return *this;
}

Option&& Option::allow_empty(bool allow) && {
  allowEmpty = allow;
  return std::move(*this);
}

int Option::get_int() const {
  assert(type == OptionType::Spin);
  return static_cast<int>(std::round(std::stod(currentValue)));
}

double Option::get_double() const {
  assert(type == OptionType::Spin);
  return std::stod(currentValue);
}

std::string Option::get_string() const {
  assert(type == OptionType::Combo || type == OptionType::String);
  return currentValue;
}

bool Option::get_bool() const {
  assert(type == OptionType::Check);
  return currentValue == "true";
}

void OptionsMap::clear() {
  unordered.clear();
  ordered.clear();
}

bool OptionsMap::exists(const std::string& name) {
  return unordered.count(name) != 0;
}

void OptionsMap::set(const std::string& name, const std::string& value) {
  unordered.at(name) = value;
}

void OptionsMap::add(const std::string& name, Option&& option) {
  auto [iter, inserted] = unordered.emplace(name, std::move(option));
  ordered.emplace_back(iter);
}

const Option& OptionsMap::get(const std::string& name) {
  return unordered.at(name);
}

int OptionsMap::get_int(const std::string& name) {
  return get(name).get_int();
}

double OptionsMap::get_double(const std::string& name) {
  return get(name).get_double();
}

std::string OptionsMap::get_string(const std::string& name) {
  return get(name).get_string();
}

bool OptionsMap::get_bool(const std::string& name) {
  return get(name).get_bool();
}

/// UCI::init() initializes the UCI options to their hard-coded default values

void init() {

  constexpr int MaxHashMB = Is64Bit ? 33554432 : 2048;

  Options.clear();

  Options.add("Debug Log File",    Option::string("").on_change(on_logger));
  Options.add("Threads",           Option::spin(1, 1, 512).on_change(on_threads));
  Options.add("Hash",              Option::spin(16, 1, MaxHashMB).on_change(on_hash_size));
  Options.add("Clear Hash",        Option::button(on_clear_hash));
  Options.add("Ponder",            Option::check(false));
  Options.add("MultiPV",           Option::spin(1, 1, 500));
  Options.add("Skill Level",       Option::spin(20, 0, 20));
  Options.add("Move Overhead",     Option::spin(10, 0, 5000));
  Options.add("Slow Mover",        Option::spin(100, 10, 1000));
  Options.add("nodestime",         Option::spin(0, 0, 10000));
  Options.add("UCI_Chess960",      Option::check(false));
  Options.add("UCI_AnalyseMode",   Option::check(false));
  Options.add("UCI_LimitStrength", Option::check(false));
  Options.add("UCI_Elo",           Option::spin(1350, 1350, 2850));
  Options.add("UCI_ShowWDL",       Option::check(false));
  Options.add("SyzygyPath",        Option::string("<empty>").on_change(on_tb_path));
  Options.add("SyzygyProbeDepth",  Option::spin(1, 1, 100));
  Options.add("Syzygy50MoveRule",  Option::check(true));
  Options.add("SyzygyProbeLimit",  Option::spin(7, 0, 7));
  Options.add("Use NNUE",          Option::check(true).on_change(on_use_NNUE));
  Options.add("EvalFile",          Option::string(EvalFileDefaultName).on_change(on_eval_file).allow_empty(false));
}

/// operator=() updates currentValue and triggers on_change() action. It's up to
/// the GUI to check for option's limits, but we could receive the new value
/// from the user by console window, so let's check the bounds anyway.

Option& Option::operator=(const std::string& v) {

  if (   (!allowEmpty && v.empty())
      || (type == OptionType::Check && v != "true" && v != "false")
      || (type == OptionType::Spin && (std::stod(v) < min || std::stod(v) > max))
      || (type == OptionType::Combo && allowedComboValues.count(v) == 0))
      return *this;

  if (type != OptionType::Button)
      currentValue = v;

  if (onChange)
      onChange(*this);

  return *this;
}

} // namespace UCI

} // namespace Stockfish
