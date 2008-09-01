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
#include <cstdarg>
#include <cstdio>

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

  enum OptionType { SPIN, COMBO, CHECK, STRING, BUTTON, OPTION_TYPE_NONE};

  struct Option {
    char name[50], defaultValue[300], currentValue[300];
    OptionType type;
    int minValue, maxValue;
    char comboValues[8][64];
  };
    

  ///
  /// Variables
  ///

  Option Options[] = {
    { "Use Search Log", "false", "false", CHECK, 0, 0, {""} },
    { "Search Log Filename", "SearchLog.txt", "SearchLog.txt", STRING, 0, 0, {""} },
    { "Book File", "book.bin", "book.bin", STRING, 0, 0, {""} },
    { "Mobility (Middle Game)", "100", "100", SPIN, 0, 200, {""} },
    { "Mobility (Endgame)", "100", "100", SPIN, 0, 200, {""} },
    { "Pawn Structure (Middle Game)", "100", "100", SPIN, 0, 200, {""} },
    { "Pawn Structure (Endgame)", "100", "100", SPIN, 0, 200, {""} },
    { "Passed Pawns (Middle Game)", "100", "100", SPIN, 0, 200, {""} },
    { "Passed Pawns (Endgame)", "100", "100", SPIN, 0, 200, {""} },
    { "Aggressiveness", "100", "100", SPIN, 0, 200, {""} },
    { "Cowardice", "100", "100", SPIN, 0, 200, {""} },
    { "King Safety Curve", "Quadratic", "Quadratic", COMBO, 0, 0,
      { "Quadratic", "Linear" /*, "From File"*/ } },
    { "King Safety Coefficient", "40", "40", SPIN, 1, 100 , {""} },
    { "King Safety X Intercept", "0", "0", SPIN, 0, 20, {""} },
    { "King Safety Max Slope", "30", "30", SPIN, 10, 100, {""} },
    { "King Safety Max Value", "500", "500", SPIN, 100, 1000, {""} },
    { "Queen Contact Check Bonus", "4", "4", SPIN, 0, 8, {""} },
    { "Rook Contact Check Bonus", "2", "2", SPIN, 0, 4, {""} },
    { "Queen Check Bonus", "2", "2", SPIN, 0, 4, {""} },
    { "Rook Check Bonus", "1", "1", SPIN, 0, 4, {""} },
    { "Bishop Check Bonus", "1", "1", SPIN, 0, 4, {""} },
    { "Knight Check Bonus", "1", "1", SPIN, 0, 4, {""} },
    { "Discovered Check Bonus", "3", "3", SPIN, 0, 8, {""} },
    { "Mate Threat Bonus", "3", "3", SPIN, 0, 8, {""} },
    { "Check Extension (PV nodes)", "2", "2", SPIN, 0, 2, {""} },
    { "Check Extension (non-PV nodes)", "1", "1", SPIN, 0, 2, {""} },
    { "Single Reply Extension (PV nodes)", "2", "2", SPIN, 0, 2, {""} },
    { "Single Reply Extension (non-PV nodes)", "2", "2", SPIN, 0, 2, {""} },
    { "Mate Threat Extension (PV nodes)", "0", "0", SPIN, 0, 2, {""} },
    { "Mate Threat Extension (non-PV nodes)", "0", "0", SPIN, 0, 2, {""} },
    { "Pawn Push to 7th Extension (PV nodes)", "1", "1", SPIN, 0, 2, {""} },
    { "Pawn Push to 7th Extension (non-PV nodes)", "1", "1", SPIN, 0, 2, {""} },
    { "Passed Pawn Extension (PV nodes)", "1", "1", SPIN, 0, 2, {""} },
    { "Passed Pawn Extension (non-PV nodes)", "0", "0", SPIN, 0, 2, {""} },
    { "Pawn Endgame Extension (PV nodes)", "2", "2", SPIN, 0, 2, {""} },
    { "Pawn Endgame Extension (non-PV nodes)", "2", "2", SPIN, 0, 2, {""} },
    { "Full Depth Moves (PV nodes)", "14", "14", SPIN, 1, 100, {""} },
    { "Full Depth Moves (non-PV nodes)", "3", "3", SPIN, 1, 100, {""} },
    { "Threat Depth", "5", "5", SPIN, 0, 100, {""} },
    { "Selective Plies", "7", "7", SPIN, 0, 10, {""} },
    { "Futility Pruning (Main Search)", "true", "true", CHECK, 0, 0, {""} },
    { "Futility Pruning (Quiescence Search)", "true", "true", CHECK, 0, 0, {""} },
    { "Futility Margin 0", "50", "50", SPIN, 0, 1000, {""} },
    { "Futility Margin 1", "100", "100", SPIN, 0, 1000, {""} },
    { "Futility Margin 2", "300", "300", SPIN, 0, 1000, {""} },
    { "Maximum Razoring Depth", "3", "3", SPIN, 0, 4, {""} },
    { "Razoring Margin", "300", "300", SPIN, 150, 600, {""} },
    { "Randomness", "0", "0", SPIN, 0, 10, {""} },
    { "Minimum Split Depth", "4", "4", SPIN, 4, 7, {""} },
    { "Maximum Number of Threads per Split Point", "5", "5", SPIN, 4, 8, {""} },
    { "Threads", "1", "1", SPIN, 1, 8, {""} },
    { "Hash", "32", "32", SPIN, 4, 4096, {""} },
    { "Clear Hash", "false", "false", BUTTON, 0, 0, {""} },
    { "Ponder", "true", "true", CHECK, 0, 0, {""} },
    { "OwnBook", "true", "true", CHECK, 0, 0, {""} },
    { "MultiPV", "1", "1", SPIN, 1, 500, {""} },
    { "UCI_ShowCurrLine", "false", "false", CHECK, 0, 0, {""} },
    { "UCI_Chess960", "false", "false", CHECK, 0, 0, {""} },
    { "", "", "", OPTION_TYPE_NONE, 0, 0, {""}}
  };
    

  ///
  /// Functions
  ///

  Option *option_with_name(const char *optionName);

}


////
//// Functions
////

/// init_uci_options() initializes the UCI options.  Currently, the only
/// thing this function does is to initialize the default value of the
/// "Threads" parameter to the number of available CPU cores.

void init_uci_options() {
  Option *o;

  o = option_with_name("Threads");
  assert(o != NULL);

  // Limit the default value of "Threads" to 7 even if we have 8 CPU cores.
  // According to Ken Dail's tests, Glaurung plays much better with 7 than
  // with 8 threads.  This is weird, but it is probably difficult to find out
  // why before I have a 8-core computer to experiment with myself.
  sprintf(o->defaultValue, "%d", Min(cpu_count(), 7));
  sprintf(o->currentValue, "%d", Min(cpu_count(), 7));

  // Increase the minimum split depth when the number of CPUs is big.
  // It would probably be better to let this depend on the number of threads
  // instead.
  o = option_with_name("Minimum Split Depth");
  assert(o != NULL);
  if(cpu_count() > 4) {
    sprintf(o->defaultValue, "%d", 6);
    sprintf(o->defaultValue, "%d", 6);
  }
}


/// print_uci_options() prints all the UCI options to the standard output,
/// in the format defined by the UCI protocol.

void print_uci_options() {
  static const char optionTypeName[][16] = {
    "spin", "combo", "check", "string", "button"
  };
  for(Option *o = Options; o->type != OPTION_TYPE_NONE; o++) {
    printf("option name %s type %s", o->name, optionTypeName[o->type]);
    if(o->type != BUTTON) {
      printf(" default %s", o->defaultValue);
      if(o->type == SPIN)
        printf(" min %d max %d", o->minValue, o->maxValue);
      else if(o->type == COMBO)
        for(int i = 0; strlen(o->comboValues[i]) > 0; i++)
          printf(" var %s", o->comboValues[i]);
    }
    printf("\n");
  }
}


/// get_option_value_bool() returns the current value of a UCI parameter of
/// type "check".

bool get_option_value_bool(const std::string &optionName) {
  Option *o = option_with_name(optionName.c_str());
  return o != NULL && strcmp(o->currentValue, "true") == 0;
}


/// get_option_value_int() returns the value of a UCI parameter as an integer.
/// Normally, this function will be used for a parameter of type "spin", but
/// it could also be used with a "combo" parameter, where all the available
/// values are integers.

int get_option_value_int(const std::string &optionName) {
  Option *o = option_with_name(optionName.c_str());
  return atoi(o->currentValue);
}


/// get_option_value_string() returns the current value of a UCI parameter as
/// a string.  It is used with parameters of type "combo" and "string".

const std::string get_option_value_string(const std::string &optionName) {
  Option *o = option_with_name(optionName.c_str());
  return o->currentValue;
}


/// button_was_pressed() tests whether a UCI parameter of type "button" has
/// been selected since the last time the function was called.

bool button_was_pressed(const std::string &buttonName) {
  if(get_option_value_bool(buttonName)) {
    set_option_value(buttonName, "false");
    return true;
  }
  else
    return false;
}


/// set_option_value() inserts a new value for a UCI parameter.  Note that
/// the function does not check that the new value is legal for the given
/// parameter:  This is assumed to be the responsibility of the GUI.

void set_option_value(const std::string &optionName,
                      const std::string &newValue) {
  Option *o = option_with_name(optionName.c_str());

  if(o != NULL)
    strcpy(o->currentValue, newValue.c_str());
  else
    std::cout << "No such option: " << optionName << std::endl;
}


/// push_button() is used to tell the engine that a UCI parameter of type
/// "button" has been selected:

void push_button(const std::string &buttonName) {
  set_option_value(buttonName, "true");
}


namespace {

  // option_with_name() tries to find a UCI option with a given
  // name.  It returns a pointer to the UCI option or the null pointer,
  // depending on whether an option with the given name exists.
  
  Option *option_with_name(const char *optionName) {
    for(Option *o = Options; o->type != OPTION_TYPE_NONE; o++)
      if(strcmp(o->name, optionName) == 0)
        return o;
    return NULL;
  }

}
