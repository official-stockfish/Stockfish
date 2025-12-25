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

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "engine.h"
#include "misc.h"
#include "search.h"

namespace Stockfish {

class Position;
class Move;
class Score;
enum Square : int;
using Value = int;

class UCIEngine {
   public:
    UCIEngine(int argc, char** argv);

    void loop();

    static int         to_cp(Value v, const Position& pos);
    static std::string format_score(const Score& s);
    static std::string square(Square s);
    static std::string move(Move m, bool chess960);
    static std::string wdl(Value v, const Position& pos);
    static std::string to_lower(std::string str);
    static Move        to_move(const Position& pos, std::string str);

    static Search::LimitsType parse_limits(std::istream& is);

    auto& engine_options() { return engine.get_options(); }

   private:
    Engine      engine;
    CommandLine cli;

    static void print_info_string(const std::string& str);

    void          go(std::istringstream& is);
    void          bench(std::istream& args);
    void          position(std::istringstream& is);
    void          setoption(std::istringstream& is);
    std::uint64_t perft(const Search::LimitsType&);

    static void on_update_no_moves(const Engine::InfoShort& info);
    static void on_update_full(const Engine::InfoFull& info, bool showWDL);
    static void on_iter(const Engine::InfoIter& info);
    static void on_bestmove(std::string_view bestmove, std::string_view ponder);
};

}  // namespace Stockfish

#endif  // #ifndef UCI_H_INCLUDED
