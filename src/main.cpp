/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

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

#ifdef _WIN32
#include <filesystem>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "bitboard.h"
#include "endgame.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

namespace PSQT {
  void init();
}

int main(int argc, char* argv[]) {
  // Change the current working directory to the binary directory.  So that a
  // net file path can be specified with a relative path from the binary
  // directory.
  // TODO(someone): Implement the logic for other OS.
#ifdef _WIN32
  TCHAR filename[_MAX_PATH];
  ::GetModuleFileName(NULL, filename, sizeof(filename) / sizeof(filename[0]));
  std::filesystem::path current_path = filename;
  current_path.remove_filename();
  std::filesystem::current_path(current_path);
#endif

  std::cout << engine_info() << std::endl;

  UCI::init(Options);
  Tune::init();
  PSQT::init();
  Bitboards::init();
  Position::init();
  Bitbases::init();
  Endgames::init();
  Threads.set(size_t(Options["Threads"]));
  Search::clear(); // After threads are up
  Eval::init_NNUE();

  UCI::loop(argc, argv);

  Threads.set(0);
  return 0;
}
