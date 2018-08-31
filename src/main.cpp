/*
 McBrain, a UCI chess playing engine derived from Stockfish and Glaurung 2.1
 Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
 Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish Authors)
 Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (Stockfish Authors)
 Copyright (C) 2017-2018 Michael Byrne, Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (McBrain Authors)
 
 McBrain is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 McBrain is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <utility>
#include <thread>
#include <chrono>
#include <functional>
#include <atomic>
#include <time.h>

#include "bitboard.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "syzygy/tbprobe.h"
#include "polybook.h"

namespace PSQT {
  void init();
}

int main(int argc, char* argv[]) {

	{
#ifdef _WIN32
		const size_t time_length_const = 100;
		char time_local[time_length_const];
		memset(time_local, char(0), time_length_const);
		time_t result = time(NULL);
		tm tm_local;
		errno_t errno_local = localtime_s(&tm_local, &result);
		if (errno_local == 0)
		{
			errno_local = asctime_s(time_local, time_length_const, &tm_local);
			if (errno_local == 0)
			{
				std::cout << time_local;
			}
			else
			{
				assert(errno_local != 0);
			}
		}
		else
		{
			assert(errno_local != 0);
		}
#else
		std::time_t result = std::time(NULL);
		std::cout << std::asctime(std::localtime(&result));
#endif
	}

	std::cout << hardware_info() << std::endl;
	std::cout << system_info() << std::endl;
	std::cout << engine_info() << std::endl;
	std::cout << cores_info() << std::endl;

  UCI::init(Options);
  PSQT::init();
  Bitboards::init();
  Position::init();
  Bitbases::init();

  Search::init(Options["Clear Search"]);
  Pawns::init();
  polybook.init(Options["BookFile"]);
  Tablebases::init(Options["SyzygyPath"]); // After Bitboards are set
  Threads.set(Options["Threads"]);
  Search::clear(); // After threads are up

  UCI::loop(argc, argv);

  Threads.set(0);
  return 0;
}
