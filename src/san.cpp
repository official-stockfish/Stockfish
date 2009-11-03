/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2009 Marco Costalba

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

#include "history.h"
#include "movepick.h"
#include "san.h"

using std::string;

////
//// Local definitions
////

namespace {

  enum Ambiguity {
    AMBIGUITY_NONE,
    AMBIGUITY_FILE,
    AMBIGUITY_RANK,
    AMBIGUITY_BOTH
  };

  const History H; // used as dummy argument for MovePicker c'tor

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

const string move_to_san(const Position& pos, Move m) {

  assert(pos.is_ok());
  assert(move_is_ok(m));

  Square from, to;
  PieceType pt;

  from = move_from(m);
  to = move_to(m);
  pt = type_of_piece(pos.piece_on(move_from(m)));

  string san = "";

  if (m == MOVE_NONE)
      return "(none)";
  else if (m == MOVE_NULL)
      return "(null)";
  else if (move_is_long_castle(m) || (int(to - from) == -2 && pt == KING))
      san = "O-O-O";
  else if (move_is_short_castle(m) || (int(to - from) == 2 && pt == KING))
      san = "O-O";
  else
  {
      if (pt != PAWN)
      {
          san += piece_type_to_char(pt, true);
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
              san += file_to_char(square_file(move_from(m)));
          san += "x";
      }
      san += square_to_string(move_to(m));
      if (move_is_promotion(m))
      {
          san += '=';
          san += piece_type_to_char(move_promotion_piece(m), true);
      }
  }
  // Is the move check?  We don't use pos.move_is_check(m) here, because
  // Position::move_is_check doesn't detect all checks (not castling moves,
  // promotions and en passant captures).
  StateInfo st;
  Position p(pos);
  p.do_move(m, st);
  if (p.is_check())
      san += p.is_mate()? "#" : "+";

  return san;
}


/// move_from_san() takes a position and a string as input, and tries to
/// interpret the string as a move in short algebraic notation. On success,
/// the move is returned.  On failure (i.e. if the string is unparsable, or
/// if the move is illegal or ambiguous), MOVE_NONE is returned.

Move move_from_san(const Position& pos, const string& movestr) {

  assert(pos.is_ok());

  MovePicker mp = MovePicker(pos, MOVE_NONE, OnePly, H);
  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());

  // Castling moves
  if (movestr == "O-O-O" || movestr == "O-O-O+")
  {
      Move m;
      while ((m = mp.get_next_move()) != MOVE_NONE)
          if (move_is_long_castle(m) && pos.pl_move_is_legal(m, pinned))
              return m;

      return MOVE_NONE;
  }
  else if (movestr == "O-O" || movestr == "O-O+")
  {
      Move m;
      while ((m = mp.get_next_move()) != MOVE_NONE)
          if (move_is_short_castle(m) && pos.pl_move_is_legal(m, pinned))
              return m;

    return MOVE_NONE;
  }

  // Normal moves. We use a simple FSM to parse the san string.
  enum { START, TO_FILE, TO_RANK, PROMOTION_OR_CHECK, PROMOTION, CHECK, END };
  static const string pieceLetters = "KQRBN";
  PieceType pt = NO_PIECE_TYPE, promotion = NO_PIECE_TYPE;
  File fromFile = FILE_NONE, toFile = FILE_NONE;
  Rank fromRank = RANK_NONE, toRank = RANK_NONE;
  Square to;
  int state = START;

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
              state = (i < movestr.length() - 1) ? CHECK : END;
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
      case 'x': case 'X':
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
      case '+': case '#':
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

  // Look for a matching move
  Move m, move = MOVE_NONE;
  to = make_square(toFile, toRank);
  int matches = 0;

  while ((m = mp.get_next_move()) != MOVE_NONE)
      if (   pos.type_of_piece_on(move_from(m)) == pt
          && move_to(m) == to
          && move_promotion_piece(m) == promotion
          && (fromFile == FILE_NONE || fromFile == square_file(move_from(m)))
          && (fromRank == RANK_NONE || fromRank == square_rank(move_from(m))))
      {
          move = m;
          matches++;
      }
  return (matches == 1 ? move : MOVE_NONE);
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
          p.do_null_move(st);
      else
          p.do_move(line[i], st);
  }
  return s.str();
}


/// pretty_pv() creates a human-readable string from a position and a PV.
/// It is used to write search information to the log file (which is created
/// when the UCI parameter "Use Search Log" is "true").

const string pretty_pv(const Position& pos, int time, int depth,
                       uint64_t nodes, Value score, ValueType type, Move pv[]) {
  std::stringstream s;

  // Depth
  s << std::setw(2) << depth << "  ";

  // Score
  s << ((type == VALUE_TYPE_LOWER)? ">" : ((type == VALUE_TYPE_UPPER)? "<" : " "));
  s << std::setw(7) << score_string(score);

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

    MovePicker mp = MovePicker(pos, MOVE_NONE, OnePly, H);
    Bitboard pinned = pos.pinned_pieces(pos.side_to_move());
    Move mv, moveList[8];

    int n = 0;
    while ((mv = mp.get_next_move()) != MOVE_NONE)
        if (move_to(mv) == to && pos.piece_on(move_from(mv)) == pc && pos.pl_move_is_legal(mv, pinned))
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


  const string time_string(int milliseconds) {

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


  const string score_string(Value v) {

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
