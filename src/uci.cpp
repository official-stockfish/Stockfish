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
#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "ucioption.h"

using namespace std;

namespace {

  // FEN string of the initial position, normal chess
  const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  // Keep track of position keys along the setup moves (from start position to the
  // position just before to start searching). This is needed by draw detection
  // where, due to 50 moves rule, we need to check at most 100 plies back.
  StateInfo StateRingBuf[102], *SetupState = StateRingBuf;

  void set_option(istringstream& up);
  void set_position(Position& pos, istringstream& up);
  void go(Position& pos, istringstream& up);
  void perft(Position& pos, istringstream& up);
}


/// Wait for a command from the user, parse this text string as an UCI command,
/// and call the appropriate functions. Also intercepts EOF from stdin to ensure
/// that we exit gracefully if the GUI dies unexpectedly. In addition to the UCI
/// commands, the function also supports a few debug commands.

void uci_loop() {

  Position pos(StartFEN, false, 0); // The root position
  string cmd, token;

  while (token != "quit")
  {
      if (!getline(cin, cmd)) // Block here waiting for input
          cmd = "quit";

      istringstream is(cmd);

      is >> skipws >> token;

      if (token == "quit" || token == "stop")
      {
          Search::Signals.stop = true;

          if (token == "quit") // Cannot quit while threads are still running
              Threads.wait_for_search_finished();
      }

      else if (token == "ponderhit")
      {
          // The opponent has played the expected move. GUI sends "ponderhit" if
          // we were told to ponder on the same move the opponent has played. We
          // should continue searching but switching from pondering to normal search.
          Search::Limits.ponder = false;

          if (Search::Signals.stopOnPonderhit)
              Search::Signals.stop = true;
      }

      else if (token == "go")
          go(pos, is);

      else if (token == "isready")
          cout << "readyok" << endl;

      else if (token == "position")
          set_position(pos, is);

      else if (token == "setoption")
          set_option(is);

      else if (token == "perft")
          perft(pos, is);

      else if (token == "d")
          pos.print();

      else if (token == "flip")
          pos.flip_me();

      else if (token == "eval")
          cout << Eval::trace(pos) << endl;

      else if (token == "key")
          cout << "key: " << hex     << pos.key()
               << "\nmaterial key: " << pos.material_key()
               << "\npawn key: "     << pos.pawn_key() << endl;

      else if (token == "uci")
          cout << "id name "     << engine_info(true)
               << "\n"           << Options
               << "\nuciok"      << endl;
      else
          cout << "Unknown command: " << cmd << endl;
  }
}


namespace {

  // set_position() is called when engine receives the "position" UCI
  // command. The function sets up the position described in the given
  // fen string ("fen") or the starting position ("startpos") and then
  // makes the moves given in the following move list ("moves").

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

    pos.from_fen(fen, Options["UCI_Chess960"]);

    // Parse move list (if any)
    while (is >> token && (m = move_from_uci(pos, token)) != MOVE_NONE)
    {
        pos.do_move(m, *SetupState);

        // Increment pointer to StateRingBuf circular buffer
        if (++SetupState - StateRingBuf >= 102)
            SetupState = StateRingBuf;
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
        cout << "No such option: " << name << endl;
  }


  // go() is called when engine receives the "go" UCI command. The function sets
  // the thinking time and other parameters from the input string, and then starts
  // the search.

  void go(Position& pos, istringstream& is) {

    Search::LimitsType limits;
    std::set<Move> searchMoves;
    string token;

    while (is >> token)
    {
        if (token == "wtime")
            is >> limits.times[WHITE];
        else if (token == "btime")
            is >> limits.times[BLACK];
        else if (token == "winc")
            is >> limits.incs[WHITE];
        else if (token == "binc")
            is >> limits.incs[BLACK];
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
                searchMoves.insert(move_from_uci(pos, token));
    }

    Threads.start_searching(pos, limits, searchMoves);
  }


  // perft() is called when engine receives the "perft" command. The function
  // calls perft() with the required search depth then prints counted leaf nodes
  // and elapsed time.

  void perft(Position& pos, istringstream& is) {

    int depth;

    if (!(is >> depth))
        return;

    Time time = Time::current_time();

    int64_t n = Search::perft(pos, depth * ONE_PLY);

    int e = time.elapsed();

    std::cout << "\nNodes " << n
              << "\nTime (ms) " << e
              << "\nNodes/second " << int(n / (e / 1000.0)) << std::endl;
  }
}
