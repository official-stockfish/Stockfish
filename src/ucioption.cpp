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

#include <cctype>
#include <iostream>
#include <sstream>

#include "misc.h"
#include "thread.h"
#include "ucioption.h"

using std::string;
using std::cout;
using std::endl;

OptionsMap Options; // Global object


// Our case insensitive less() function as required by UCI protocol
bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {

  int c1, c2;
  size_t i = 0;

  while (i < s1.size() && i < s2.size())
  {
      c1 = tolower(s1[i]);
      c2 = tolower(s2[i++]);

      if (c1 != c2)
          return c1 < c2;
  }
  return s1.size() < s2.size();
}


// stringify() converts a numeric value of type T to a std::string
template<typename T>
static string stringify(const T& v) {

  std::ostringstream ss;
  ss << v;
  return ss.str();
}


/// init_uci_options() initializes the UCI options to their hard coded default
/// values and initializes the default value of "Threads" and "Minimum Split Depth"
/// parameters according to the number of CPU cores.

void init_uci_options() {

  Options["Use Search Log"] = Option(false);
  Options["Search Log Filename"] = Option("SearchLog.txt");
  Options["Book File"] = Option("book.bin");
  Options["Best Book Move"] = Option(false);
  Options["Mobility (Middle Game)"] = Option(100, 0, 200);
  Options["Mobility (Endgame)"] = Option(100, 0, 200);
  Options["Pawn Structure (Middle Game)"] = Option(100, 0, 200);
  Options["Pawn Structure (Endgame)"] = Option(100, 0, 200);
  Options["Passed Pawns (Middle Game)"] = Option(100, 0, 200);
  Options["Passed Pawns (Endgame)"] = Option(100, 0, 200);
  Options["Space"] = Option(100, 0, 200);
  Options["Aggressiveness"] = Option(100, 0, 200);
  Options["Cowardice"] = Option(100, 0, 200);
  Options["Check Extension (PV nodes)"] = Option(2, 0, 2);
  Options["Check Extension (non-PV nodes)"] = Option(1, 0, 2);
  Options["Single Evasion Extension (PV nodes)"] = Option(2, 0, 2);
  Options["Single Evasion Extension (non-PV nodes)"] = Option(2, 0, 2);
  Options["Mate Threat Extension (PV nodes)"] = Option(2, 0, 2);
  Options["Mate Threat Extension (non-PV nodes)"] = Option(2, 0, 2);
  Options["Pawn Push to 7th Extension (PV nodes)"] = Option(1, 0, 2);
  Options["Pawn Push to 7th Extension (non-PV nodes)"] = Option(1, 0, 2);
  Options["Passed Pawn Extension (PV nodes)"] = Option(1, 0, 2);
  Options["Passed Pawn Extension (non-PV nodes)"] = Option(0, 0, 2);
  Options["Pawn Endgame Extension (PV nodes)"] = Option(2, 0, 2);
  Options["Pawn Endgame Extension (non-PV nodes)"] = Option(2, 0, 2);
  Options["Minimum Split Depth"] = Option(4, 4, 7);
  Options["Maximum Number of Threads per Split Point"] = Option(5, 4, 8);
  Options["Threads"] = Option(1, 1, MAX_THREADS);
  Options["Use Sleeping Threads"] = Option(false);
  Options["Hash"] = Option(32, 4, 8192);
  Options["Clear Hash"] = Option(false, "button");
  Options["Ponder"] = Option(true);
  Options["OwnBook"] = Option(true);
  Options["MultiPV"] = Option(1, 1, 500);
  Options["Emergency Move Horizon"] = Option(40, 0, 50);
  Options["Emergency Base Time"] = Option(200, 0, 60000);
  Options["Emergency Move Time"] = Option(70, 0, 5000);
  Options["Minimum Thinking Time"] = Option(20, 0, 5000);
  Options["UCI_Chess960"] = Option(false); // Just a dummy but needed by GUIs
  Options["UCI_AnalyseMode"] = Option(false);

  // Set some SMP parameters accordingly to the detected CPU count
  Option& thr = Options["Threads"];
  Option& msd = Options["Minimum Split Depth"];

  thr.defaultValue = thr.currentValue = stringify(cpu_count());

  if (cpu_count() >= 8)
      msd.defaultValue = msd.currentValue = stringify(7);
}


/// print_uci_options() prints all the UCI options to the standard output,
/// in chronological insertion order (the idx field) and in the format
/// defined by the UCI protocol.

void print_uci_options() {

  for (size_t i = 0; i <= Options.size(); i++)
      for (OptionsMap::const_iterator it = Options.begin(); it != Options.end(); ++it)
          if (it->second.idx == i)
          {
              const Option& o = it->second;
              cout << "\noption name " << it->first << " type " << o.type;

              if (o.type != "button")
                  cout << " default " << o.defaultValue;

              if (o.type == "spin")
                  cout << " min " << o.minValue << " max " << o.maxValue;

              break;
          }
  cout << endl;
}


/// Option class c'tors

Option::Option(const char* def) : type("string"), idx(Options.size()), minValue(0), maxValue(0)
{ defaultValue = currentValue = def; }

Option::Option(bool def, string t) : type(t), idx(Options.size()), minValue(0), maxValue(0)
{ defaultValue = currentValue = (def ? "true" : "false"); }

Option::Option(int def, int minv, int maxv) : type("spin"), idx(Options.size()), minValue(minv), maxValue(maxv)
{ defaultValue = currentValue = stringify(def); }


/// set_value() updates currentValue of the Option object. Normally it's up to
/// the GUI to check for option's limits, but we could receive the new value
/// directly from the user by teminal window. So let's check the bounds anyway.

void Option::set_value(const string& value) {

  assert(!type.empty());

  if (    (type == "check" || type == "button")
      && !(value == "true" || value == "false"))
      return;

  if (type == "spin")
  {
      int v = atoi(value.c_str());
      if (v < minValue || v > maxValue)
          return;
  }

  currentValue = value;
}
