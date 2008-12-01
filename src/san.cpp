/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008 Marco Costalba

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

#include <cassert>
#include <cstring>
#include <iomanip>
#include <string>
#include <sstream>

#include "movepick.h"
#include "san.h"

extern SearchStack EmptySearchStack;

////
//// Local definitions
////

namespace {

  /// Types

  enum Ambiguity {
    AMBIGUITY_NONE,
    AMBIGUITY_FILE,
    AMBIGUITY_RANK,
    AMBIGUITY_BOTH
  };


  /// Functions

  Ambiguity move_ambiguity(const Position& pos, Move m);
  const std::string time_string(int milliseconds);
  const std::string score_string(Value v);
}


////
//// Functions
////

/// move_to_san() takes a position and a move as input, where it is assumed
/// that the move is a legal move from the position. The return value is
/// a string containing the move in short algebraic notation.

const std::string move_to_san(const Position& pos, Move m) {

  assert(pos.is_ok());
  assert(move_is_ok(m));

  std::string san = "";

  if (m == MOVE_NONE)
      return "(none)";
  else if (m == MOVE_NULL)
      return "(null)";
  else if (move_is_long_castle(m))
      san = "O-O-O";
  else if (move_is_short_castle(m))
      san = "O-O";
  else
  {
      Piece pc = pos.piece_on(move_from(m));
      if (type_of_piece(pc) != PAWN)
      {
          san += piece_type_to_char(type_of_piece(pc), true);
          Square from = move_from(m);
          switch (move_ambiguity(pos, m)) {
          case AMBIGUITY_NONE:
            break;
          case AMBIGUITY_FILE:
            san += file_to_char(square_file(from));
            break;
          case AMBIGUITY_RANK:
            san += rank_to_char(square_rank(from));
            break;
          case AMBIGUITY_BOTH:
            san += square_to_string(from);
            break;
          default:
            assert(false);
          }
      }
      if (pos.move_is_capture(m))
      {
          if (type_of_piece(pc) == PAWN)
              san += file_to_char(square_file(move_from(m)));
          san += "x";
      }
      san += square_to_string(move_to(m));
      if (move_promotion(m))
      {
          san += '=';
          san += piece_type_to_char(move_promotion(m), true);
      }
  }
  // Is the move check?  We don't use pos.move_is_check(m) here, because
  // Position::move_is_check doesn't detect all checks (not castling moves,
  // promotions and en passant captures).
  UndoInfo u;
  Position p(pos);
  p.do_move(m, u);
  if (p.is_check())
      san += p.is_mate()? "#" : "+";

  return san;
}


/// move_from_san() takes a position and a string as input, and tries to
/// interpret the string as a move in short algebraic notation. On success,
/// the move is returned.  On failure (i.e. if the string is unparsable, or
/// if the move is illegal or ambiguous), MOVE_NONE is returned.

Move move_from_san(const Position& pos, const std::string& movestr) {

  assert(pos.is_ok());

  MovePicker mp = MovePicker(pos, false, MOVE_NONE, EmptySearchStack, OnePly);

  // Castling moves
  if (movestr == "O-O-O")
  {
      Move m;
      while ((m = mp.get_next_move()) != MOVE_NONE)
          if (move_is_long_castle(m) && pos.pl_move_is_legal(m))
              return m;

      return MOVE_NONE;
  }
  else if (movestr == "O-O")
  {
      Move m;
      while ((m = mp.get_next_move()) != MOVE_NONE)
          if (move_is_short_castle(m) && pos.pl_move_is_legal(m))
              return m;

    return MOVE_NONE;
  }

  // Normal moves
  const char *cstr = movestr.c_str();
  const char *c;
  char *cc;
  char str[10];
  int i;

  // Initialize str[] by making a copy of movestr with the characters
  // 'x', '=', '+' and '#' removed.
  cc = str;
  for(i=0, c=cstr; i<10 && *c!='\0' && *c!='\n' && *c!=' '; i++, c++)
    if(!strchr("x=+#", *c)) {
      *cc = strchr("nrq", *c)? toupper(*c) : *c;
      cc++;
    }
  *cc = '\0';

  size_t left = 0, right = strlen(str) - 1;
  PieceType pt = NO_PIECE_TYPE, promotion;
  Square to;
  File fromFile = FILE_NONE;
  Rank fromRank = RANK_NONE;

  // Promotion?
  if(strchr("BNRQ", str[right])) {
    promotion = piece_type_from_char(str[right]);
    right--;
  }
  else
    promotion = NO_PIECE_TYPE;

  // Find the moving piece:
  if(left < right) {
    if(strchr("BNRQK", str[left])) {
      pt = piece_type_from_char(str[left]);
      left++;
    }
    else
      pt = PAWN;
  }

  // Find the to square:
  if(left < right) {
    if(str[right] < '1' || str[right] > '8' ||
       str[right-1] < 'a' || str[right-1] > 'h')
      return MOVE_NONE;
    to = make_square(file_from_char(str[right-1]), rank_from_char(str[right]));
    right -= 2;
  }
  else
    return MOVE_NONE;

  // Find the file and/or rank of the from square:
  if(left <= right) {
    if(strchr("abcdefgh", str[left])) {
      fromFile = file_from_char(str[left]);
      left++;
    }
    if(strchr("12345678", str[left]))
      fromRank = rank_from_char(str[left]);
  }

  // Look for a matching move:
  Move m, move = MOVE_NONE;
  int matches = 0;

  while((m = mp.get_next_move()) != MOVE_NONE) {
    bool match = true;
    if(pos.type_of_piece_on(move_from(m)) != pt)
      match = false;
    else if(move_to(m) != to)
      match = false;
    else if(move_promotion(m) != promotion)
      match = false;
    else if(fromFile != FILE_NONE && fromFile != square_file(move_from(m)))
      match = false;
    else if(fromRank != RANK_NONE && fromRank != square_rank(move_from(m)))
      match = false;
    if(match) {
      move = m;
      matches++;
    }
  }

  if(matches == 1)
    return move;
  else
    return MOVE_NONE;
}


/// line_to_san() takes a position and a line (an array of moves representing
/// a sequence of legal moves from the position) as input, and returns a
/// string containing the line in short algebraic notation.  If the boolean
/// parameter 'breakLines' is true, line breaks are inserted, with a line
/// length of 80 characters.  After a line break, 'startColumn' spaces are
/// inserted at the beginning of the new line.

const std::string line_to_san(const Position& pos, Move line[], int startColumn, bool breakLines) {

  UndoInfo u;
  std::stringstream s;
  std::string moveStr;
  size_t length = 0;
  size_t maxLength = 80 - startColumn;
  Position p(pos);

  for (int i = 0; line[i] != MOVE_NONE; i++)
  {
      moveStr = move_to_san(p, line[i]);
      length += moveStr.length() + 1;
      if (breakLines && length > maxLength)
      {
          s << '\n' << std::setw(startColumn) << ' ';
          length = moveStr.length() + 1;
      }
      s << moveStr << ' ';

      if (line[i] == MOVE_NULL)
          p.do_null_move(u);
      else
          p.do_move(line[i], u);
  }
  return s.str();
}


/// pretty_pv() creates a human-readable string from a position and a PV.
/// It is used to write search information to the log file (which is created
/// when the UCI parameter "Use Search Log" is "true").

const std::string pretty_pv(const Position& pos, int time, int depth,
                            uint64_t nodes, Value score, Move pv[]) {
  std::stringstream s;

  // Depth
  s << std::setw(2) << depth << "  ";

  // Score
  s << std::setw(8) << score_string(score);

  // Time
  s << std::setw(8) << time_string(time) << " ";

  // Nodes
  if (nodes < 1000000ULL)
    s << std::setw(8) << nodes << " ";
  else if (nodes < 1000000000ULL)
    s << std::setw(7) << nodes/1000ULL << 'k' << " ";
  else
    s << std::setw(7) << nodes/1000000ULL << 'M' << " ";

  // PV
  s << line_to_san(pos, pv, 30, true);

  return s.str();
}


namespace {

  Ambiguity move_ambiguity(const Position& pos, Move m) {

    Square from = move_from(m);
    Square to = move_to(m);
    Piece pc = pos.piece_on(from);

    // King moves are never ambiguous, because there is never two kings of
    // the same color.
    if (type_of_piece(pc) == KING)
        return AMBIGUITY_NONE;

    MovePicker mp = MovePicker(pos, false, MOVE_NONE, EmptySearchStack, OnePly);
    Move mv, moveList[8];

    int n = 0;
    while ((mv = mp.get_next_move()) != MOVE_NONE)
        if (move_to(mv) == to && pos.piece_on(move_from(mv)) == pc && pos.pl_move_is_legal(mv))
            moveList[n++] = mv;

    if (n == 1)
        return AMBIGUITY_NONE;

    int f = 0, r = 0;
    for (int i = 0; i < n; i++)
    {
        if (square_file(move_from(moveList[i])) == square_file(from))
            f++;

        if (square_rank(move_from(moveList[i])) == square_rank(from))
            r++;
    }
    if (f == 1)
        return AMBIGUITY_FILE;

    if (r == 1)
        return AMBIGUITY_RANK;

    return AMBIGUITY_BOTH;
  }


  const std::string time_string(int milliseconds) {

    std::stringstream s;
    s << std::setfill('0');

    int hours = milliseconds / (1000*60*60);
    int minutes = (milliseconds - hours*1000*60*60) / (1000*60);
    int seconds = (milliseconds - hours*1000*60*60 - minutes*1000*60) / 1000;

    if (hours)
        s << hours << ':';

    s << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return s.str();
  }


  const std::string score_string(Value v) {

    std::stringstream s;

    if (v >= VALUE_MATE - 200)
        s << "#" << (VALUE_MATE - v + 1) / 2;
    else if(v <= -VALUE_MATE + 200)
        s << "-#" << (VALUE_MATE + v) / 2;
    else
    {
        float floatScore = float(v) / float(PawnValueMidgame);
        if (v >= 0)
            s << '+';

        s << std::setprecision(2) << std::fixed << floatScore;
    }
    return s.str();
  }
}
