/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2024 The Stockfish developers (see AUTHORS file)

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

#include <iostream>
#include <string>

#include "misc.h"
#include "nnue/network.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"

namespace Stockfish {

class Move;
enum Square : int;
using Value = int;

class UCI {
   public:
    UCI(int argc, char** argv);

    void loop();

    static int         to_cp(Value v, const Position& pos);
    static std::string to_score(Value v, const Position& pos);
    static std::string square(Square s);
    static std::string move(Move m, bool chess960);
    static std::string wdl(Value v, const Position& pos);
    static Move        to_move(const Position& pos, std::string& str);

    static Search::LimitsType parse_limits(const Position& pos, std::istream& is);

    const std::string& working_directory() const { return cli.workingDirectory; }

    OptionsMap           options;
    Eval::NNUE::Networks networks;

   private:
    TranspositionTable tt;
    ThreadPool         threads;
    CommandLine        cli;

    void go(Position& pos, std::istringstream& is, StateListPtr& states);
    void bench(Position& pos, std::istream& args, StateListPtr& states);
    void position(Position& pos, std::istringstream& is, StateListPtr& states);
    void trace_eval(Position& pos);
    void search_clear();
    void setoption(std::istringstream& is);
};

}  // namespace Stockfish

#endif  // #ifndef UCI_H_INCLUDED
