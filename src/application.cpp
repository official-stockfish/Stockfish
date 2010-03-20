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
#include "ucioption.h"


/// Application class is in charge of initializing global resources
/// at startup and cleanly releases them when program terminates.

Application::Application() {

    init_mersenne();
    init_direction_table();
    init_bitboards();
    init_uci_options();
    Position::init_zobrist();
    Position::init_piece_square_tables();
    init_eval(1);
    init_bitbases();
    init_search();
    init_threads();

    // Make random number generation less deterministic, for book moves
    for (int i = abs(get_system_time() % 10000); i > 0; i--)
        genrand_int32();
}

Application::~Application() {

  exit_threads();
  quit_eval();
}

void Application::initialize() {

  // A static Application object is allocated
  // once only when this function is called.
  static Application singleton;
}

void Application::exit_with_failure() {

  exit(EXIT_FAILURE); // d'tor will be called automatically
}
