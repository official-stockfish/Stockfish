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

#if !defined(MATERIAL_H_INCLUDED)
#define MATERIAL_H_INCLUDED

#include "endgame.h"
#include "position.h"
#include "tt.h"
#include "types.h"

const int MaterialTableSize = 8192;

/// Game phase
enum Phase {
  PHASE_ENDGAME = 0,
  PHASE_MIDGAME = 128
};


/// MaterialInfo is a class which contains various information about a
/// material configuration. It contains a material balance evaluation,
/// a function pointer to a special endgame evaluation function (which in
/// most cases is NULL, meaning that the standard evaluation function will
/// be used), and "scale factors" for black and white.
///
/// The scale factors are used to scale the evaluation score up or down.
/// For instance, in KRB vs KR endgames, the score is scaled down by a factor
/// of 4, which will result in scores of absolute value less than one pawn.

class MaterialInfo {

  friend class MaterialInfoTable;

public:
  Score material_value() const;
  ScaleFactor scale_factor(const Position& pos, Color c) const;
  int space_weight() const;
  Phase game_phase() const;
  bool specialized_eval_exists() const;
  Value evaluate(const Position& pos) const;

private:
  Key key;
  int16_t value;
  uint8_t factor[2];
  EndgameBase<Value>* evaluationFunction;
  EndgameBase<ScaleFactor>* scalingFunction[2];
  int spaceWeight;
  Phase gamePhase;
};


/// The MaterialInfoTable class represents a pawn hash table. The most important
/// method is material_info(), which returns a pointer to a MaterialInfo object.

class MaterialInfoTable : public SimpleHash<MaterialInfo, MaterialTableSize> {
public:
  MaterialInfoTable() : funcs(new Endgames()) {}
  ~MaterialInfoTable() { delete funcs; }

  MaterialInfo* material_info(const Position& pos) const;
  static Phase game_phase(const Position& pos);

private:
  template<Color Us>
  static int imbalance(const int pieceCount[][8]);

  Endgames* funcs;
};


/// MaterialInfo::scale_factor takes a position and a color as input, and
/// returns a scale factor for the given color. We have to provide the
/// position in addition to the color, because the scale factor need not
/// to be a constant: It can also be a function which should be applied to
/// the position. For instance, in KBP vs K endgames, a scaling function
/// which checks for draws with rook pawns and wrong-colored bishops.

inline ScaleFactor MaterialInfo::scale_factor(const Position& pos, Color c) const {

  if (!scalingFunction[c])
      return ScaleFactor(factor[c]);

  ScaleFactor sf = (*scalingFunction[c])(pos);
  return sf == SCALE_FACTOR_NONE ? ScaleFactor(factor[c]) : sf;
}

inline Value MaterialInfo::evaluate(const Position& pos) const {
  return (*evaluationFunction)(pos);
}

inline Score MaterialInfo::material_value() const {
  return make_score(value, value);
}

inline int MaterialInfo::space_weight() const {
  return spaceWeight;
}

inline Phase MaterialInfo::game_phase() const {
  return gamePhase;
}

inline bool MaterialInfo::specialized_eval_exists() const {
  return evaluationFunction != NULL;
}

#endif // !defined(MATERIAL_H_INCLUDED)
