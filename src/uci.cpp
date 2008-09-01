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

#include <iostream>
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

  // UCIInputParser is a class for parsing UCI input.  The class is
  // very simple, and basically just consist of a constant input
  // string and a current location in the string.  There are methods
  // for checking if we are at the end of the line, for getting the
  // next token (defined as any whitespace-delimited sequence of
  // characters), and for getting the rest of the line as a single
  // string.

  class UCIInputParser {

  public:
    UCIInputParser(const std::string &line);
    std::string get_next_token();
    std::string get_rest_of_line();
    bool at_end_of_line();

  private:
    const std::string &inputLine;
    int length, currentIndex;

    void skip_whitespace();

  };


  // The root position.  This is set up when the user (or in practice, the GUI)
  // sends the "position" UCI command.  The root position is sent to the think()
  // function when the program receives the "go" command.
  Position RootPosition;

  // Local functions
  void wait_for_command();
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
  while(1) wait_for_command();
}


////
//// Local functions
////

namespace {

  ///
  /// Implementation of the UCIInputParser class.
  ///

  // Constructor for the UCIInputParser class.  The constructor takes a 
  // text string containing a single UCI command as input.

  UCIInputParser::UCIInputParser(const std::string &line) : inputLine(line) {
    this->currentIndex = 0;
    this->length = line.length();
  }


  // UCIInputParser::skip_whitspace() skips any number of whitespace
  // characters from the current location in an input string.

  void UCIInputParser::skip_whitespace() {
    while(isspace((int)(unsigned char)this->inputLine[this->currentIndex]))
      this->currentIndex++;
  }


  // UCIInputParser::get_next_token() gets the next token in an UCI
  // command.  A 'token' in an UCI command is simply any
  // whitespace-delimited sequence of characters.

  std::string UCIInputParser::get_next_token() {
    int i, j;

    this->skip_whitespace();
    for(i = j = this->currentIndex; 
        j < this->length && !isspace(this->inputLine[j]); 
        j++);
    this->currentIndex = j;
    this->skip_whitespace();

    std::string str = this->inputLine.substr(i, j - i);

    return str;
  }


  // UCIInputParser::get_rest_of_line() returns the rest of the input
  // line (from the current location) as a single string.

  std::string UCIInputParser::get_rest_of_line() {
    this->skip_whitespace();
    return this->inputLine.substr(this->currentIndex, this->length);
  }


  // UCIInputParser::at_end_of_line() tests whether we have reached the
  // end of the input string, i.e. if any more input remains to be
  // parsed.

  bool UCIInputParser::at_end_of_line() {
    return this->currentIndex == this->length;
  }


  /// 
  /// Other functions
  ///


  // wait_for_command() waits for a command from the user, and passes
  // this command to handle_command.  wait_for_command also intercepts
  // EOF from stdin, by translating EOF to the "quit" command.  This
  // ensures that Glaurung exits gracefully if the GUI dies
  // unexpectedly.

  void wait_for_command() {
    std::string command;
    if(!std::getline(std::cin, command)) command = "quit";
    handle_command(command);
  }


  // handle_command() takes a text string as input, uses a
  // UCIInputParser object to parse this text string as a UCI command,
  // and calls the appropriate functions.  In addition to the UCI
  // commands, the function also supports a few debug commands.
  
  void handle_command(const std::string &command) {
    UCIInputParser uip(command);
    std::string s = uip.get_next_token();

    if(s == "quit") {
      OpeningBook.close();
      stop_threads();
      quit_eval();
      exit(0);
    }
    else if(s == "uci") {
      std::cout << "id name " << engine_name() << std::endl;
      std::cout << "id author Tord Romstad" << std::endl;
      print_uci_options();
      std::cout << "uciok" << std::endl;
    }
    else if(s == "ucinewgame") {
      TT.clear();
      Position::init_piece_square_tables();
      RootPosition.from_fen(StartPosition);
    }
    else if(s == "isready")
      std::cout << "readyok" << std::endl;
    else if(s == "position")
      set_position(uip);
    else if(s == "setoption")
      set_option(uip);
    else if(s == "go")
      go(uip);

    // The remaining commands are for debugging purposes only.
    // Perhaps they should be removed later in order to reduce the
    // size of the program binary.
    else if(s == "d")
      RootPosition.print();
    else if(s == "flip") {
      Position p(RootPosition);
      RootPosition.flipped_copy(p);
    }
    else if(s == "eval") {
      EvalInfo ei;
      std::cout << "Incremental mg: " << RootPosition.mg_value()
                << std::endl;
      std::cout << "Incremental eg: " << RootPosition.eg_value()
                << std::endl;
      std::cout << "Full eval: "
                << evaluate(RootPosition, ei, 0)
                << std::endl;
    }
    else if(s == "key") {
      std::cout << "key: " << RootPosition.get_key()
                << " material key: " << RootPosition.get_material_key()
                << " pawn key: " << RootPosition.get_pawn_key()
                << std::endl;
    }
    else {
      std::cout << "Unknown command: " << command << std::endl;
      while(!uip.at_end_of_line()) {
        std::cout << uip.get_next_token() << std::endl;
      }
    }
  }


  // set_position() is called when Glaurung receives the "position" UCI
  // command.  The input parameter is a UCIInputParser.  It is assumed
  // that this parser has consumed the first token of the UCI command
  // ("position"), and is ready to read the second token ("startpos"
  // or "fen", if the input is well-formed).

  void set_position(UCIInputParser &uip) {
    std::string token;

    token = uip.get_next_token();
    if(token == "startpos")
      RootPosition.from_fen(StartPosition);
    else if(token == "fen") {
      std::string fen;
      while(token != "moves" && !uip.at_end_of_line()) {
        token = uip.get_next_token();
        fen += token;
        fen += ' ';
      }
      RootPosition.from_fen(fen);
    }

    if(!uip.at_end_of_line()) {
      if(token != "moves")
        token = uip.get_next_token();
      if(token == "moves") {
        Move move;
        UndoInfo u;
        while(!uip.at_end_of_line()) {
          token = uip.get_next_token();
          move = move_from_string(RootPosition, token);
          RootPosition.do_move(move, u);
          if(RootPosition.rule_50_counter() == 0)
            RootPosition.reset_game_ply();
        }
      }
    }
  }


  // set_option() is called when Glaurung receives the "setoption" UCI
  // command.  The input parameter is a UCIInputParser.  It is assumed
  // that this parser has consumed the first token of the UCI command
  // ("setoption"), and is ready to read the second token ("name", if
  // the input is well-formed).

  void set_option(UCIInputParser &uip) {
    std::string token;
    if(!uip.at_end_of_line()) {
      token = uip.get_next_token();
      if(token == "name" && !uip.at_end_of_line()) {
        std::string name = uip.get_next_token();
        std::string nextToken;
        while(!uip.at_end_of_line()
              && (nextToken = uip.get_next_token()) != "value")
          name += (" " + nextToken);
        if(nextToken == "value")
          set_option_value(name, uip.get_rest_of_line());
        else
          push_button(name);
      }
    }
  }


  // go() is called when Glaurung receives the "go" UCI command.  The
  // input parameter is a UCIInputParser.  It is assumed that this
  // parser has consumed the first token of the UCI command ("go"),
  // and is ready to read the second token.  The function sets the
  // thinking time and other parameters from the input string, and
  // calls think() (defined in search.cpp) with the appropriate
  // parameters.

  void go(UCIInputParser &uip) {
    std::string token;
    int time[2] = {0, 0}, inc[2] = {0, 0}, movesToGo = 0, depth = 0, nodes = 0;
    int moveTime = 0;
    bool infinite = false, ponder = false;
    Move searchMoves[500];

    searchMoves[0] = MOVE_NONE;

    while(!uip.at_end_of_line()) {
      token = uip.get_next_token();

      if(token == "infinite")
        infinite = true;
      else if(token == "ponder")
        ponder = true;
      else if(token == "wtime") {
        if(!uip.at_end_of_line())
          time[0] = atoi(uip.get_next_token().c_str());
      }
      else if(token == "btime") {
        if(!uip.at_end_of_line())
          time[1] = atoi(uip.get_next_token().c_str());
      }
      else if(token == "winc") {
        if(!uip.at_end_of_line())
          inc[0] = atoi(uip.get_next_token().c_str());
      }
      else if(token == "binc") {
        if(!uip.at_end_of_line())
          inc[1] = atoi(uip.get_next_token().c_str());
      }
      else if(token == "movestogo") {
        if(!uip.at_end_of_line())
          movesToGo = atoi(uip.get_next_token().c_str());
      }
      else if(token == "depth") {
        if(!uip.at_end_of_line())
          depth = atoi(uip.get_next_token().c_str());
      }
      else if(token == "nodes") {
        if(!uip.at_end_of_line())
          nodes = atoi(uip.get_next_token().c_str());
      }
      else if(token == "movetime") {
        if(!uip.at_end_of_line())
          moveTime = atoi(uip.get_next_token().c_str());
      }
      else if(token == "searchmoves" && !uip.at_end_of_line()) {
        int numOfMoves = 0;
        while(!uip.at_end_of_line()) {
          token = uip.get_next_token();
          searchMoves[numOfMoves++] = move_from_string(RootPosition, token);
        }
        searchMoves[numOfMoves] = MOVE_NONE;
      }
    }

    if(moveTime)
      infinite = true;  // HACK

    think(RootPosition, infinite, ponder, time[RootPosition.side_to_move()],
          inc[RootPosition.side_to_move()], movesToGo, depth, nodes, moveTime,
          searchMoves);
  }
  
}
