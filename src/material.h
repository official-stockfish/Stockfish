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


#if !defined(MATERIAL_H_INCLUDED)
#define MATERIAL_H_INCLUDED

////
//// Includes
////

#include "endgame.h"
#include "position.h"
#include "scale.h"


////
//// Types
////

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
  MaterialInfo() : key(0) { clear(); }

  Score material_value() const;
  ScaleFactor scale_factor(const Position& pos, Color c) const;
  int space_weight() const;
  Phase game_phase() const;
  bool specialized_eval_exists() const;
  Value evaluate(const Position& pos) const;

private:
  inline void clear();

  Key key;
  int16_t value;
  uint8_t factor[2];
  EndgameEvaluationFunctionBase* evaluationFunction;
  EndgameScalingFunctionBase* scalingFunction[2];
  int spaceWeight;
  Phase gamePhase;
};

/// The MaterialInfoTable class represents a pawn hash table. It is basically
/// just an array of MaterialInfo objects and a few methods for accessing these
/// objects. The most important method is get_material_info, which looks up a
/// position in the table and returns a pointer to a MaterialInfo object.
class EndgameFunctions;

class MaterialInfoTable {

public:
  MaterialInfoTable(unsigned numOfEntries);
  ~MaterialInfoTable();
  MaterialInfo* get_material_info(const Position& pos);

  static Phase game_phase(const Position& pos);

private:
  unsigned size;
  MaterialInfo* entries;
  EndgameFunctions* funcs;
};


////
//// Inline functions
////


/// MaterialInfo::material_value simply returns the material balance
/// evaluation that is independent from game phase.

inline Score MaterialInfo::material_value() const {

  return make_score(value, value);
}


/// MaterialInfo::clear() resets a MaterialInfo object to an empty state,
/// with all slots at their default values but the key.

inline void MaterialInfo::clear() {

  value = 0;
  factor[WHITE] = factor[BLACK] = uint8_t(SCALE_FACTOR_NORMAL);
  evaluationFunction = NULL;
  scalingFunction[WHITE] = scalingFunction[BLACK] = NULL;
  spaceWeight = 0;
}


/// MaterialInfo::scale_factor takes a position and a color as input, and
/// returns a scale factor for the given color. We have to provide the
/// position in addition to the color, because the scale factor need not
/// to be a constant: It can also be a function which should be applied to
/// the position. For instance, in KBP vs K endgames, a scaling function
/// which checks for draws with rook pawns and wrong-colored bishops.

inline ScaleFactor MaterialInfo::scale_factor(const Position& pos, Color c) const {

  if (scalingFunction[c] != NULL)
  {
      ScaleFactor sf = scalingFunction[c]->apply(pos);
      if (sf != SCALE_FACTOR_NONE)
          return sf;
  }
  return ScaleFactor(factor[c]);
}


/// MaterialInfo::space_weight() simply returns the weight for the space
/// evaluation for this material configuration.

inline int MaterialInfo::space_weight() const {

  return spaceWeight;
}

/// MaterialInfo::game_phase() returns the game phase according
/// to this material configuration.

inline Phase MaterialInfo::game_phase() const {

  return gamePhase;
}


/// MaterialInfo::specialized_eval_exists decides whether there is a
/// specialized evaluation function for the current material configuration,
/// or if the normal evaluation function should be used.

inline bool MaterialInfo::specialized_eval_exists() const {

  return evaluationFunction != NULL;
}


/// MaterialInfo::evaluate applies a specialized evaluation function
/// to a given position object. It should only be called when
/// specialized_eval_exists() returns 'true'.

inline Value MaterialInfo::evaluate(const Position& pos) const {

  return evaluationFunction->apply(pos);
}

#endif // !defined(MATERIAL_H_INCLUDED)
