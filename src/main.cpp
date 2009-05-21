/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2009 Marco Costalba

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

// To profile with callgrind uncomment following line
#define USE_CALLGRIND


////
//// Includes
////

#include <iostream>
#include <string>

#include "benchmark.h"
#include "misc.h"
#include "uci.h"

#ifdef USE_CALLGRIND
#include <valgrind/callgrind.h>
#endif

using namespace std;


////
//// Functions
////

int main(int argc, char *argv[]) {

  // Disable IO buffering
  cout.rdbuf()->pubsetbuf(NULL, 0);
  cin.rdbuf()->pubsetbuf(NULL, 0);

  // Initialization through global resources manager
  Application::initialize();

#ifdef USE_CALLGRIND
  CALLGRIND_START_INSTRUMENTATION;
#endif

  // Process command line arguments if any
  if (argc > 1)
  {
      if (string(argv[1]) != "bench" || argc < 4 || argc > 7)
          cout << "Usage: stockfish bench <hash size> <threads> "
               << "[time = 60s] [fen positions file = default] "
               << "[time, depth or node limited = time]" << endl;
      else
      {
          string time = argc > 4 ? argv[4] : "60";
          string fen = argc > 5 ? argv[5] : "default";
          string lim = argc > 6 ? argv[6] : "time";
          benchmark(string(argv[2]) + " " + string(argv[3]) + " " + time + " " + fen + " " + lim);
      }
      return 0;
  }

  // Print copyright notice
  cout << engine_name() << ".  Copyright (C) "
       << "2004-2009 Tord Romstad, Marco Costalba. " << endl;

  // Enter UCI mode
  uci_main_loop();
  return 0;
}
