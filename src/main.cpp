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

#include <cstdio>
#include <iostream>
#include <string>

#include "bitboard.h"
#include "evaluate.h"
#include "position.h"
#include "thread.h"
#include "search.h"
#include "ucioption.h"

using namespace std;

extern void uci_loop();
extern void benchmark(int argc, char* argv[]);
extern void kpk_bitbase_init();

int main(int argc, char* argv[]) {

  // Disable IO buffering for C and C++ standard libraries
  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  cout.rdbuf()->pubsetbuf(NULL, 0);
  cin.rdbuf()->pubsetbuf(NULL, 0);

  // Startup initializations
  init_bitboards();
  Position::init();
  kpk_bitbase_init();
  Search::init();
  Threads.init();

  if (argc < 2)
  {
      cout << engine_name() << " by " << engine_authors() << endl;

      if (CpuHasPOPCNT)
          cout << "Good! CPU has hardware POPCNT." << endl;

      uci_loop(); // Enter the UCI loop and wait for user input
  }
  else if (string(argv[1]) == "bench" && argc < 8)
      benchmark(argc, argv);

  else
      cout << "Usage: stockfish bench [hash size = 128] [threads = 1] "
           << "[limit = 12] [fen positions file = default] "
           << "[limited by depth, time, nodes or perft = depth]" << endl;

  Threads.exit();
  return 0;
}
