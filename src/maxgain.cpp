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

#include "maxgain.h"
#include "value.h"

////
//// Functions
////


/// Constructor

MaxGain::MaxGain() { clear(); }


/// MaxGain::clear() clears the table

void MaxGain::clear() {
  memset(maxStaticValueDelta, 0, 16 * 64 * 64 * sizeof(int));
}


/// MaxGain::store

void MaxGain::store(Piece p, Square from, Square to, Value prev, Value curr)
{
  if (prev == VALUE_NONE || curr == VALUE_NONE)
    return;

  Value delta = curr - prev;
  if (delta >= maxStaticValueDelta[p][from][to])
    maxStaticValueDelta[p][from][to] = delta;
  else
    maxStaticValueDelta[p][from][to]--;
}

// MaxGain::retrieve

Value MaxGain::retrieve(Piece p, Square from, Square to)
{
  return (Value) maxStaticValueDelta[p][from][to];
}
