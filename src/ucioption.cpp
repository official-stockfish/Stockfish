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

#include <algorithm>
#include <sstream>

#include "evaluate.h"
#include "misc.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"

using std::string;

OptionsMap Options; // Global object

namespace {

/// 'On change' actions, triggered by an option's value change
void on_logger(const UCIOption& opt) { start_logger(opt); }
void on_eval(const UCIOption&) { Eval::init(); }
void on_threads(const UCIOption&) { Threads.read_uci_options(); }
void on_hash_size(const UCIOption& opt) { TT.set_size(opt); }
void on_clear_hash(const UCIOption&) { TT.clear(); }

/// Our case insensitive less() function as required by UCI protocol
bool ci_less(char c1, char c2) { return tolower(c1) < tolower(c2); }

}

bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {
  return lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), ci_less);
}


/// OptionsMap c'tor initializes the UCI options to their hard coded default
/// values and initializes the default value of "Threads" and "Min Split Depth"
/// parameters according to the number of CPU cores detected.

OptionsMap::OptionsMap() {

  int cpus = std::min(cpu_count(), MAX_THREADS);
  int msd = cpus < 8 ? 4 : 7;
  OptionsMap& o = *this;

  o["Use Debug Log"]               = UCIOption(false, on_logger);
  o["Use Search Log"]              = UCIOption(false);
  o["Search Log Filename"]         = UCIOption("SearchLog.txt");
  o["Book File"]                   = UCIOption("book.bin");
  o["Best Book Move"]              = UCIOption(false);
  o["Mobility (Middle Game)"]      = UCIOption(100, 0, 200, on_eval);
  o["Mobility (Endgame)"]          = UCIOption(100, 0, 200, on_eval);
  o["Passed Pawns (Middle Game)"]  = UCIOption(100, 0, 200, on_eval);
  o["Passed Pawns (Endgame)"]      = UCIOption(100, 0, 200, on_eval);
  o["Space"]                       = UCIOption(100, 0, 200, on_eval);
  o["Aggressiveness"]              = UCIOption(100, 0, 200, on_eval);
  o["Cowardice"]                   = UCIOption(100, 0, 200, on_eval);
  o["Min Split Depth"]             = UCIOption(msd, 4, 7, on_threads);
  o["Max Threads per Split Point"] = UCIOption(5, 4, 8, on_threads);
  o["Threads"]                     = UCIOption(cpus, 1, MAX_THREADS, on_threads);
  o["Use Sleeping Threads"]        = UCIOption(true, on_threads);
  o["Hash"]                        = UCIOption(32, 4, 8192, on_hash_size);
  o["Clear Hash"]                  = UCIOption(on_clear_hash);
  o["Ponder"]                      = UCIOption(true);
  o["OwnBook"]                     = UCIOption(false);
  o["MultiPV"]                     = UCIOption(1, 1, 500);
  o["Skill Level"]                 = UCIOption(20, 0, 20);
  o["Emergency Move Horizon"]      = UCIOption(40, 0, 50);
  o["Emergency Base Time"]         = UCIOption(200, 0, 30000);
  o["Emergency Move Time"]         = UCIOption(70, 0, 5000);
  o["Minimum Thinking Time"]       = UCIOption(20, 0, 5000);
  o["Slow Mover"]                  = UCIOption(100, 10, 1000);
  o["UCI_Chess960"]                = UCIOption(false);
  o["UCI_AnalyseMode"]             = UCIOption(false, on_eval);
}


/// operator<<() is used to output all the UCI options in chronological insertion
/// order (the idx field) and in the format defined by the UCI protocol.

std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {

  for (size_t idx = 0; idx < om.size(); idx++)
      for (OptionsMap::const_iterator it = om.begin(); it != om.end(); ++it)
          if (it->second.idx == idx)
          {
              const UCIOption& o = it->second;
              os << "\noption name " << it->first << " type " << o.type;

              if (o.type != "button")
                  os << " default " << o.defaultValue;

              if (o.type == "spin")
                  os << " min " << o.min << " max " << o.max;

              break;
          }
  return os;
}


/// UCIOption class c'tors

UCIOption::UCIOption(const char* v, Fn* f) : type("string"), min(0), max(0), idx(Options.size()), on_change(f)
{ defaultValue = currentValue = v; }

UCIOption::UCIOption(bool v, Fn* f) : type("check"), min(0), max(0), idx(Options.size()), on_change(f)
{ defaultValue = currentValue = (v ? "true" : "false"); }

UCIOption::UCIOption(Fn* f) : type("button"), min(0), max(0), idx(Options.size()), on_change(f)
{}

UCIOption::UCIOption(int v, int minv, int maxv, Fn* f) : type("spin"), min(minv), max(maxv), idx(Options.size()), on_change(f)
{ std::ostringstream ss; ss << v; defaultValue = currentValue = ss.str(); }


/// UCIOption::operator=() updates currentValue. Normally it's up to the GUI to
/// check for option's limits, but we could receive the new value directly from
/// the user by console window, so let's check the bounds anyway.

void UCIOption::operator=(const string& v) {

  assert(!type.empty());

  if (   (type == "button" || !v.empty())
      && (type != "check"  || (v == "true" || v == "false"))
      && (type != "spin"   || (atoi(v.c_str()) >= min && atoi(v.c_str()) <= max)))
  {
      if (type != "button")
          currentValue = v;

      if (on_change)
          (*on_change)(*this);
  }
}
