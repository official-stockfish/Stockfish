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
#include <string>

#include "movegen.h"
#include "position.h"

using std::string;

/// move_to_uci() converts a move to a string in coordinate notation
/// (g1f3, a7a8q, etc.). The only special case is castling moves, where we
/// print in the e1g1 notation in normal chess mode, and in e1h1 notation in
/// Chess960 mode. Instead internally Move is coded as "king captures rook".

const string move_to_uci(Move m, bool chess960) {

  Square from = from_sq(m);
  Square to = to_sq(m);
  string promotion;

  if (m == MOVE_NONE)
      return "(none)";

  if (m == MOVE_NULL)
      return "0000";

  if (is_castle(m) && !chess960)
      to = from + (file_of(to) == FILE_H ? Square(2) : -Square(2));

  if (is_promotion(m))
      promotion = char(tolower(piece_type_to_char(promotion_type(m))));

  return square_to_string(from) + square_to_string(to) + promotion;
}


/// move_from_uci() takes a position and a string representing a move in
/// simple coordinate notation and returns an equivalent Move if any.
/// Moves are guaranteed to be legal.

Move move_from_uci(const Position& pos, const string& str) {

  for (MoveList<MV_LEGAL> ml(pos); !ml.end(); ++ml)
      if (str == move_to_uci(ml.move(), pos.is_chess960()))
          return ml.move();

  return MOVE_NONE;
}


/// move_to_san() takes a position and a move as input, where it is assumed
/// that the move is a legal move for the position. The return value is
/// a string containing the move in short algebraic notation.

const string move_to_san(Position& pos, Move m) {

  if (m == MOVE_NONE)
      return "(none)";

  if (m == MOVE_NULL)
      return "(null)";

  assert(is_ok(m));

  Bitboard attackers;
  bool ambiguousMove, ambiguousFile, ambiguousRank;
  Square sq, from = from_sq(m);
  Square to = to_sq(m);
  PieceType pt = type_of(pos.piece_moved(m));
  string san;

  if (is_castle(m))
      san = (to_sq(m) < from_sq(m) ? "O-O-O" : "O-O");
  else
  {
      if (pt != PAWN)
      {
          san = piece_type_to_char(pt);

          // Disambiguation if we have more then one piece with destination 'to'
          // note that for pawns is not needed because starting file is explicit.
          attackers = pos.attackers_to(to) & pos.pieces(pos.side_to_move(), pt);
          attackers ^= from;
          ambiguousMove = ambiguousFile = ambiguousRank = false;

          while (attackers)
          {
              sq = pop_1st_bit(&attackers);

              // Pinned pieces are not included in the possible sub-set
              if (!pos.pl_move_is_legal(make_move(sq, to), pos.pinned_pieces()))
                  continue;

              if (file_of(sq) == file_of(from))
                  ambiguousFile = true;

              if (rank_of(sq) == rank_of(from))
                  ambiguousRank = true;

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

      if (pos.is_capture(m))
      {
          if (pt == PAWN)
              san += file_to_char(file_of(from));

          san += 'x';
      }

      san += square_to_string(to);

      if (is_promotion(m))
      {
          san += '=';
          san += piece_type_to_char(promotion_type(m));
      }
  }

  if (pos.move_gives_check(m, CheckInfo(pos)))
  {
      StateInfo st;
      pos.do_move(m, st);
      san += MoveList<MV_LEGAL>(pos).size() ? "+" : "#";
      pos.undo_move(m);
  }

  return san;
}
