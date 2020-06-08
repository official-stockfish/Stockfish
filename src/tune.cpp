/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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
#include <iostream>
#include <sstream>

#include "types.h"
#include "misc.h"
#include "uci.h"

using std::string;

bool Tune::update_on_last;
const UCI::Option* LastOption = nullptr;
BoolConditions Conditions;
static std::map<std::string, int> TuneResults;

string Tune::next(string& names, bool pop) {

  string name;

  do {
      string token = names.substr(0, names.find(','));

      if (pop)
          names.erase(0, token.size() + 1);

      std::stringstream ws(token);
      name += (ws >> token, token); // Remove trailing whitespace

  } while (  std::count(name.begin(), name.end(), '(')
           - std::count(name.begin(), name.end(), ')'));

  return name;
}

static void on_tune(const UCI::Option& o) {

  if (!Tune::update_on_last || LastOption == &o)
      Tune::read_options();
}

static void make_option(const string& n, int v, const SetRange& r) {

  // Do not generate option when there is nothing to tune (ie. min = max)
  if (r(v).first == r(v).second)
      return;

  if (TuneResults.count(n))
      v = TuneResults[n];

  Options[n] << UCI::Option(v, r(v).first, r(v).second, on_tune);
  LastOption = &Options[n];

  // Print formatted parameters, ready to be copy-pasted in fishtest
  std::cout << n << ","
            << v << ","
            << r(v).first << "," << r(v).second << ","
            << (r(v).second - r(v).first) / 20.0 << ","
            << "0.0020"
            << std::endl;
}

template<> void Tune::Entry<int>::init_option() { make_option(name, value, range); }

template<> void Tune::Entry<int>::read_option() {
  if (Options.count(name))
      value = int(Options[name]);
}

template<> void Tune::Entry<Value>::init_option() { make_option(name, value, range); }

template<> void Tune::Entry<Value>::read_option() {
  if (Options.count(name))
      value = Value(int(Options[name]));
}

template<> void Tune::Entry<Score>::init_option() {
  make_option("m" + name, mg_value(value), range);
  make_option("e" + name, eg_value(value), range);
}

template<> void Tune::Entry<Score>::read_option() {
  if (Options.count("m" + name))
      value = make_score(int(Options["m" + name]), eg_value(value));

  if (Options.count("e" + name))
      value = make_score(mg_value(value), int(Options["e" + name]));
}

// Instead of a variable here we have a PostUpdate function: just call it
template<> void Tune::Entry<Tune::PostUpdate>::init_option() {}
template<> void Tune::Entry<Tune::PostUpdate>::read_option() { value(); }


// Set binary conditions according to a probability that depends
// on the corresponding parameter value.

void BoolConditions::set() {

  static PRNG rng(now());
  static bool startup = true; // To workaround fishtest bench

  for (size_t i = 0; i < binary.size(); i++)
      binary[i] = !startup && (values[i] + int(rng.rand<unsigned>() % variance) > threshold);

  startup = false;

  for (size_t i = 0; i < binary.size(); i++)
      sync_cout << binary[i] << sync_endl;
}


// Init options with tuning session results instead of default values. Useful to
// get correct bench signature after a tuning session or to test tuned values.
// Just copy fishtest tuning results in a result.txt file and extract the
// values with:
//
// cat results.txt | sed 's/^param: \([^,]*\), best: \([^,]*\).*/  TuneResults["\1"] = int(round(\2));/'
//
// Then paste the output below, as the function body

#include <cmath>

void Tune::read_results() {

  /* ...insert your values here... */
}
