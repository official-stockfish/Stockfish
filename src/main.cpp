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

#include "benchmark.h"
#include "bitboard.h"
#include "direction.h"
#include "endgame.h"
#include "evaluate.h"
#include "material.h"
#include "mersenne.h"
#include "misc.h"
#include "movepick.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "uci.h"
#include "ucioption.h"

using std::string;

//// 
//// Functions
////

int main(int argc, char *argv[]) {

  // Disable IO buffering
  std::cout.rdbuf()->pubsetbuf(NULL, 0);
  std::cin.rdbuf()->pubsetbuf(NULL, 0);

  // Initialization

  init_mersenne();
  init_direction_table();
  init_bitboards();
  init_uci_options();
  Position::init_zobrist();
  Position::init_piece_square_tables();
  MovePicker::init_phase_table();
  init_eval(1);
  init_bitbases();
  init_threads();

  // Make random number generation less deterministic, for book moves
  for (int i = abs(get_system_time() % 10000); i > 0; i--)
      genrand_int32();

  // Process command line arguments
  if (argc >= 2 && string(argv[1]) == "bench")
  {
      if (argc < 4 || argc > 6)
      {
        std::cout << "Usage: glaurung bench <hash size> <threads> "
                  << "[time = 60s] [fen positions file = default]"
                  << std::endl;
        exit(0);
      }
      string time = argc > 4 ? argv[4] : "60";
      string fen = argc > 5 ? argv[5] : "default";
      benchmark(string(argv[2]) + " " + string(argv[3]) + " " + time + " " + fen);
      return 0;
  }

  // Print copyright notice
  std::cout << engine_name() << ".  Copyright (C) "
            << "2004-2008 Tord Romstad, Marco Costalba. "
            << std::endl;

  // Enter UCI mode
  uci_main_loop();

  return 0;
}
