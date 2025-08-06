/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

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
#include <memory>

#include "bitboard.h"
#include "misc.h"
#include "position.h"
#include "tune.h"
#include "types.h"
#include "uci.h"

#include "shm.h"

#if defined(SHM_CLEANUP)
    #include <cstdlib>
    #include <cstdio>

    #include <signal.h>
#endif

using namespace Stockfish;

namespace {
#if defined(SHM_CLEANUP)

void register_cleanup() {
    // hack to invoke atexit
    int signals[] = {SIGHUP,  SIGINT,  SIGQUIT, SIGILL, SIGABRT, SIGFPE,
                     SIGSEGV, SIGTERM, SIGBUS,  SIGSYS, SIGXCPU, SIGXFSZ};

    struct sigaction sa;
    sa.sa_handler = [](int) { std::exit(1); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    for (int sig : signals)
        if (sigaction(sig, &sa, nullptr) == -1)
            std::perror("sigaction");

    // Cleanup function to ensure shared memory is unlinked on exit
    std::atexit([]() { shm::SharedMemory<Eval::NNUE::Networks>::cleanup_all_instances(); });
}
#else
void register_cleanup() {}
#endif
}

int main(int argc, char* argv[]) {
    register_cleanup();

    std::cout << engine_info() << std::endl;

    Bitboards::init();
    Position::init();

    auto uci = std::make_unique<UCIEngine>(argc, argv);

    Tune::init(uci->engine_options());

    uci->loop();

    return 0;
}
