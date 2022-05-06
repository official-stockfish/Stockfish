/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

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
#include <string>

#include "bitboard.h"
#include "endgame.h"
#include "position.h"
#include "psqt.h"
#include "search.h"
#include "syzygy/tbprobe.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"


using namespace Stockfish;


int main(int argc, char* argv[]) {

  bool retVal = false;

  if(argc > 1 && (strcmp(argv[1],"--help") == 0)) {

    /// process general help request (ex: --help)
    if(argc == 2) {
  
      if(strcmp(argv[1],"--help") == 0) {
        sync_cout << "\n-----UCI COMMANDS-----" << sync_endl;
	sync_cout << "quit		exit stockfish" << sync_endl;
	sync_cout << "stop		halt move search" << sync_endl;
	sync_cout << "ponderhit	start search (ponder) on same move user has played" << sync_endl;
	sync_cout << "uci		tell engine to use UCI interface (will display options)" << sync_endl;
	sync_cout << "setoption	set specific UCI option" << sync_endl;
	sync_cout << "go		start move search based on current position" << sync_endl;
	sync_cout << "help		add'l detail on using help system" << sync_endl;
	sync_cout << "ucinewgame	start move search on new/different game" << sync_endl;
	sync_cout << "isready		response is 'ready' if engine is ready and available" << sync_endl;
	sync_cout << "flip		flip sides" << sync_endl;
	sync_cout << "bench		calculate/display benchmarks for this installation of stockfish" << sync_endl;
	sync_cout << "d		display chess board and current position of all pieces" << sync_endl;
	sync_cout << "eval		display current NNUE evaluation" << sync_endl;
	sync_cout << "compiler	display info re:compiler used for this installation of stockfish" << sync_endl;
	sync_cout << "export_net	save current stockfish neural network to file" << sync_endl;
	sync_cout << "position	set up position in festering or use startups" << sync_endl;
	sync_cout << "\nSee the following for a full UCI protocol discussion:" << sync_endl;
	sync_cout << "   http://wbec-ridderkerk.nl/html/UCIProtocol.html\n" << sync_endl;

	retVal = true;
      }
    }

    /// process specific help requests (ex: --help <ucicommand>)
    if(argc == 3 && (strcmp(argv[1],"--help") == 0)) {

      if(strcmp(argv[2],"quit") == 0) {
	  printf("[quit]\nquit the program as soon as possible\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"stop") == 0) {
	  printf("[stop]\nstop calculating as soon as possible, don't forget\n"
	  	"the 'bestmove' and possibly the 'ponder' token when finishing the search\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"ponderhit") == 0) {
	  printf("[ponderhit]\nThe user has played the expected move. This will be sent if the engine was told\n"
		 "to ponder on the same move the user has played.\n"
		 "The engine should continue searching but switch from pondering to normal search.\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"uci") == 0) {
        printf("[uci]\nTell engine to use the uci (universal chess interface).\n"
		"This will be sent once, by a GUI, as a first command after program boot\n"
		"to tell the engine to switch to uci mode.\n\n"
		"After receiving the uci command the engine must identify itself with\n"
		"the 'id' command and send the 'option' commands to tell the GUI which\n"
		"engine settings the engine supports (if any).\n\n"
		"After that the engine should send 'uciok' to acknowledge the uci mode.\n"
		"If no uciok is sent within a certain time period, the engine task will \n"
		"be killed by the GUI.\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"setoption") == 0) {
	printf("[setoption]\nsetoption name <id> [value <x>]\n"
		"This is sent to the engine when the user wants to change the internal parameters\n"
		"of the engine. For the 'button' type no value is needed.\n\n"
		"One string will be sent for each parameter and this will only be sent\n"
		"when the engine is waiting.\n\n"
		"The name and value of the option in <id> should not be case sensitive and\n"
		"can include spaces.\n\n"
		"The substrings 'value' and 'name' should be avoided in <id> and <x> to allow\n"
		"unambiguous parsing, for example do not use <name> = 'draw value'.\n\n"
		"Here are some examples:\n"
		"setoption name Nullmove value true\n"
     		"setoption name Selectivity value 3\n"
	  	"setoption name Style value Risky\n"
	  	"setoption name Clear Hash\n"
	  	"setoption name NalimovPath value c:\\chess\\tb\\4;c:\\chess\\tb\\5\\n\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"go") == 0) {
		printf("[go]\nStart calculating on the current position set up with the 'position'\n"
		"command.\n\n"
		"There are a number of parameters that can follow this command and all\n"
		"will be sent in the same string.\n\n"
		"If one parameter is not sent its value should be interpreted as it would\n"
		"not influence the search.\n\n"
		"The following are the parameters and their associated values\n\n");

		printf("[go parameters]\n\n");
		printf("* searchmoves <move1> .... <movei>\n"
		"	restrict search to these moves only\n"
		"	Example: After 'position startpos' and\n"
		"		       'go infinite searchmoves e2e4 d2d4'\n"
		"	the engine should only search the two moves e2e4 and d2d4 in the\n"
		"	initial position.\n\n");

		printf("* ponder\n"
		"	start searching in pondering mode.\n"
		"	Do not exit the search in ponder mode, even if it's mate!\n\n"
		"	This means that the last move sent in in the position string\n"
		"	is the ponder move.  The engine can do what it wants to do, but\n"
		"	after a 'ponderhit' command it should execute the suggested move\n"
		"	to ponder on.\n\n"
		"	This means that the ponder move sent by the GUI can be\n"
		"	interpreted as a recommendation about which move to ponder.\n"
		"	However, if the engine decides to ponder on a different move, it\n"
		"	should not display any mainlines as they are likely to be\n"
		"	misinterpreted by the GUI because the GUI expects the engine\n"
		"	to ponder on the suggested move.\n\n");

		printf("* wtime <x>\n"
		"	white has x msec left on the clock\n"
		"* btime <x>\n"
		"	black has x msec left on the clock\n"
		"* winc <x>\n"
		"	white increment per move in mseconds if x > 0\n"
		"* binc <x>\n"
		"	black increment per move in mseconds if x > 0\n"
		"* movestogo <x>\n"
      		"	there are x moves to the next time control\n"
		"		NOTE: this will only be sent if x > 0,\n"
		"		      if you don't get this and get the\n"
		"		      wtime and btime it's sudden death\n\n"
		"* depth <x>\n"
		"	search x plies only.\n"
		"* nodes <x>\n"
	   	"	search x nodes only\n\n");

		printf("* mate <x>\n"
		"	search for a mate in x moves\n"
		"* movetime <x>\n"
		"	search exactly x mseconds\n"
		"* infinite\n"
		"	search until the 'stop' command. Do not exit the\n"
		"	search without being told so in this mode!\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"help") == 0) {
	  printf("[help]\n"
		 "usage:\n"
		 "	stockfish --help <uci_cmd optional>\n\n"
		 "	--help all by itself will display the main help screen\n"
		 "	--help followed by any UCI command will display specific\n"
		 "	  information on that particular UCI command\n"
		 "	     example:  stockfish --help help)\n"
		 "	     will display this exact screen\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"ucinewgame") == 0) {
	printf("[ucinewgame]\nThis is sent to the engine when the next search (started with 'position' and\n"
		"'go') will be from a different game. This can be a new game the engine should\n"
		"play or a new game it should analyse but also the next position from a testsuite\n"
		"with positions only.\n\n"
   		"If the GUI hasn't sent a 'ucinewgame' before the first 'position' command,\n"
		"the engine shouldn't expect any further ucinewgame commands as the GUI is\n"
		"probably not supporting the ucinewgame command.\n\n"
   		"So the engine should not rely on this command even though all new GUIs should\n"
		"support it.\n\n"
   		"As the engine's reaction to 'ucinewgame' can take some time the GUI should\n"
		"always send 'isready' after 'ucinewgame' to wait for the engine to finish\n"
		"its operation. The engine should respond with 'readyok'\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"isready") == 0) {
	printf("[isready]\nThis is used to synchronize the engine with the GUI.\n"
		"When the GUI has sent a command or multiple commands that can take some time\n"
		"to complete, this command can be used to wait for the engine to be ready again\n"
		"or to ping the engine to find out if it is still alive.\n\n"
		"e.g. this should be sent after setting the path to the tablebases as this\n" 
		"can take some time.\n\n"
		"This command is also required once, before the engine is asked to do any\n"
		"searching, to wait for the engine to finish initializing.\n\n"
		"This command must always be answered with 'readyok' and can be sent also when\n"
		"the engine is calculating in which case the engine should also immediately\n"
		"answer with 'readyok' without stopping the search.\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"flip") == 0) {
	  printf("[flip]\nFlip sides in the current game\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"bench") == 0) {
	  printf("[bench]\ncalculate/display benchmarks for this installation of stockfish\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"d") == 0) {
	  printf("[d]\ndisplay chess board and current position of all pieces\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"eval") == 0) {
	  printf("[eval]\ndisplay current NNUE evaluation\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"compiler") == 0) {
	  printf("[compiler]\ndisplay information about the compiler use for this installation of stockfish\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"export_net") == 0) {
	  printf("[export_net]\nsave current stockfish neural network to file\n\n");
	  retVal = true;
      }
      if(strcmp(argv[2],"position") == 0) {
	printf("[position]\nposition [fen <fenstring> | startpos ]  moves <move1> .... <movei>\n"
		"Set up the position described in fenstring on the internal board and\n"
		"play the moves on the internal chess board.\n\n"
		"If the game was played  from the start position the string 'startpos'\n"
		"must be sent\n\n"
		"Note: no 'new' command is needed. However, if this position is from\n"
		"a different game than the last position sent to the engine, the GUI\n"
		"should have sent a 'ucinewgame' in between.\n\n");
	  retVal = true;
      }
    }

    if(retVal) {
      return 0;
    }

  }

  std::cout << engine_info() << std::endl;


  CommandLine::init(argc, argv);
  UCI::init(Options);
  Tune::init();
  PSQT::init();
  Bitboards::init();
  Position::init();
  Bitbases::init();
  Endgames::init();
  Threads.set(size_t(Options["Threads"]));
  Search::clear(); // After threads are up
  Eval::NNUE::init();

  UCI::loop(argc, argv);

  Threads.set(0);
  return 0;
}


