/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include "bitboard.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "endgame.h"
#include "syzygy/tbprobe.h"

#ifdef WIN32
#define EXPORT_API __declspec(dllexport)
#else
#define EXPORT_API
#endif

namespace PSQT {
  void init();
}

int main(int argc, char* argv[]) {
  std::cout << engine_info() << std::endl;

  UCI::init(Options);
  PSQT::init();
  Bitboards::init();
  Position::init();
  Bitbases::init();
  Endgames::init();
  Search::init();
  Threads.set(Options["Threads"]);
  Search::clear(); // After threads are up

  UCI::loop(argc, argv);

  Threads.set(0);
  return 0;
}
/*
#include <iostream>

pthread_t thread = 0;

static void* stockfish_loop(void *args)
{
    char argv[1][256];

    strcpy(argv[0], "stockfish");

    UCI::loop(1, (char**)argv);

    return args;
}
*/
int EXPORT_API stockfish_init()
{
    UCI::init(Options);
    PSQT::init();
    Bitboards::init();
    Position::init();
    Bitbases::init();
    Endgames::init();
    Search::init();
    Threads.set(Options["Threads"]);
    Search::clear(); // After threads are up

    UCI::command("init");

    return 0;
}

int EXPORT_API stockfish_command(const char *cmd, char *out)
{
    std::string os = UCI::command(cmd);
    if (out)
        strcpy(out, os.c_str());
    return 0;
}

int EXPORT_API stockfish_exit()
{
    Threads.set(0);

    return 0;
}
