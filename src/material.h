/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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

#if !defined(MATERIAL_H_INCLUDED)
#define MATERIAL_H_INCLUDED

#include "endgame.h"
#include "misc.h"
#include "position.h"
#include "types.h"

namespace Material {

/// Material::Entry contains various information about a material configuration.
/// It contains a material balance evaluation, a function pointer to a special
/// endgame evaluation function (which in most cases is NULL, meaning that the
/// standard evaluation function will be used), and "scale factors".
///
/// The scale factors are used to scale the evaluation score up or down.
/// For instance, in KRB vs KR endgames, the score is scaled down by a factor
/// of 4, which will result in scores of absolute value less than one pawn.

struct Entry {

  Score material_value() const { return make_score(value, value); }
  Score space_weight() const { return spaceWeight; }
  Phase game_phase() const { return gamePhase; }
  bool specialized_eval_exists() const { return evaluationFunction != NULL; }
  Value evaluate(const Position& p) const { return (*evaluationFunction)(p); }
  ScaleFactor scale_factor(const Position& pos, Color c) const;

  Key key;
  int16_t value;
  uint8_t factor[COLOR_NB];
  EndgameBase<Value>* evaluationFunction;
  EndgameBase<ScaleFactor>* scalingFunction[COLOR_NB];
  Score spaceWeight;
  Phase gamePhase;
};

typedef HashTable<Entry, 8192> Table;

Entry* probe(const Position& pos, Table& entries, Endgames& endgames);
Phase game_phase(const Position& pos);

/// Material::scale_factor takes a position and a color as input, and
/// returns a scale factor for the given color. We have to provide the
/// position in addition to the color, because the scale factor need not
/// to be a constant: It can also be a function which should be applied to
/// the position. For instance, in KBP vs K endgames, a scaling function
/// which checks for draws with rook pawns and wrong-colored bishops.

inline ScaleFactor Entry::scale_factor(const Position& pos, Color c) const {

  return !scalingFunction[c] || (*scalingFunction[c])(pos) == SCALE_FACTOR_NONE
        ? ScaleFactor(factor[c]) : (*scalingFunction[c])(pos);
}

}

#endif // !defined(MATERIAL_H_INCLUDED)
