/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008 Marco Costalba

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


////
//// Includes
////

#include <cassert>
#include <string>
#include <sstream>
#include <vector>

#include "misc.h"
#include "thread.h"
#include "ucioption.h"


////
//// Variables
////

bool Chess960 = false;


////
//// Local definitions
////

namespace {

  ///
  /// Types
  ///

  enum OptionType { SPIN, COMBO, CHECK, STRING, BUTTON };

  typedef std::vector<std::string> ComboValues;

  struct Option {

    std::string name, defaultValue, currentValue;
    OptionType type;
    int minValue, maxValue;
    ComboValues comboValues;

    Option(const char* name, const char* defaultValue, OptionType = STRING);
    Option(const char* name, bool defaultValue, OptionType = CHECK);
    Option(const char* name, int defaultValue, int minValue, int maxValue);
  };

  typedef std::vector<Option> Options;

  ///
  /// Constants
  ///

  // load_defaults populates the options vector with the hard
  // coded names and default values.

  void load_defaults(Options& o) {

    o.push_back(Option("Use Search Log", false));
    o.push_back(Option("Search Log Filename", "SearchLog.txt"));
    o.push_back(Option("Book File", "book.bin"));
    o.push_back(Option("Mobility (Middle Game)", 100, 0, 200));
    o.push_back(Option("Mobility (Endgame)", 100, 0, 200));
    o.push_back(Option("Pawn Structure (Middle Game)", 100, 0, 200));
    o.push_back(Option("Pawn Structure (Endgame)", 100, 0, 200));
    o.push_back(Option("Passed Pawns (Middle Game)", 100, 0, 200));
    o.push_back(Option("Passed Pawns (Endgame)", 100, 0, 200));
    o.push_back(Option("Aggressiveness", 100, 0, 200));
    o.push_back(Option("Cowardice", 100, 0, 200));
    o.push_back(Option("King Safety Curve", "Quadratic", COMBO));

       o.back().comboValues.push_back("Quadratic");
       o.back().comboValues.push_back("Linear");  /*, "From File"*/

    o.push_back(Option("King Safety Coefficient", 40, 1, 100));
    o.push_back(Option("King Safety X Intercept", 0, 0, 20));
    o.push_back(Option("King Safety Max Slope", 30, 10, 100));
    o.push_back(Option("King Safety Max Value", 500, 100, 1000));
    o.push_back(Option("Queen Contact Check Bonus", 4, 0, 8));
    o.push_back(Option("Rook Contact Check Bonus", 2, 0, 4));
    o.push_back(Option("Queen Check Bonus", 2, 0, 4));
    o.push_back(Option("Rook Check Bonus", 1, 0, 4));
    o.push_back(Option("Bishop Check Bonus", 1, 0, 4));
    o.push_back(Option("Knight Check Bonus", 1, 0, 4));
    o.push_back(Option("Discovered Check Bonus", 3, 0, 8));
    o.push_back(Option("Mate Threat Bonus", 3, 0, 8));
    o.push_back(Option("Check Extension (PV nodes)", 2, 0, 2));
    o.push_back(Option("Check Extension (non-PV nodes)", 1, 0, 2));
    o.push_back(Option("Single Reply Extension (PV nodes)", 2, 0, 2));
    o.push_back(Option("Single Reply Extension (non-PV nodes)", 2, 0, 2));
    o.push_back(Option("Mate Threat Extension (PV nodes)", 0, 0, 2));
    o.push_back(Option("Mate Threat Extension (non-PV nodes)", 0, 0, 2));
    o.push_back(Option("Pawn Push to 7th Extension (PV nodes)", 1, 0, 2));
    o.push_back(Option("Pawn Push to 7th Extension (non-PV nodes)", 1, 0, 2));
    o.push_back(Option("Passed Pawn Extension (PV nodes)", 1, 0, 2));
    o.push_back(Option("Passed Pawn Extension (non-PV nodes)", 0, 0, 2));
    o.push_back(Option("Pawn Endgame Extension (PV nodes)", 2, 0, 2));
    o.push_back(Option("Pawn Endgame Extension (non-PV nodes)", 2, 0, 2));
    o.push_back(Option("Full Depth Moves (PV nodes)", 14, 1, 100));
    o.push_back(Option("Full Depth Moves (non-PV nodes)", 3, 1, 100));
    o.push_back(Option("Threat Depth", 5, 0, 100));
    o.push_back(Option("Selective Plies", 7, 0, 10));
    o.push_back(Option("Futility Pruning (Main Search)", true));
    o.push_back(Option("Futility Pruning (Quiescence Search)", true));
    o.push_back(Option("Futility Margin 0", 50, 0, 1000));
    o.push_back(Option("Futility Margin 1", 100, 0, 1000));
    o.push_back(Option("Futility Margin 2", 300, 0, 1000));
    o.push_back(Option("Maximum Razoring Depth", 3, 0, 4));
    o.push_back(Option("Razoring Margin", 300, 150, 600));
    o.push_back(Option("LSN filtering", false));
    o.push_back(Option("LSN Time Margin (sec)", 4, 1, 10));
    o.push_back(Option("LSN Value Margin", 200, 100, 600));
    o.push_back(Option("Randomness", 0, 0, 10));
    o.push_back(Option("Minimum Split Depth", 4, 4, 7));
    o.push_back(Option("Maximum Number of Threads per Split Point", 5, 4, 8));
    o.push_back(Option("Threads", 1, 1, 8));
    o.push_back(Option("Hash", 32, 4, 4096));
    o.push_back(Option("Clear Hash", false, BUTTON));
    o.push_back(Option("Ponder", true));
    o.push_back(Option("OwnBook", true));
    o.push_back(Option("MultiPV", 1, 1, 500));
    o.push_back(Option("UCI_ShowCurrLine", false));
    o.push_back(Option("UCI_Chess960", false));
  }

  ///
  /// Variables
  ///

  Options options;

  // Local functions
  Options::iterator option_with_name(const std::string& optionName);

  // stringify converts a value of type T to a std::string
  template<typename T>
  std::string stringify(const T& v) {

     std::ostringstream ss;
     ss << v;
     return ss.str();
  }

  // We want conversion from a bool value to be "true" or "false",
  // not "1" or "0", so add a specialization for bool type.
  template<>
  std::string stringify<bool>(const bool& v) {

    return v ? "true" : "false";
  }

  // get_option_value implements the various get_option_value_<type>
  // functions defined later, because only the option value
  // type changes a template seems a proper solution.

  template<typename T>
  T get_option_value(const std::string& optionName) {

      T ret;
      Options::iterator it = option_with_name(optionName);

      if (it != options.end())
      {
          std::istringstream ss(it->currentValue);
          ss >> ret;
      }
      return ret;
  }

  // Unfortunatly we need a specialization to convert "false" and "true"
  // to proper bool values. The culprit is that we use a non standard way
  // to store a bool value in a string, in particular we use "false" and
  // "true" instead of "0" and "1" due to how UCI protocol works.

  template<>
  bool get_option_value<bool>(const std::string& optionName) {

      Options::iterator it = option_with_name(optionName);

      return it != options.end() && it->currentValue == "true";
  }
}

////
//// Functions
////

/// init_uci_options() initializes the UCI options.  Currently, the only
/// thing this function does is to initialize the default value of the
/// "Threads" parameter to the number of available CPU cores.

void init_uci_options() {

  load_defaults(options);

  // Limit the default value of "Threads" to 7 even if we have 8 CPU cores.
  // According to Ken Dail's tests, Glaurung plays much better with 7 than
  // with 8 threads.  This is weird, but it is probably difficult to find out
  // why before I have a 8-core computer to experiment with myself.
  Options::iterator it = option_with_name("Threads");

  assert(it != options.end());

  it->defaultValue = stringify(Min(cpu_count(), 7));
  it->currentValue = stringify(Min(cpu_count(), 7));

  // Increase the minimum split depth when the number of CPUs is big.
  // It would probably be better to let this depend on the number of threads
  // instead.
  if(cpu_count() > 4)
  {
      it = option_with_name("Minimum Split Depth");

      assert(it != options.end());

      it->defaultValue = "6";
      it->currentValue = "6";
  }
}


/// print_uci_options() prints all the UCI options to the standard output,
/// in the format defined by the UCI protocol.

void print_uci_options() {

  static const char optionTypeName[][16] = {
    "spin", "combo", "check", "string", "button"
  };

  for (Options::iterator it = options.begin(); it != options.end(); ++it)
  {
      std::cout << "option name " << it->name
                << " type "       << optionTypeName[it->type];

      if (it->type != BUTTON)
      {
          std::cout << " default " << it->defaultValue;

          if (it->type == SPIN)
              std::cout << " min " << it->minValue
                        << " max " << it->maxValue;

          else if (it->type == COMBO)
              for(ComboValues::iterator itc = it->comboValues.begin();
                  itc != it->comboValues.end(); ++itc)
                      std::cout << " var " << *itc;
      }
      std::cout << std::endl;
  }
}

/// get_option_value_bool() returns the current value of a UCI parameter of
/// type "check".

bool get_option_value_bool(const std::string& optionName) {

  return get_option_value<bool>(optionName);
}


/// get_option_value_int() returns the value of a UCI parameter as an integer.
/// Normally, this function will be used for a parameter of type "spin", but
/// it could also be used with a "combo" parameter, where all the available
/// values are integers.

int get_option_value_int(const std::string& optionName) {

  return get_option_value<int>(optionName);
}


/// get_option_value_string() returns the current value of a UCI parameter as
/// a string.  It is used with parameters of type "combo" and "string".

const std::string get_option_value_string(const std::string& optionName) {

   return get_option_value<std::string>(optionName);
}


/// button_was_pressed() tests whether a UCI parameter of type "button" has
/// been selected since the last time the function was called.

bool button_was_pressed(const std::string& buttonName) {

  if (get_option_value<bool>(buttonName))
  {
    set_option_value(buttonName, "false");
    return true;
  }

  return false;
}


/// set_option_value() inserts a new value for a UCI parameter.  Note that
/// the function does not check that the new value is legal for the given
/// parameter:  This is assumed to be the responsibility of the GUI.

void set_option_value(const std::string& optionName,
                      const std::string& newValue) {

  Options::iterator it = option_with_name(optionName);

  if (it != options.end())
      it->currentValue = newValue;
  else
      std::cout << "No such option: " << optionName << std::endl;
}


/// push_button() is used to tell the engine that a UCI parameter of type
/// "button" has been selected:

void push_button(const std::string& buttonName) {

  set_option_value(buttonName, "true");
}


namespace {

    // Define constructors of Option class.

    Option::Option(const char* nm, const char* def, OptionType t)
    : name(nm), defaultValue(def), currentValue(def), type(t), minValue(0), maxValue(0) {}

    Option::Option(const char* nm, bool def, OptionType t)
    : name(nm), defaultValue(stringify(def)), currentValue(stringify(def)), type(t), minValue(0), maxValue(0) {}

    Option::Option(const char* nm, int def, int minv, int maxv)
    : name(nm), defaultValue(stringify(def)), currentValue(stringify(def)), type(SPIN), minValue(minv), maxValue(maxv) {}

    // option_with_name() tries to find a UCI option with a given
    // name.  It returns an iterator to the UCI option or to options.end(),
    // depending on whether an option with the given name exists.

    Options::iterator option_with_name(const std::string& optionName) {

        for (Options::iterator it = options.begin(); it != options.end(); ++it)
            if (it->name == optionName)
                return it;

        return options.end();
    }
}
