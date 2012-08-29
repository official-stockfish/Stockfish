/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <iostream>
#include <sstream>
#include <string>

#include "evaluate.h"
#include "notation.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"

using namespace std;

extern void benchmark(const Position& pos, istream& is);

namespace {

  // FEN string of the initial position, normal chess
  const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  // Keep track of position keys along the setup moves (from start position to the
  // position just before to start searching). Needed by repetition draw detection.
  Search::StateStackPtr SetupStates;

  void set_option(istringstream& up);
  void set_position(Position& pos, istringstream& up);
  void go(Position& pos, istringstream& up);
}


/// Wait for a command from the user, parse this text string as an UCI command,
/// and call the appropriate functions. Also intercepts EOF from stdin to ensure
/// that we exit gracefully if the GUI dies unexpectedly. In addition to the UCI
/// commands, the function also supports a few debug commands.

void UCI::loop(const string& args) {

  Position pos(StartFEN, false, Threads.main_thread()); // The root position
  string cmd, token;

  while (token != "quit")
  {
      if (!args.empty())
          cmd = args;

      else if (!getline(cin, cmd)) // Block here waiting for input
          cmd = "quit";

      istringstream is(cmd);

      is >> skipws >> token;

      if (token == "quit" || token == "stop")
      {
          Search::Signals.stop = true;
          Threads.wait_for_search_finished(); // Cannot quit while threads are running
      }

      else if (token == "ponderhit")
      {
          // The opponent has played the expected move. GUI sends "ponderhit" if
          // we were told to ponder on the same move the opponent has played. We
          // should continue searching but switching from pondering to normal search.
          Search::Limits.ponder = false;

          if (Search::Signals.stopOnPonderhit)
          {
              Search::Signals.stop = true;
              Threads.main_thread()->wake_up(); // Could be sleeping
          }
      }

      else if (token == "go")
          go(pos, is);

      else if (token == "ucinewgame")
          TT.clear();

      else if (token == "isready")
          sync_cout << "readyok" << sync_endl;

      else if (token == "position")
          set_position(pos, is);

      else if (token == "setoption")
          set_option(is);

      else if (token == "d")
          pos.print();

      else if (token == "flip")
          pos.flip();

      else if (token == "eval")
          sync_cout << Eval::trace(pos) << sync_endl;

      else if (token == "bench")
          benchmark(pos, is);

      else if (token == "key")
          sync_cout << "key: " << hex     << pos.key()
                    << "\nmaterial key: " << pos.material_key()
                    << "\npawn key: "     << pos.pawn_key() << sync_endl;

      else if (token == "uci")
          sync_cout << "id name " << engine_info(true)
                    << "\n"       << Options
                    << "\nuciok"  << sync_endl;

      else if (token == "perft" && (is >> token)) // Read depth
      {
          stringstream ss;

          ss << Options["Hash"]    << " "
             << Options["Threads"] << " " << token << " current perft";

          benchmark(pos, ss);
      }

      else
          sync_cout << "Unknown command: " << cmd << sync_endl;

      if (!args.empty()) // Command line arguments have one-shot behaviour
      {
          Threads.wait_for_search_finished();
          break;
      }
  }
}


namespace {

  // set_position() is called when engine receives the "position" UCI command.
  // The function sets up the position described in the given fen string ("fen")
  // or the starting position ("startpos") and then makes the moves given in the
  // following move list ("moves").

  void set_position(Position& pos, istringstream& is) {

    Move m;
    string token, fen;

    is >> token;

    if (token == "startpos")
    {
        fen = StartFEN;
        is >> token; // Consume "moves" token if any
    }
    else if (token == "fen")
        while (is >> token && token != "moves")
            fen += token + " ";
    else
        return;

    pos.from_fen(fen, Options["UCI_Chess960"], Threads.main_thread());
    SetupStates = Search::StateStackPtr(new std::stack<StateInfo>());

    // Parse move list (if any)
    while (is >> token && (m = move_from_uci(pos, token)) != MOVE_NONE)
    {
        SetupStates->push(StateInfo());
        pos.do_move(m, SetupStates->top());
    }
  }


  // set_option() is called when engine receives the "setoption" UCI command. The
  // function updates the UCI option ("name") to the given value ("value").

  void set_option(istringstream& is) {

    string token, name, value;

    is >> token; // Consume "name" token

    // Read option name (can contain spaces)
    while (is >> token && token != "value")
        name += string(" ", !name.empty()) + token;

    // Read option value (can contain spaces)
    while (is >> token)
        value += string(" ", !value.empty()) + token;

    if (Options.count(name))
        Options[name] = value;
    else
        sync_cout << "No such option: " << name << sync_endl;
  }


  // go() is called when engine receives the "go" UCI command. The function sets
  // the thinking time and other parameters from the input string, and then starts
  // the search.

  void go(Position& pos, istringstream& is) {

    Search::LimitsType limits;
    vector<Move> searchMoves;
    string token;

    while (is >> token)
    {
        if (token == "wtime")
            is >> limits.time[WHITE];
        else if (token == "btime")
            is >> limits.time[BLACK];
        else if (token == "winc")
            is >> limits.inc[WHITE];
        else if (token == "binc")
            is >> limits.inc[BLACK];
        else if (token == "movestogo")
            is >> limits.movestogo;
        else if (token == "depth")
            is >> limits.depth;
        else if (token == "nodes")
            is >> limits.nodes;
        else if (token == "movetime")
            is >> limits.movetime;
        else if (token == "infinite")
            limits.infinite = true;
        else if (token == "ponder")
            limits.ponder = true;
        else if (token == "searchmoves")
            while (is >> token)
                searchMoves.push_back(move_from_uci(pos, token));
    }

    Threads.start_searching(pos, limits, searchMoves, SetupStates);
  }
}
