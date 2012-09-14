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

#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>

#include "movegen.h"
#include "notation.h"
#include "position.h"

using namespace std;

static const char* PieceToChar = " PNBRQK  pnbrqk";


/// score_to_uci() converts a value to a string suitable for use with the UCI
/// protocol specifications:
///
/// cp <x>     The score from the engine's point of view in centipawns.
/// mate <y>   Mate in y moves, not plies. If the engine is getting mated
///            use negative values for y.

string score_to_uci(Value v, Value alpha, Value beta) {

  stringstream s;

  if (abs(v) < VALUE_MATE_IN_MAX_PLY)
      s << "cp " << v * 100 / int(PawnValueMg);
  else
      s << "mate " << (v > 0 ? VALUE_MATE - v + 1 : -VALUE_MATE - v) / 2;

  s << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

  return s.str();
}


/// move_to_uci() converts a move to a string in coordinate notation
/// (g1f3, a7a8q, etc.). The only special case is castling moves, where we print
/// in the e1g1 notation in normal chess mode, and in e1h1 notation in chess960
/// mode. Internally castle moves are always coded as "king captures rook".

const string move_to_uci(Move m, bool chess960) {

  Square from = from_sq(m);
  Square to = to_sq(m);

  if (m == MOVE_NONE)
      return "(none)";

  if (m == MOVE_NULL)
      return "0000";

  if (type_of(m) == CASTLE && !chess960)
      to = (to > from ? FILE_G : FILE_C) | rank_of(from);

  string move = square_to_string(from) + square_to_string(to);

  if (type_of(m) == PROMOTION)
      move += PieceToChar[make_piece(BLACK, promotion_type(m))]; // Lower case

  return move;
}


/// move_from_uci() takes a position and a string representing a move in
/// simple coordinate notation and returns an equivalent legal Move if any.

Move move_from_uci(const Position& pos, string& str) {

  if (str.length() == 5) // Junior could send promotion piece in uppercase
      str[4] = char(tolower(str[4]));

  for (MoveList<LEGAL> ml(pos); !ml.end(); ++ml)
      if (str == move_to_uci(ml.move(), pos.is_chess960()))
          return ml.move();

  return MOVE_NONE;
}


/// move_to_san() takes a position and a legal Move as input and returns its
/// short algebraic notation representation.

const string move_to_san(Position& pos, Move m) {

  if (m == MOVE_NONE)
      return "(none)";

  if (m == MOVE_NULL)
      return "(null)";

  assert(pos.move_is_legal(m));

  Bitboard attackers;
  bool ambiguousMove, ambiguousFile, ambiguousRank;
  string san;
  Color us = pos.side_to_move();
  Square from = from_sq(m);
  Square to = to_sq(m);
  Piece pc = pos.piece_on(from);
  PieceType pt = type_of(pc);

  if (type_of(m) == CASTLE)
      san = to > from ? "O-O" : "O-O-O";
  else
  {
      if (pt != PAWN)
      {
          san = PieceToChar[pt]; // Upper case

          // Disambiguation if we have more then one piece with destination 'to'
          // note that for pawns is not needed because starting file is explicit.
          ambiguousMove = ambiguousFile = ambiguousRank = false;

          attackers = (pos.attacks_from(pc, to) & pos.pieces(us, pt)) ^ from;

          while (attackers)
          {
              Square sq = pop_lsb(&attackers);

              // Pinned pieces are not included in the possible sub-set
              if (!pos.pl_move_is_legal(make_move(sq, to), pos.pinned_pieces()))
                  continue;

              ambiguousFile |= file_of(sq) == file_of(from);
              ambiguousRank |= rank_of(sq) == rank_of(from);
              ambiguousMove = true;
          }

          if (ambiguousMove)
          {
              if (!ambiguousFile)
                  san += file_to_char(file_of(from));

              else if (!ambiguousRank)
                  san += rank_to_char(rank_of(from));

              else
                  san += square_to_string(from);
          }
      }
      else if (pos.is_capture(m))
          san = file_to_char(file_of(from));

      if (pos.is_capture(m))
          san += 'x';

      san += square_to_string(to);

      if (type_of(m) == PROMOTION)
          san += string("=") + PieceToChar[promotion_type(m)];
  }

  if (pos.move_gives_check(m, CheckInfo(pos)))
  {
      StateInfo st;
      pos.do_move(m, st);
      san += MoveList<LEGAL>(pos).size() ? "+" : "#";
      pos.undo_move(m);
  }

  return san;
}


/// pretty_pv() formats human-readable search information, typically to be
/// appended to the search log file. It uses the two helpers below to pretty
/// format time and score respectively.

static string time_to_string(int64_t msecs) {

  const int MSecMinute = 1000 * 60;
  const int MSecHour   = 1000 * 60 * 60;

  int64_t hours   =   msecs / MSecHour;
  int64_t minutes =  (msecs % MSecHour) / MSecMinute;
  int64_t seconds = ((msecs % MSecHour) % MSecMinute) / 1000;

  stringstream s;

  if (hours)
      s << hours << ':';

  s << setfill('0') << setw(2) << minutes << ':' << setw(2) << seconds;

  return s.str();
}

static string score_to_string(Value v) {

  stringstream s;

  if (v >= VALUE_MATE_IN_MAX_PLY)
      s << "#" << (VALUE_MATE - v + 1) / 2;

  else if (v <= VALUE_MATED_IN_MAX_PLY)
      s << "-#" << (VALUE_MATE + v) / 2;

  else
      s << setprecision(2) << fixed << showpos << float(v) / PawnValueMg;

  return s.str();
}

string pretty_pv(Position& pos, int depth, Value value, int64_t msecs, Move pv[]) {

  const int64_t K = 1000;
  const int64_t M = 1000000;

  StateInfo state[MAX_PLY_PLUS_2], *st = state;
  Move* m = pv;
  string san, padding;
  size_t length;
  stringstream s;

  s << setw(2) << depth
    << setw(8) << score_to_string(value)
    << setw(8) << time_to_string(msecs);

  if (pos.nodes_searched() < M)
      s << setw(8) << pos.nodes_searched() / 1 << "  ";

  else if (pos.nodes_searched() < K * M)
      s << setw(7) << pos.nodes_searched() / K << "K  ";

  else
      s << setw(7) << pos.nodes_searched() / M << "M  ";

  padding = string(s.str().length(), ' ');
  length = padding.length();

  while (*m != MOVE_NONE)
  {
      san = move_to_san(pos, *m);

      if (length + san.length() > 80)
      {
          s << "\n" + padding;
          length = padding.length();
      }

      s << san << ' ';
      length += san.length() + 1;

      pos.do_move(*m++, *st++);
  }

  while (m != pv)
      pos.undo_move(*--m);

  return s.str();
}
