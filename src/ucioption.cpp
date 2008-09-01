/*
  Glaurung, a UCI chess playing engine.
  Copyright (C) 2004-2008 Tord Romstad

  Glaurung is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Glaurung is distributed in the hope that it will be useful,
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
#include <map>
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

  enum OptionType { SPIN, COMBO, CHECK, STRING, BUTTON, OPTION_TYPE_NONE };

  typedef std::vector<std::string> ComboValues;

  struct OptionValue
  {
    std::string defaultValue, currentValue;
    OptionType type;
    int minValue, maxValue;
    ComboValues comboValues;

    // Helper to convert a bool or an int in a std::string
    template<typename T> std::string stringify(const T& v);

    OptionValue(); // this is needed to use a std::map
    OptionValue(const char* defaultValue, OptionType = STRING);
    OptionValue(bool defaultValue);
    OptionValue(int defaultValue, int minValue, int maxValue);
  };

  typedef std::map<std::string, OptionValue> Options;

  ///
  /// Constants
  ///

  // load-defaults populates the options map with the hard
  // coded options names and their default values.
  void load_defaults(Options& options) {

    options["Use Search Log"]                            = OptionValue(false);
    options["Search Log Filename"]                       = OptionValue("SearchLog.txt");
    options["Book File"]                                 = OptionValue("book.bin");
    options["Mobility (Middle Game)"]                    = OptionValue(100, 0, 200);
    options["Mobility (Endgame)"]                        = OptionValue(100, 0, 200);
    options["Pawn Structure (Middle Game)"]              = OptionValue(100, 0, 200);
    options["Pawn Structure (Endgame)"]                  = OptionValue(100, 0, 200);
    options["Passed Pawns (Middle Game)"]                = OptionValue(100, 0, 200);
    options["Passed Pawns (Endgame)"]                    = OptionValue(100, 0, 200);
    options["Aggressiveness"]                            = OptionValue(100, 0, 200);
    options["Cowardice"]                                 = OptionValue(100, 0, 200);
    options["King Safety Curve"]                         = OptionValue("Quadratic", COMBO);

       options["King Safety Curve"].comboValues.push_back("Quadratic");
       options["King Safety Curve"].comboValues.push_back("Linear");  /*, "From File"*/

    options["King Safety Coefficient"]                   = OptionValue(40, 1, 100);
    options["King Safety X Intercept"]                   = OptionValue(0, 0, 20);
    options["King Safety Max Slope"]                     = OptionValue(30, 10, 100);
    options["King Safety Max Value"]                     = OptionValue(500, 100, 1000);
    options["Queen Contact Check Bonus"]                 = OptionValue(4, 0, 8);
    options["Rook Contact Check Bonus"]                  = OptionValue(2, 0, 4);
    options["Queen Check Bonus"]                         = OptionValue(2, 0, 4);
    options["Rook Check Bonus"]                          = OptionValue(1, 0, 4);
    options["Bishop Check Bonus"]                        = OptionValue(1, 0, 4);
    options["Knight Check Bonus"]                        = OptionValue(1, 0, 4);
    options["Discovered Check Bonus"]                    = OptionValue(3, 0, 8);
    options["Mate Threat Bonus"]                         = OptionValue(3, 0, 8);

    options["Check Extension (PV nodes)"]                = OptionValue(2, 0, 2);
    options["Check Extension (non-PV nodes)"]            = OptionValue(1, 0, 2);
    options["Single Reply Extension (PV nodes)"]         = OptionValue(2, 0, 2);
    options["Single Reply Extension (non-PV nodes)"]     = OptionValue(2, 0, 2);
    options["Mate Threat Extension (PV nodes)"]          = OptionValue(2, 0, 2);
    options["Mate Threat Extension (non-PV nodes)"]      = OptionValue(0, 0, 2);
    options["Pawn Push to 7th Extension (PV nodes)"]     = OptionValue(1, 0, 2);
    options["Pawn Push to 7th Extension (non-PV nodes)"] = OptionValue(1, 0, 2);
    options["Passed Pawn Extension (PV nodes)"]          = OptionValue(1, 0, 2);
    options["Passed Pawn Extension (non-PV nodes)"]      = OptionValue(0, 0, 2);
    options["Pawn Endgame Extension (PV nodes)"]         = OptionValue(2, 0, 2);
    options["Pawn Endgame Extension (non-PV nodes)"]     = OptionValue(2, 0, 2);
    options["Full Depth Moves (PV nodes)"]               = OptionValue(14, 1, 100);
    options["Full Depth Moves (non-PV nodes)"]           = OptionValue(3, 1, 100);
    options["Threat Depth"]                              = OptionValue(5, 0, 100);
    options["Selective Plies"]                           = OptionValue(7, 0, 10);
    options["Futility Pruning (Main Search)"]            = OptionValue(true);
    options["Futility Pruning (Quiescence Search)"]      = OptionValue(true);
    options["Futility Margin 0"]                         = OptionValue(50, 0, 1000);
    options["Futility Margin 1"]                         = OptionValue(100, 0, 1000);
    options["Futility Margin 2"]                         = OptionValue(300, 0, 1000);
    options["Maximum Razoring Depth"]                    = OptionValue(3, 0, 4);
    options["Razoring Margin"]                           = OptionValue(300, 150, 600);
    options["Randomness"]                                = OptionValue(0, 0, 10);
    options["Minimum Split Depth"]                       = OptionValue(4, 4, 7);
    options["Maximum Number of Threads per Split Point"] = OptionValue(5, 4, 8);
    options["Threads"]                                   = OptionValue(1, 1, 8);
    options["Hash"]                                      = OptionValue(32, 4, 4096);
    options["Clear Hash"]                                = OptionValue(false);
    options["Ponder"]                                    = OptionValue(true);
    options["OwnBook"]                                   = OptionValue(true);
    options["MultiPV"]                                   = OptionValue(1, 1, 500);
    options["UCI_ShowCurrLine"]                          = OptionValue(false);
    options["UCI_Chess960"]                              = OptionValue(false);
  }


  ///
  /// Variables
  ///

  Options options;

  // Local functions
  template<typename T> T get_option_value(const std::string& optionName);

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
  assert(options.find("Threads") != options.end());

  options["Threads"].defaultValue = Min(cpu_count(), 7);
  options["Threads"].currentValue = Min(cpu_count(), 7);

  // Increase the minimum split depth when the number of CPUs is big.
  // It would probably be better to let this depend on the number of threads
  // instead.
  if(cpu_count() > 4)
  {
      assert(options.find("Minimum Split Depth") != options.end());

      options["Minimum Split Depth"].defaultValue = 6;
      options["Minimum Split Depth"].currentValue = 6;
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
      const OptionValue& o = it->second;

      std::cout << "option name " << it->first
                << " type "       << optionTypeName[o.type];

      if (o.type != BUTTON)
      {
          std::cout << " default " << o.defaultValue;

          if (o.type == SPIN)
              std::cout << " min " << o.minValue
                        << " max " << o.maxValue;

          else if (o.type == COMBO)
              for(ComboValues::size_type i = 0; i < o.comboValues.size(); ++i)
                  std::cout << " var " << o.comboValues[i];
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

  if (options.find(optionName) != options.end())
      options[optionName].currentValue = newValue;
  else
      std::cout << "No such option: " << optionName << std::endl;
}


/// push_button() is used to tell the engine that a UCI parameter of type
/// "button" has been selected:

void push_button(const std::string& buttonName) {

  set_option_value(buttonName, "true");
}


namespace {

    // methods and c'tors of OptionValue class.

    template<typename T>
    std::string OptionValue::stringify(const T& v)
    {
        std::ostringstream ss;
        ss << v;

        return ss.str();
    }

    OptionValue::OptionValue() {}

    OptionValue::OptionValue(const char* def, OptionType t)
    : defaultValue(def), currentValue(def), type(t), minValue(0), maxValue(0) {}

    OptionValue::OptionValue(bool def)
    : defaultValue(stringify(def)), currentValue(stringify(def)), type(CHECK), minValue(0), maxValue(0) {}

    OptionValue::OptionValue(int def, int minv, int maxv)
    : defaultValue(stringify(def)), currentValue(stringify(def)), type(SPIN), minValue(minv), maxValue(maxv) {}

    // get_option_value is the implementation of the various
    // get_option_value_<type>, because only the option value
    // type changes a template is the proper solution.

    template<typename T>
    T get_option_value(const std::string& optionName) {

        T ret;

        if (options.find(optionName) != options.end())
        {
            std::istringstream ss(options[optionName].currentValue);
            ss >> ret;
        }
        return ret;
    }

}
