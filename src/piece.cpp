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

#include <string>

#include "piece.h"

using namespace std;

////
//// Functions
////

/// Translating piece types to/from English piece letters

static const string PieceChars(" pnbrqk PNBRQK");

char piece_type_to_char(PieceType pt, bool upcase) {

  return PieceChars[pt + int(upcase) * 7];
}

PieceType piece_type_from_char(char c) {

  size_t idx = PieceChars.find(c);

  return idx != string::npos ? PieceType(idx % 7) : NO_PIECE_TYPE;
}
