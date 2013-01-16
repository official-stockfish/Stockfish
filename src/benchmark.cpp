/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2012 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <fstream>
#include <iostream>
#include <istream>
#include <vector>

#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "ucioption.h"

using namespace std;

static const char* Defaults[] = {
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
  "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
  "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
  "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14",
  "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
  "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
  "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
  "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
  "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
  "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
  "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
  "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
  "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
  "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26"
};


/// benchmark() runs a simple benchmark by letting Stockfish analyze a set
/// of positions for a given limit each. There are five parameters; the
/// transposition table size, the number of search threads that should
/// be used, the limit value spent for each position (optional, default is
/// depth 12), an optional file name where to look for positions in fen
/// format (defaults are the positions defined above) and the type of the
/// limit value: depth (default), time in secs or number of nodes.

void benchmark(const Position& current, istream& is) {

  string token;
  Search::LimitsType limits;
  vector<string> fens;

  // Assign default values to missing arguments
  string ttSize    = (is >> token) ? token : "32";
  string threads   = (is >> token) ? token : "1";
  string limit     = (is >> token) ? token : "12";
  string fenFile   = (is >> token) ? token : "default";
  string limitType = (is >> token) ? token : "depth";

  Options["Hash"]    = ttSize;
  Options["Threads"] = threads;
  TT.clear();

  if (limitType == "time")
      limits.movetime = 1000 * atoi(limit.c_str()); // movetime is in ms

  else if (limitType == "nodes")
      limits.nodes = atoi(limit.c_str());

  else if (limitType == "mate")
      limits.mate = atoi(limit.c_str());

  else
      limits.depth = atoi(limit.c_str());

  if (fenFile == "default")
      fens.assign(Defaults, Defaults + 16);

  else if (fenFile == "current")
      fens.push_back(current.fen());

  else
  {
      string fen;
      ifstream file(fenFile.c_str());

      if (!file.is_open())
      {
          cerr << "Unable to open file " << fenFile << endl;
          return;
      }

      while (getline(file, fen))
          if (!fen.empty())
              fens.push_back(fen);

      file.close();
  }

  int64_t nodes = 0;
  Search::StateStackPtr st;
  Time::point elapsed = Time::now();

  for (size_t i = 0; i < fens.size(); i++)
  {
      Position pos(fens[i], Options["UCI_Chess960"], Threads.main_thread());

      cerr << "\nPosition: " << i + 1 << '/' << fens.size() << endl;

      if (limitType == "perft")
      {
          size_t cnt = Search::perft(pos, limits.depth * ONE_PLY);
          cerr << "\nPerft " << limits.depth  << " leaf nodes: " << cnt << endl;
          nodes += cnt;
      }
      else
      {
          Threads.start_thinking(pos, limits, vector<Move>(), st);
          Threads.wait_for_think_finished();
          nodes += Search::RootPos.nodes_searched();
      }
  }

  elapsed = Time::now() - elapsed + 1; // Assure positive to avoid a 'divide by zero'

  cerr << "\n==========================="
       << "\nTotal time (ms) : " << elapsed
       << "\nNodes searched  : " << nodes
       << "\nNodes/second    : " << 1000 * nodes / elapsed << endl;
}
