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


#if !defined(UCIOPTION_H_INCLUDED)
#define UCIOPTION_H_INCLUDED

////
//// Includes
////

#include <string>


////
//// Variables
////

extern bool Chess960;


////
//// Prototypes
////

extern void init_uci_options();
extern void print_uci_options();
extern bool get_option_value_bool(const std::string &optionName);
extern int get_option_value_int(const std::string &optionName);
extern const std::string get_option_value_string(const std::string &optionName);
extern bool button_was_pressed(const std::string &buttonName);
extern void set_option_value(const std::string &optionName, 
                             const std::string &newValue);
extern void push_button(const std::string &buttonName);


#endif // !defined(UCIOPTION_H_INCLUDED)
