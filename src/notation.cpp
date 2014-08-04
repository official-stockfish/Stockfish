/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include <cassert>
#include <iomanip>
#include <sstream>
#include <stack>

#include "movegen.h"
#include "notation.h"
#include "position.h"

using namespace std;

static const char* PieceToChar[COLOR_NB] = { " PNBRQK", " pnbrqk" };


/// score_to_uci() converts a value to a string suitable for use with the UCI
/// protocol specifications:
///
/// cp <x>     The score from the engine's point of view in centipawns.
/// mate <y>   Mate in y moves, not plies. If the engine is getting mated
///            use negative values for y.

string score_to_uci(Value v, Value alpha, Value beta) {

  stringstream ss;

  if (abs(v) < VALUE_MATE_IN_MAX_PLY)
      ss << "cp " << v * 100 / PawnValueEg;
  else
      ss << "mate " << (v > 0 ? VALUE_MATE - v + 1 : -VALUE_MATE - v) / 2;

  ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

  return ss.str();
}


/// move_to_uci() converts a move to a string in coordinate notation
/// (g1f3, a7a8q, etc.). The only special case is castling moves, where we print
/// in the e1g1 notation in normal chess mode, and in e1h1 notation in chess960
/// mode. Internally castling moves are always encoded as "king captures rook".

const string move_to_uci(Move m, bool chess960) {

  Square from = from_sq(m);
  Square to = to_sq(m);

  if (m == MOVE_NONE)
      return "(none)";

  if (m == MOVE_NULL)
      return "0000";

  if (type_of(m) == CASTLING && !chess960)
      to = make_square(to > from ? FILE_G : FILE_C, rank_of(from));

  string move = to_string(from) + to_string(to);

  if (type_of(m) == PROMOTION)
      move += PieceToChar[BLACK][promotion_type(m)]; // Lower case

  return move;
}


/// move_from_uci() takes a position and a string representing a move in
/// simple coordinate notation and returns an equivalent legal Move if any.

Move move_from_uci(const Position& pos, string& str) {

  if (str.length() == 5) // Junior could send promotion piece in uppercase
      str[4] = char(tolower(str[4]));

  for (MoveList<LEGAL> it(pos); *it; ++it)
      if (str == move_to_uci(*it, pos.is_chess960()))
          return *it;

  return MOVE_NONE;
}


/// pretty_pv() formats human-readable search information, typically to be
/// appended to the search log file. It uses the two helpers below to pretty
/// format the time and score respectively.

static string format(int64_t msecs) {

  const int MSecMinute = 1000 * 60;
  const int MSecHour   = 1000 * 60 * 60;

  int64_t hours   =   msecs / MSecHour;
  int64_t minutes =  (msecs % MSecHour) / MSecMinute;
  int64_t seconds = ((msecs % MSecHour) % MSecMinute) / 1000;

  stringstream ss;

  if (hours)
      ss << hours << ':';

  ss << setfill('0') << setw(2) << minutes << ':' << setw(2) << seconds;

  return ss.str();
}

static string format(Value v) {

  stringstream ss;

  if (v >= VALUE_MATE_IN_MAX_PLY)
      ss << "#" << (VALUE_MATE - v + 1) / 2;

  else if (v <= VALUE_MATED_IN_MAX_PLY)
      ss << "-#" << (VALUE_MATE + v) / 2;

  else
      ss << setprecision(2) << fixed << showpos << double(v) / PawnValueEg;

  return ss.str();
}

string pretty_pv(const Position& pos, int depth, Value value, int64_t msecs, Move pv[]) {

  const uint64_t K = 1000;
  const uint64_t M = 1000000;

  stringstream ss;
  ss << setw(2) << depth << setw(8) << format(value) << setw(8) << format(msecs);

  if (pos.nodes_searched() < M)
      ss << setw(8) << pos.nodes_searched() / 1 << "  ";

  else if (pos.nodes_searched() < K * M)
      ss << setw(7) << pos.nodes_searched() / K << "K  ";

  else
      ss << setw(7) << pos.nodes_searched() / M << "M  ";

  string str = ss.str();

  for (Move *m = pv; *m != MOVE_NONE; m++)
      str += move_to_uci(*m, pos.is_chess960()) + ' ';

  return str;
}
