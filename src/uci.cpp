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


////
//// Local definitions:
////

namespace {

  // UCIInputParser is a class for parsing UCI input.  The class
  // is actually a string stream built on a given input string.

  typedef std::istringstream UCIInputParser;

  // The root position.  This is set up when the user (or in practice, the GUI)
  // sends the "position" UCI command.  The root position is sent to the think()
  // function when the program receives the "go" command.
  Position RootPosition;

  // Local functions
  void get_command();
  void handle_command(const std::string &command);
  void set_option(UCIInputParser &uip);
  void set_position(UCIInputParser &uip);
  void go(UCIInputParser &uip);
}


////
//// Functions
////

/// uci_main_loop() is the only global function in this file.  It is
/// called immediately after the program has finished initializing.
/// The program remains in this loop until it receives the "quit" UCI
/// command.

void uci_main_loop() {

  RootPosition.from_fen(StartPosition);

  while (1)
      get_command();
}


////
//// Local functions
////

namespace {

  // get_command() waits for a command from the user, and passes
  // this command to handle_command.  get_command also intercepts
  // EOF from stdin, by translating EOF to the "quit" command.  This
  // ensures that Stockfish exits gracefully if the GUI dies
  // unexpectedly.

  void get_command() {

    std::string command;

    if (!std::getline(std::cin, command))
        command = "quit";

    handle_command(command);
  }


  // handle_command() takes a text string as input, uses a
  // UCIInputParser object to parse this text string as a UCI command,
  // and calls the appropriate functions.  In addition to the UCI
  // commands, the function also supports a few debug commands.

  void handle_command(const std::string &command) {

    UCIInputParser uip(command);
    std::string token;

    uip >> token; // operator >> skips any whitespace

    if (token == "quit")
    {
        OpeningBook.close();
        stop_threads();
        quit_eval();
        exit(0);
    }
    else if (token == "uci")
    {
        std::cout << "id name " << engine_name() << std::endl
                  << "id author Tord Romstad, Marco Costalba"
                  << std::endl;
        print_uci_options();
        std::cout << "uciok" << std::endl;
    }
    else if (token == "ucinewgame")
    {
        TT.clear();
        Position::init_piece_square_tables();
        RootPosition.from_fen(StartPosition);
    }
    else if (token == "isready")
        std::cout << "readyok" << std::endl;
    else if (token == "position")
        set_position(uip);
    else if (token == "setoption")
        set_option(uip);
    else if (token == "go")
        go(uip);

    // The remaining commands are for debugging purposes only.
    // Perhaps they should be removed later in order to reduce the
    // size of the program binary.
    else if (token == "d")
        RootPosition.print();
    else if (token == "flip")
    {
        Position p(RootPosition);
        RootPosition.flipped_copy(p);
    }
    else if (token == "eval")
    {
        EvalInfo ei;
        std::cout << "Incremental mg: " << RootPosition.mg_value()
                  << std::endl;
        std::cout << "Incremental eg: " << RootPosition.eg_value()
                  << std::endl;
        std::cout << "Full eval: "
                  << evaluate(RootPosition, ei, 0)
                  << std::endl;
    }
    else if (token == "key")
    {
        std::cout << "key: " << RootPosition.get_key()
                  << " material key: " << RootPosition.get_material_key()
                  << " pawn key: " << RootPosition.get_pawn_key()
                  << std::endl;
    }
    else
    {
        std::cout << "Unknown command: " << command << std::endl;
        while (!uip.eof())
        {
            uip >> token;
            std::cout << token << std::endl;
        }
    }
  }


  // set_position() is called when Stockfish receives the "position" UCI
  // command.  The input parameter is a UCIInputParser.  It is assumed
  // that this parser has consumed the first token of the UCI command
  // ("position"), and is ready to read the second token ("startpos"
  // or "fen", if the input is well-formed).

  void set_position(UCIInputParser &uip) {

    std::string token;

    uip >> token; // operator >> skips any whitespace

    if (token == "startpos")
        RootPosition.from_fen(StartPosition);
    else if (token == "fen")
    {
        std::string fen;
        while (token != "moves" && !uip.eof())
        {
          uip >> token;
          fen += token;
          fen += ' ';
        }
        RootPosition.from_fen(fen);
    }

    if (!uip.eof())
    {
        if (token != "moves")
          uip >> token;
        if (token == "moves")
        {
            Move move;
            UndoInfo u;
            while (!uip.eof())
            {
                uip >> token;
                move = move_from_string(RootPosition, token);
                RootPosition.do_move(move, u);
                if (RootPosition.rule_50_counter() == 0)
                    RootPosition.reset_game_ply();
            }
        }
    }
  }


  // set_option() is called when Stockfish receives the "setoption" UCI
  // command.  The input parameter is a UCIInputParser.  It is assumed
  // that this parser has consumed the first token of the UCI command
  // ("setoption"), and is ready to read the second token ("name", if
  // the input is well-formed).

  void set_option(UCIInputParser &uip) {

    std::string token, name;

    uip >> token;
    if (token == "name")
    {
        uip >> name;
        uip >> token;
        while (!uip.eof() && token != "value")
        {
          name += (" " + token);
          uip >> token;
        }
        if (token == "value")
        {
            std::getline(uip, token); // reads until end of line
            set_option_value(name, token);
        } else
            push_button(name);
    }
  }


  // go() is called when Stockfish receives the "go" UCI command.  The
  // input parameter is a UCIInputParser.  It is assumed that this
  // parser has consumed the first token of the UCI command ("go"),
  // and is ready to read the second token.  The function sets the
  // thinking time and other parameters from the input string, and
  // calls think() (defined in search.cpp) with the appropriate
  // parameters.

  void go(UCIInputParser &uip) {

    std::string token;

    int time[2] = {0, 0}, inc[2] = {0, 0};
    int movesToGo = 0, depth = 0, nodes = 0, moveTime = 0;
    bool infinite = false, ponder = false;
    Move searchMoves[500];

    searchMoves[0] = MOVE_NONE;

    while (!uip.eof())
    {
        uip >> token;

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
            while (!uip.eof())
            {
                uip >> token;
                searchMoves[numOfMoves++] = move_from_string(RootPosition, token);
            }
            searchMoves[numOfMoves] = MOVE_NONE;
        }
    }

    if (moveTime)
        infinite = true;  // HACK

    think(RootPosition, infinite, ponder, RootPosition.side_to_move(), time,
          inc, movesToGo, depth, nodes, moveTime, searchMoves);
  }
}
