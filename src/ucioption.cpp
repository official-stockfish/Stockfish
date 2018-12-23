/*
 McCain, a UCI chess playing engine derived from Stockfish and Glaurung 2.1
 Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
 Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish Authors)
 Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (Stockfish Authors)
 Copyright (C) 2017-2019 Michael Byrne, Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (McCain Authors)

 McCain is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 McCain is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <cassert>
#include <ostream>

#include "misc.h"
#ifdef Add_Features
#include "polybook.h"
#endif
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

using std::string;

UCI::OptionsMap Options; // Global object

namespace UCI {

/// 'On change' actions, triggered by an option's value change
void on_clear_hash(const Option&) {    Search::clear();}
void on_hash_size(const Option& o) {    TT.resize(o);}
void on_logger(const Option& o) {    start_logger(o);}
void on_threads(const Option& o) {    Threads.set(o);}
void on_tb_path(const Option& o) {    Tablebases::init(o);}
#ifdef Add_Features
void on_book_file(const Option& o) {    polybook.init(o);}
void on_best_book_move(const Option& o) {    polybook.set_best_book_move(o);}
void on_book_depth(const Option& o) {    polybook.set_book_depth(o);}
#endif

/// Our case insensitive less() function as required by UCI protocol
bool CaseInsensitiveLess::operator() (const string& s1, const string& s2) const {

  return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
   [](char c1, char c2) { return tolower(c1) < tolower(c2); });
}


/// init() initializes the UCI options to their hard-coded default values

void init(OptionsMap& o) {

    // at most 2^32 clusters.
    constexpr int MaxHashMB = Is64Bit ? 131072 : 2048;
    o["Debug Log File"]			<< Option("<empty>", on_logger);
#ifdef Maverick
    o["Contempt"]				<< Option(12, -300, 300);
#else
    o["Contempt"]				<< Option(24, -100, 100);
#endif
#ifdef Add_Features
    o["Analysis Contempt"]		<< Option("var Off var White var Black var Both", "Off");
#else
    o["Analysis Contempt"]		<< Option("Both var Off var White var Black var Both", "Both");
#endif
    o["Threads"]				<< Option(1, 1, 512, on_threads);
    o["Hash"]					<< Option(16, 1, MaxHashMB, on_hash_size);
    o["Clear Hash"]				<< Option(on_clear_hash);
	o["Clean_Search"]			<< Option(false);
    o["Ponder"]					<< Option(false);
#ifdef Add_Features
	o["7 Man Probing"]			<< Option(false);
	o["BruteForce"]				<< Option(false);
	o["Black Defensive"]		<< Option(true);
	o["White Defensive"]		<< Option(false);
    o["Dynamic_Contempt"]		<< Option(true);
    o["FastPlay"]				<< Option(false);
    o["Minimal_Output"]			<< Option(true);
    o["No_Null_Moves"]			<< Option(false);
	o["UCI_LimitStrength"]		<< Option(false);
	o["Levels"]			        << Option("var None var World Champion var Super GM "
										  "var Deep Thought var Grandmaster var Cray Blitz"
										  "var Int'l Master var Master var Expert var Class A "
										  "var Class B var Class C var Class D var Challenger 7 "
										  "var Boris var Novice", "None");
	o["UCI_ELO"]				<< Option(1500, 1200, 2800);
	o["Defensive Focus"]		<< Option( 3, 1, 4 );
#endif
#ifdef Maverick
	o["MultiPV"]				<< Option(1, 1, 256);
#else
	o["MultiPV"]				<< Option(1, 1, 500);
#endif
#ifdef Pi
	o["Bench_KNPS"]				<< Option (200, 100, 1000);//used for UCI Strength
#else
	o["Bench_KNPS"]				<< Option (1500, 500, 5000);//used for UCI Strength
#endif
	o["Tactical"]				<< Option(0, 0,  8);
	o["Variety"]				<< Option(0, 0, 20);
    o["Skill Level"]			<< Option(20, 0, 20);
    o["Move Overhead"]			<< Option(30, 0, 5000);
    o["Minimum Thinking Time"]	<< Option(20, 0, 5000);
#ifdef Add_Features
    o["Slow Mover"]				<< Option(100, 10, 1000);
#else
	o["Slow Mover"]				<< Option(84, 10, 1000);
#endif
    o["nodestime"]				<< Option(0, 0, 10000);
    o["UCI_Chess960"]			<< Option(false);
    o["UCI_AnalyseMode"]		<< Option(false);
	
    o["SyzygyPath"]				<< Option("<empty>", on_tb_path);
    o["SyzygyProbeDepth"]		<< Option(1, 1, 100);
    o["Syzygy50MoveRule"]		<< Option(true);
    o["SyzygyProbeLimit"]		<< Option(7, 0, 7);
#ifdef Add_Features
    o["Book_Enabled"]			<< Option(true);
	o["BookFile"]				<< Option("var None var Cerebellum var Champions "
										  "var Alekhine var Anand var Botvinnik "
										  "var Capablanca var Carlsen var Fischer "
										  "var Karpov var Kasparov var Kortschnoi "
										  "var Kramink var Lasker var Petrosian "
										  "var Tal ","None",on_book_file);
    o["BestBookMove"]			<< Option(true, on_best_book_move);
    o["BookDepth"]				<< Option(255, 1, 255, on_book_depth);
#endif
}


/// operator<<() is used to print all the options default values in chronological
/// insertion order (the idx field) and in the format defined by the UCI protocol.

std::ostream& operator<<(std::ostream& os, const OptionsMap& om) {

  for (size_t idx = 0; idx < om.size(); ++idx)
      for (const auto& it : om)
          if (it.second.idx == idx)
          {
              const Option& o = it.second;
              os << "\noption name " << it.first << " type " << o.type;

              if (o.type == "string" || o.type == "check" || o.type == "combo")
                  os << " default " << o.defaultValue;

              if (o.type == "spin")
                  os << " default " << int(stof(o.defaultValue))
                     << " min "     << o.min
                     << " max "     << o.max;

              break;
          }

  return os;
}


/// Option class constructors and conversion operators

Option::Option(const char* v, OnChange f) : type("string"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = v; }

Option::Option(bool v, OnChange f) : type("check"), min(0), max(0), on_change(f)
{ defaultValue = currentValue = (v ? "true" : "false"); }

Option::Option(OnChange f) : type("button"), min(0), max(0), on_change(f)
{}

Option::Option(double v, int minv, int maxv, OnChange f) : type("spin"), min(minv), max(maxv), on_change(f)
{ defaultValue = currentValue = std::to_string(v); }

Option::Option(const char* v, const char* cur, OnChange f) : type("combo"), min(0), max(0), on_change(f)
{ defaultValue = v; currentValue = cur; }

Option::operator double() const {
  assert(type == "check" || type == "spin");
  return (type == "spin" ? stof(currentValue) : currentValue == "true");
}

Option::operator std::string() const {
  assert(type == "string");
  return currentValue;
}

bool Option::operator==(const char* s) const {
  assert(type == "combo");
  return    !CaseInsensitiveLess()(currentValue, s)
         && !CaseInsensitiveLess()(s, currentValue);
}


/// operator<<() inits options and assigns idx in the correct printing order

void Option::operator<<(const Option& o) {

  static size_t insert_order = 0;

  *this = o;
  idx = insert_order++;
}


/// operator=() updates currentValue and triggers on_change() action. It's up to
/// the GUI to check for option's limits, but we could receive the new value from
/// the user by console window, so let's check the bounds anyway.

Option& Option::operator=(const string& v) {

  assert(!type.empty());

  if (   (type != "button" && v.empty())
      || (type == "check" && v != "true" && v != "false")
      || (type == "spin" && (stof(v) < min || stof(v) > max)))
      return *this;

  if (type != "button")
      currentValue = v;

  if (on_change)
      on_change(*this);

  return *this;
}

} // namespace UCI
