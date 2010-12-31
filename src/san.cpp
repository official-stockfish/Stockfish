/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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

#include "movegen.h"
#include "san.h"

using std::string;

////
//// Local definitions
////

namespace {

  enum Ambiguity {
    AMBIGUITY_NONE, AMBIGUITY_FILE, AMBIGUITY_RANK, AMBIGUITY_BOTH
  };

  Ambiguity move_ambiguity(const Position& pos, Move m);
  const string time_string(int milliseconds);
  const string score_string(Value v);
}


////
//// Functions
////

/// move_to_san() takes a position and a move as input, where it is assumed
/// that the move is a legal move from the position. The return value is
/// a string containing the move in short algebraic notation.

const string move_to_san(Position& pos, Move m) {

  assert(pos.is_ok());
  assert(move_is_ok(m));

  string san;
  Square from = move_from(m);
  Square to = move_to(m);
  PieceType pt = type_of_piece(pos.piece_on(from));

  if (m == MOVE_NONE)
      return "(none)";

  if (m == MOVE_NULL)
      return "(null)";

  if (move_is_long_castle(m))
      san = "O-O-O";
  else if (move_is_short_castle(m))
      san = "O-O";
  else
  {
      if (pt != PAWN)
      {
          san += piece_type_to_char(pt);

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
          if (pt == PAWN)
              san += file_to_char(square_file(from));

          san += 'x';
      }
      san += square_to_string(to);

      if (move_is_promotion(m))
      {
          san += '=';
          san += piece_type_to_char(move_promotion_piece(m));
      }
  }

  // The move gives check ? We don't use pos.move_is_check() here
  // because we need to test for mate after the move is done.
  StateInfo st;
  pos.do_move(m, st);
  if (pos.is_check())
      san += pos.is_mate() ? "#" : "+";
  pos.undo_move(m);

  return san;
}


/// move_from_san() takes a position and a string as input, and tries to
/// interpret the string as a move in short algebraic notation. On success,
/// the move is returned.  On failure (i.e. if the string is unparsable, or
/// if the move is illegal or ambiguous), MOVE_NONE is returned.

Move move_from_san(const Position& pos, const string& movestr) {

  assert(pos.is_ok());

  enum { START, TO_FILE, TO_RANK, PROMOTION_OR_CHECK, PROMOTION, CHECK, END };
  static const string pieceLetters = "KQRBN";

  MoveStack mlist[MOVES_MAX], *last;
  PieceType pt = PIECE_TYPE_NONE, promotion = PIECE_TYPE_NONE;
  File fromFile = FILE_NONE, toFile = FILE_NONE;
  Rank fromRank = RANK_NONE, toRank = RANK_NONE;
  Move move = MOVE_NONE;
  Square from, to;
  int matches, state = START;

  // Generate all legal moves for the given position
  last = generate_moves(pos, mlist);

  // Castling moves
  if (movestr == "O-O-O" || movestr == "O-O-O+")
  {
     for (MoveStack* cur = mlist; cur != last; cur++)
          if (move_is_long_castle(cur->move))
              return cur->move;

      return MOVE_NONE;
  }
  else if (movestr == "O-O" || movestr == "O-O+")
  {
      for (MoveStack* cur = mlist; cur != last; cur++)
           if (move_is_short_castle(cur->move))
               return cur->move;

    return MOVE_NONE;
  }

  // Normal moves. We use a simple FSM to parse the san string
  for (size_t i = 0; i < movestr.length(); i++)
  {
      char type, c = movestr[i];

      if (pieceLetters.find(c) != string::npos)
          type = 'P';
      else if (c >= 'a' && c <= 'h')
          type = 'F';
      else if (c >= '1' && c <= '8')
          type = 'R';
      else
          type = c;

      switch (type) {
      case 'P':
          if (state == START)
          {
              pt = piece_type_from_char(c);
              state = TO_FILE;
          }
          else if (state == PROMOTION)
          {
              promotion = piece_type_from_char(c);
              state = (i < movestr.length() - 1 ? CHECK : END);
          }
          else
              return MOVE_NONE;
          break;
      case 'F':
          if (state == START)
          {
              pt = PAWN;
              fromFile = toFile = file_from_char(c);
              state = TO_RANK;
          }
          else if (state == TO_FILE)
          {
              toFile = file_from_char(c);
              state = TO_RANK;
          }
          else if (state == TO_RANK && toFile != FILE_NONE)
          {
              // Previous file was for disambiguation
              fromFile = toFile;
              toFile = file_from_char(c);
          }
          else
              return MOVE_NONE;
          break;
      case 'R':
          if (state == TO_RANK)
          {
              toRank = rank_from_char(c);
              state = (i < movestr.length() - 1) ? PROMOTION_OR_CHECK : END;
          }
          else if (state == TO_FILE && fromRank == RANK_NONE)
          {
              // It's a disambiguation rank instead of a file
              fromRank = rank_from_char(c);
          }
          else
              return MOVE_NONE;
          break;
      case 'x':
      case 'X':
          if (state == TO_RANK)
          {
              // Previous file was for disambiguation, or it's a pawn capture
              fromFile = toFile;
              state = TO_FILE;
          }
          else if (state != TO_FILE)
              return MOVE_NONE;
          break;
      case '=':
          if (state == PROMOTION_OR_CHECK)
              state = PROMOTION;
          else
              return MOVE_NONE;
          break;
      case '+':
      case '#':
          if (state == PROMOTION_OR_CHECK || state == CHECK)
              state = END;
          else
              return MOVE_NONE;
          break;
      default:
          return MOVE_NONE;
          break;
      }
  }

  if (state != END)
      return MOVE_NONE;

  // Look for an unambiguous matching move
  to = make_square(toFile, toRank);
  matches = 0;

  for (MoveStack* cur = mlist; cur != last; cur++)
  {
      from = move_from(cur->move);

      if (   pos.type_of_piece_on(from) == pt
          && move_to(cur->move) == to
          && move_promotion_piece(cur->move) == promotion
          && (fromFile == FILE_NONE || fromFile == square_file(from))
          && (fromRank == RANK_NONE || fromRank == square_rank(from)))
      {
          move = cur->move;
          matches++;
      }
  }
  return matches == 1 ? move : MOVE_NONE;
}


/// line_to_san() takes a position and a line (an array of moves representing
/// a sequence of legal moves from the position) as input, and returns a
/// string containing the line in short algebraic notation.  If the boolean
/// parameter 'breakLines' is true, line breaks are inserted, with a line
/// length of 80 characters.  After a line break, 'startColumn' spaces are
/// inserted at the beginning of the new line.

const string line_to_san(const Position& pos, Move line[], int startColumn, bool breakLines) {

  StateInfo st;
  std::stringstream s;
  string moveStr;
  size_t length = 0;
  size_t maxLength = 80 - startColumn;
  Position p(pos, pos.thread());

  for (Move* m = line; *m != MOVE_NONE; m++)
  {
      moveStr = move_to_san(p, *m);
      length += moveStr.length() + 1;
      if (breakLines && length > maxLength)
      {
          s << "\n" << std::setw(startColumn) << " ";
          length = moveStr.length() + 1;
      }
      s << moveStr << ' ';

      if (*m == MOVE_NULL)
          p.do_null_move(st);
      else
          p.do_move(*m, st);
  }
  return s.str();
}


/// pretty_pv() creates a human-readable string from a position and a PV.
/// It is used to write search information to the log file (which is created
/// when the UCI parameter "Use Search Log" is "true").

const string pretty_pv(const Position& pos, int time, int depth,
                       Value score, ValueType type, Move pv[]) {

  const int64_t K = 1000;
  const int64_t M = 1000000;

  std::stringstream s;

  // Depth
  s << std::setw(2) << depth << "  ";

  // Score
  s << (type == VALUE_TYPE_LOWER ? ">" : type == VALUE_TYPE_UPPER ? "<" : " ")
    << std::setw(7) << score_string(score);

  // Time
  s << std::setw(8) << time_string(time) << " ";

  // Nodes
  if (pos.nodes_searched() < M)
      s << std::setw(8) << pos.nodes_searched() / 1 << " ";
  else if (pos.nodes_searched() < K * M)
      s << std::setw(7) << pos.nodes_searched() / K << "K ";
  else
      s << std::setw(7) << pos.nodes_searched() / M << "M ";

  // PV
  s << line_to_san(pos, pv, 30, true);

  return s.str();
}


namespace {

  Ambiguity move_ambiguity(const Position& pos, Move m) {

    MoveStack mlist[MOVES_MAX], *last;
    Move candidates[8];
    Square from = move_from(m);
    Square to = move_to(m);
    Piece pc = pos.piece_on(from);
    int matches = 0, f = 0, r = 0;

    // If there is only one piece 'pc' then move cannot be ambiguous
    if (pos.piece_count(pos.side_to_move(), type_of_piece(pc)) == 1)
        return AMBIGUITY_NONE;

    // Collect all legal moves of piece 'pc' with destination 'to'
    last = generate_moves(pos, mlist);
    for (MoveStack* cur = mlist; cur != last; cur++)
        if (move_to(cur->move) == to && pos.piece_on(move_from(cur->move)) == pc)
            candidates[matches++] = cur->move;

    if (matches == 1)
        return AMBIGUITY_NONE;

    for (int i = 0; i < matches; i++)
    {
        if (square_file(move_from(candidates[i])) == square_file(from))
            f++;

        if (square_rank(move_from(candidates[i])) == square_rank(from))
            r++;
    }

    return f == 1 ? AMBIGUITY_FILE : r == 1 ? AMBIGUITY_RANK : AMBIGUITY_BOTH;
  }


  const string time_string(int millisecs) {

    const int MSecMinute = 1000 * 60;
    const int MSecHour   = 1000 * 60 * 60;

    std::stringstream s;
    s << std::setfill('0');

    int hours = millisecs / MSecHour;
    int minutes = (millisecs - hours * MSecHour) / MSecMinute;
    int seconds = (millisecs - hours * MSecHour - minutes * MSecMinute) / 1000;

    if (hours)
        s << hours << ':';

    s << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
    return s.str();
  }


  const string score_string(Value v) {

    std::stringstream s;

    if (v >= VALUE_MATE - 200)
        s << "#" << (VALUE_MATE - v + 1) / 2;
    else if (v <= -VALUE_MATE + 200)
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
