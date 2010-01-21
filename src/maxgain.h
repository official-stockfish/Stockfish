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


#if !defined(MAXGAIN_H_INCLUDED)
#define MAXGAIN_H_INCLUDED

////
//// Includes
////

#include "move.h"
#include "piece.h"
#include "value.h"


////
//// Types
////

class MaxGain {

public:
  MaxGain();
  void clear();
  void store(Piece p, Square from, Square to, Value prev, Value curr);
  Value retrieve(Piece p, Square from, Square to);

private:
  int maxStaticValueDelta[16][64][64];  // [piece][from_square][to_square]
};


#endif // !defined(MAXGAIN_H_INCLUDED)

