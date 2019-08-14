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

#ifdef USE_DLL
#define EXPORT_API __declspec(dllexport)
#else
#define EXPORT_API
#endif

namespace PSQT {
	void init();
}

#ifndef USE_DLL
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

#else

std::stringstream _os;
static char _str[1 << 10];

static int string_copy(const wchar_t *src, char *dst, int dst_len)
{
	if (!src || !dst || (dst_len <= 1))
		return 0;
	int len = 0;
	for (; src && *src && (dst_len > 1); src += 1, dst += 1, len += 1, dst_len -= 1)
		*dst = (char)*src;
	if (dst_len > 0)
		*dst = 0;
	return len;
}

static int string_copy(const char *src, wchar_t *dst, int dst_len)
{
	if (!src || !dst || (dst_len <= 1))
		return 0;
	int len = 0;
	for (; src && *src && (dst_len > 1); src += 1, dst += 1, len += 1, dst_len -= 1)
		*dst = (wchar_t)(*src & 0xFF);
	if (dst_len > 0)
		*dst = 0;
	return len;
}

static int os_len()
{
	_os.seekp(0, std::ios::end);

	return (int)_os.tellp() - (int)_os.tellg();
}

extern "C"
{
	EXPORT_API int stockfish_init()
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

	EXPORT_API int stockfish_command(const wchar_t *cmd, wchar_t *dst, int dst_size)
	{
		if (!cmd) {
			if (!dst)
				return -1;
			int len = os_len();
			if (len <= 0)
				return 0;
			std::string out;
			getline(_os, out);
			string_copy(out.c_str(), dst, dst_size);
			len = os_len();
			if (len <= 0) {
				_os.str("");
				_os.clear();
			}
			return (len > 0)? 1 : 0;
		}

		_os.str("");
		_os.clear();

		string_copy(cmd, _str, (int)sizeof(_str));

		UCI::command(_str);

		if (dst) {
			int len = os_len();
			if (len > 0) {
				string_copy(_os.str().c_str() + (int)_os.tellg(), dst, dst_size);
				_os.str("");
				_os.clear();
			}
			return 0;
		}

		return 1;
	}

	EXPORT_API int stockfish_exit()
	{
		UCI::command("quit");

		Threads.set(0);

		return 0;
	}
}
#endif

