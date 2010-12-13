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


////
//// Includes
////

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

#include "evaluate.h"
#include "misc.h"
#include "move.h"
#include "movegen.h"
#include "position.h"
#include "san.h"
#include "search.h"
#include "ucioption.h"

using namespace std;


namespace {

  // FEN string for the initial position
  const string StartPositionFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  // UCIParser is a class for parsing UCI input. The class
  // is actually a string stream built on a given input string.
  typedef istringstream UCIParser;

  // Local functions
  void set_option(UCIParser& uip);
  void set_position(Position& pos, UCIParser& uip);
  bool go(Position& pos, UCIParser& uip);
  void perft(Position& pos, UCIParser& uip);
}


/// execute_uci_command() takes a string as input, uses a UCIParser
/// object to parse this text string as a UCI command, and calls
/// the appropriate functions. In addition to the UCI commands,
/// the function also supports a few debug commands.

bool execute_uci_command(const string& cmd) {

  static Position pos(StartPositionFEN, 0); // The root position
  UCIParser up(cmd);
  string token;

  if (!(up >> token)) // operator>>() skips any whitespace
      return true;

  if (token == "quit")
      return false;

  if (token == "go")
      return go(pos, up);

  if (token == "uci")
  {
      cout << "id name " << engine_name()
           << "\nid author Tord Romstad, Marco Costalba, Joona Kiiski\n";
      print_uci_options();
      cout << "uciok" << endl;
  }
  else if (token == "ucinewgame")
      pos.from_fen(StartPositionFEN);

  else if (token == "isready")
      cout << "readyok" << endl;

  else if (token == "position")
      set_position(pos, up);

  else if (token == "setoption")
      set_option(up);

  // The remaining commands are for debugging purposes only
  else if (token == "d")
      pos.print();

  else if (token == "flip")
  {
      Position p(pos, pos.thread());
      pos.flipped_copy(p);
  }
  else if (token == "eval")
  {
      Value evalMargin;
      cout << "Incremental mg: "   << mg_value(pos.value())
           << "\nIncremental eg: " << eg_value(pos.value())
           << "\nFull eval: "      << evaluate(pos, evalMargin) << endl;
  }
  else if (token == "key")
      cout << "key: " << hex << pos.get_key()
           << "\nmaterial key: " << pos.get_material_key()
           << "\npawn key: " << pos.get_pawn_key() << endl;

  else if (token == "perft")
      perft(pos, up);

  else
      cout << "Unknown command: " << cmd << endl;

  return true;
}


////
//// Local functions
////

namespace {

  // set_position() is called when Stockfish receives the "position" UCI
  // command. The input parameter is a UCIParser. It is assumed
  // that this parser has consumed the first token of the UCI command
  // ("position"), and is ready to read the second token ("startpos"
  // or "fen", if the input is well-formed).

  void set_position(Position& pos, UCIParser& up) {

    string token;

    if (!(up >> token) || (token != "startpos" && token != "fen"))
        return;

    if (token == "startpos")
    {
        pos.from_fen(StartPositionFEN);
        if (!(up >> token))
            return;
    }
    else // fen
    {
        string fen;
        while (up >> token && token != "moves")
        {
            fen += token;
            fen += ' ';
        }
        pos.from_fen(fen);
    }

    if (token != "moves")
        return;

    // Parse optional move list
    Move move;
    StateInfo st;
    while (up >> token)
    {
        move = move_from_uci(pos, token);
        pos.do_move(move, st);
        if (pos.rule_50_counter() == 0)
            pos.reset_game_ply();

        pos.inc_startpos_ply_counter(); //FIXME: make from_fen to support this and rule50
    }
    // Our StateInfo st is about going out of scope so copy
    // its content inside pos before it disappears.
    pos.detach();
  }


  // set_option() is called when Stockfish receives the "setoption" UCI
  // command. The input parameter is a UCIParser. It is assumed
  // that this parser has consumed the first token of the UCI command
  // ("setoption"), and is ready to read the second token ("name", if
  // the input is well-formed).

  void set_option(UCIParser& up) {

    string token, name, value;

    if (!(up >> token) || token != "name") // operator>>() skips any whitespace
        return;

    if (!(up >> name))
        return;

    // Handle names with included spaces
    while (up >> token && token != "value")
        name += (" " + token);

    if (Options.find(name) == Options.end())
    {
        cout << "No such option: " << name << endl;
        return;
    }

    // Is a button ?
    if (token != "value")
    {
        Options[name].set_value("true");
        return;
    }

    if (!(up >> value))
        return;

    // Handle values with included spaces
    while (up >> token)
        value += (" " + token);

    Options[name].set_value(value);
  }


  // go() is called when Stockfish receives the "go" UCI command. The
  // input parameter is a UCIParser. It is assumed that this
  // parser has consumed the first token of the UCI command ("go"),
  // and is ready to read the second token. The function sets the
  // thinking time and other parameters from the input string, and
  // calls think() (defined in search.cpp) with the appropriate
  // parameters. Returns false if a quit command is received while
  // thinking, returns true otherwise.

  bool go(Position& pos, UCIParser& up) {

    string token;

    int time[2] = {0, 0}, inc[2] = {0, 0};
    int movesToGo = 0, depth = 0, nodes = 0, moveTime = 0;
    bool infinite = false, ponder = false;
    Move searchMoves[MOVES_MAX];

    searchMoves[0] = MOVE_NONE;

    while (up >> token)
    {
        if (token == "infinite")
            infinite = true;
        else if (token == "ponder")
            ponder = true;
        else if (token == "wtime")
            up >> time[0];
        else if (token == "btime")
            up >> time[1];
        else if (token == "winc")
            up >> inc[0];
        else if (token == "binc")
            up >> inc[1];
        else if (token == "movestogo")
            up >> movesToGo;
        else if (token == "depth")
            up >> depth;
        else if (token == "nodes")
            up >> nodes;
        else if (token == "movetime")
            up >> moveTime;
        else if (token == "searchmoves")
        {
            int numOfMoves = 0;
            while (up >> token)
                searchMoves[numOfMoves++] = move_from_uci(pos, token);

            searchMoves[numOfMoves] = MOVE_NONE;
        }
    }

    assert(pos.is_ok());

    return think(pos, infinite, ponder, time, inc, movesToGo,
                 depth, nodes, moveTime, searchMoves);
  }

  void perft(Position& pos, UCIParser& up) {

    int depth, tm, n;

    if (!(up >> depth))
        return;

    tm = get_system_time();

    n = perft(pos, depth * ONE_PLY);

    tm = get_system_time() - tm;
    std::cout << "\nNodes " << n
              << "\nTime (ms) " << tm
              << "\nNodes/second " << int(n / (tm / 1000.0)) << std::endl;
  }
}
