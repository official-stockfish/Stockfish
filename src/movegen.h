/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

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

#ifndef MOVEGEN_H_INCLUDED
#define MOVEGEN_H_INCLUDED

#include <algorithm>  // IWYU pragma: keep
#include <cstddef>

#include "types.h"

namespace Stockfish {

class Position;

enum GenType {
    CAPTURES,
    QUIETS,
    EVASIONS,
    NON_EVASIONS,
    LEGAL
};

struct ExtMove: public Move {
    int value;

    void operator=(Move m) { data = m.raw(); }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const = delete;
};

inline bool operator<(const ExtMove& f, const ExtMove& s) { return f.value < s.value; }

template<GenType>
Move* generate(const Position& pos, Move* moveList, Bitboard threats);

// The MoveList struct wraps the generate() function and returns a convenient
// list of moves. Using MoveList is sometimes preferable to directly calling
// the lower level generate() function.
template<GenType T>
struct MoveList {

    explicit MoveList(const Position& pos, Bitboard threats = 0ULL) :
        last(generate<T>(pos, moveList, threats)) {}
    const Move* begin() const { return moveList; }
    const Move* end() const { return last; }
    size_t      size() const { return last - moveList; }
    bool        contains(Move move) const { return std::find(begin(), end(), move) != end(); }

   private:
    Move moveList[MAX_MOVES], *last;
};

}  // namespace Stockfish

#endif  // #ifndef MOVEGEN_H_INCLUDED
