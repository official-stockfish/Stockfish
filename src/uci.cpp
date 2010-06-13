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

#include "book.h"
#include "evaluate.h"
#include "misc.h"
#include "move.h"
#include "movegen.h"
#include "position.h"
#include "san.h"
#include "search.h"
#include "uci.h"
#include "ucioption.h"

using namespace std;

////
//// Local definitions:
////

namespace {

  // UCIInputParser is a class for parsing UCI input. The class
  // is actually a string stream built on a given input string.

  typedef istringstream UCIInputParser;

  // The root position. This is set up when the user (or in practice, the GUI)
  // sends the "position" UCI command. The root position is sent to the think()
  // function when the program receives the "go" command.
  Position RootPosition(0);

  // Local functions
  bool handle_command(const string& command);
  void set_option(UCIInputParser& uip);
  void set_position(UCIInputParser& uip);
  bool go(UCIInputParser& uip);
  void perft(UCIInputParser& uip);
}


////
//// Functions
////

/// uci_main_loop() is the only global function in this file. It is
/// called immediately after the program has finished initializing.
/// The program remains in this loop until it receives the "quit" UCI
/// command. It waits for a command from the user, and passes this
/// command to handle_command and also intercepts EOF from stdin,
/// by translating EOF to the "quit" command. This ensures that Stockfish
/// exits gracefully if the GUI dies unexpectedly.

void uci_main_loop() {

  RootPosition.from_fen(StartPosition);
  string command;

  do {
      // Wait for a command from stdin
      if (!getline(cin, command))
          command = "quit";

  } while (handle_command(command));
}


////
//// Local functions
////

namespace {

  // handle_command() takes a text string as input, uses a
  // UCIInputParser object to parse this text string as a UCI command,
  // and calls the appropriate functions. In addition to the UCI
  // commands, the function also supports a few debug commands.

  bool handle_command(const string& command) {

    UCIInputParser uip(command);
    string token;

    if (!(uip >> token)) // operator>>() skips any whitespace
        return true;

    if (token == "quit")
        return false;

    if (token == "go")
        return go(uip);

    if (token == "uci")
    {
        cout << "id name " << engine_name()
             << "\nid author Tord Romstad, Marco Costalba, Joona Kiiski\n";
        print_uci_options();
        cout << "uciok" << endl;
    }
    else if (token == "ucinewgame")
    {
        push_button("New Game");
        Position::init_piece_square_tables();
        RootPosition.from_fen(StartPosition);
    }
    else if (token == "isready")
        cout << "readyok" << endl;
    else if (token == "position")
        set_position(uip);
    else if (token == "setoption")
        set_option(uip);

    // The remaining commands are for debugging purposes only.
    // Perhaps they should be removed later in order to reduce the
    // size of the program binary.
    else if (token == "d")
        RootPosition.print();
    else if (token == "flip")
    {
        Position p(RootPosition, RootPosition.thread());
        RootPosition.flipped_copy(p);
    }
    else if (token == "eval")
    {
        EvalInfo ei;
        cout << "Incremental mg: " << mg_value(RootPosition.value())
             << "\nIncremental eg: " << eg_value(RootPosition.value())
             << "\nFull eval: " << evaluate(RootPosition, ei) << endl;
    }
    else if (token == "key")
        cout << "key: " << hex << RootPosition.get_key()
             << "\nmaterial key: " << RootPosition.get_material_key()
             << "\npawn key: " << RootPosition.get_pawn_key() << endl;
    else if (token == "perft")
        perft(uip);
    else
        cout << "Unknown command: " << command << endl;

    return true;
  }


  // set_position() is called when Stockfish receives the "position" UCI
  // command. The input parameter is a UCIInputParser. It is assumed
  // that this parser has consumed the first token of the UCI command
  // ("position"), and is ready to read the second token ("startpos"
  // or "fen", if the input is well-formed).

  void set_position(UCIInputParser& uip) {

    string token;

    if (!(uip >> token)) // operator>>() skips any whitespace
        return;

    if (token == "startpos")
        RootPosition.from_fen(StartPosition);
    else if (token == "fen")
    {
        string fen;
        while (uip >> token && token != "moves")
        {
            fen += token;
            fen += ' ';
        }
        RootPosition.from_fen(fen);
    }

    if (uip.good())
    {
        if (token != "moves")
          uip >> token;

        if (token == "moves")
        {
            Move move;
            StateInfo st;
            while (uip >> token)
            {
                move = move_from_string(RootPosition, token);
                RootPosition.do_move(move, st);
                if (RootPosition.rule_50_counter() == 0)
                    RootPosition.reset_game_ply();
            }
            // Our StateInfo st is about going out of scope so copy
            // its content inside RootPosition before they disappear.
            RootPosition.detach();
        }
    }
  }


  // set_option() is called when Stockfish receives the "setoption" UCI
  // command. The input parameter is a UCIInputParser. It is assumed
  // that this parser has consumed the first token of the UCI command
  // ("setoption"), and is ready to read the second token ("name", if
  // the input is well-formed).

  void set_option(UCIInputParser& uip) {

    string token, name, value;

    if (!(uip >> token)) // operator>>() skips any whitespace
        return;

    if (token == "name" && uip >> name)
    {
        while (uip >> token && token != "value")
            name += (" " + token);

        if (token == "value" && uip >> value)
        {
            while (uip >> token)
                value += (" " + token);

            set_option_value(name, value);
        } else
            push_button(name);
    }
  }


  // go() is called when Stockfish receives the "go" UCI command. The
  // input parameter is a UCIInputParser. It is assumed that this
  // parser has consumed the first token of the UCI command ("go"),
  // and is ready to read the second token. The function sets the
  // thinking time and other parameters from the input string, and
  // calls think() (defined in search.cpp) with the appropriate
  // parameters. Returns false if a quit command is received while
  // thinking, returns true otherwise.

  bool go(UCIInputParser& uip) {

    string token;

    int time[2] = {0, 0}, inc[2] = {0, 0};
    int movesToGo = 0, depth = 0, nodes = 0, moveTime = 0;
    bool infinite = false, ponder = false;
    Move searchMoves[500];

    searchMoves[0] = MOVE_NONE;

    while (uip >> token)
    {
        if (token == "infinite")
            infinite = true;
        else if (token == "ponder")
            ponder = true;
        else if (token == "wtime")
            uip >> time[0];
        else if (token == "btime")
            uip >> time[1];
        else if (token == "winc")
            uip >> inc[0];
        else if (token == "binc")
            uip >> inc[1];
        else if (token == "movestogo")
            uip >> movesToGo;
        else if (token == "depth")
            uip >> depth;
        else if (token == "nodes")
            uip >> nodes;
        else if (token == "movetime")
            uip >> moveTime;
        else if (token == "searchmoves")
        {
            int numOfMoves = 0;
            while (uip >> token)
                searchMoves[numOfMoves++] = move_from_string(RootPosition, token);

            searchMoves[numOfMoves] = MOVE_NONE;
        }
    }

    assert(RootPosition.is_ok());

    return think(RootPosition, infinite, ponder, RootPosition.side_to_move(),
                 time, inc, movesToGo, depth, nodes, moveTime, searchMoves);
  }

  void perft(UCIInputParser& uip) {

    string token;
    int depth, tm, n;
    Position pos(RootPosition, RootPosition.thread());

    if (!(uip >> depth))
        return;

    tm = get_system_time();

    n = perft(pos, depth * OnePly);

    tm = get_system_time() - tm;
    std::cout << "\nNodes " << n
              << "\nTime (ms) " << tm
              << "\nNodes/second " << (int)(n/(tm/1000.0)) << std::endl;
  }
}
