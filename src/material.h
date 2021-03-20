/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2021 The Stockfish developers (see AUTHORS file)

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

#ifndef MATERIAL_H_INCLUDED
#define MATERIAL_H_INCLUDED

#include "endgame.h"
#include "misc.h"
#include "position.h"
#include "types.h"

namespace Stockfish::Material {

/// Material::Entry contains various information about a material configuration.
/// It contains a material imbalance evaluation, a function pointer to a special
/// endgame evaluation function (which in most cases is NULL, meaning that the
/// standard evaluation function will be used), and scale factors.
///
/// The scale factors are used to scale the evaluation score up or down. For
/// instance, in KRB vs KR endgames, the score is scaled down by a factor of 4,
/// which will result in scores of absolute value less than one pawn.

struct Entry {

  Score imbalance() const { return score; }
  Phase game_phase() const { return (Phase)gamePhase; }
  bool specialized_eval_exists() const { return evaluationFunction != nullptr; }
  Value evaluate(const Position& pos) const { return (*evaluationFunction)(pos); }

  // scale_factor() takes a position and a color as input and returns a scale factor
  // for the given color. We have to provide the position in addition to the color
  // because the scale factor may also be a function which should be applied to
  // the position. For instance, in KBP vs K endgames, the scaling function looks
  // for rook pawns and wrong-colored bishops.
  ScaleFactor scale_factor(const Position& pos, Color c) const {
    ScaleFactor sf = scalingFunction[c] ? (*scalingFunction[c])(pos)
                                        :  SCALE_FACTOR_NONE;
    return sf != SCALE_FACTOR_NONE ? sf : ScaleFactor(factor[c]);
  }

  Key key;
  const EndgameBase<Value>* evaluationFunction;
  const EndgameBase<ScaleFactor>* scalingFunction[COLOR_NB]; // Could be one for each
                                                             // side (e.g. KPKP, KBPsK)
  Score score;
  int16_t gamePhase;
  uint8_t factor[COLOR_NB];
};

typedef HashTable<Entry, 8192> Table;

Entry* probe(const Position& pos);

} // namespace Stockfish::Material

#endif // #ifndef MATERIAL_H_INCLUDED
