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


#if !defined(ENDGAME_H_INCLUDED)
#define ENDGAME_H_INCLUDED

////
//// Includes
////

#include "position.h"
#include "scale.h"
#include "value.h"


////
//// Types
////

enum EndgameType {

    // Evaluation functions
    KXK,   // Generic "mate lone king" eval
    KBNK,  // KBN vs K
    KPK,   // KP vs K
    KRKP,  // KR vs KP
    KRKB,  // KR vs KB
    KRKN,  // KR vs KN
    KQKR,  // KQ vs KR
    KBBKN, // KBB vs KN
    KNNK,  // KNN vs K
    KmmKm, // K and two minors vs K and one or two minors

    // Scaling functions
    KBPsK,   // KB+pawns vs K
    KQKRPs,  // KQ vs KR+pawns
    KRPKR,   // KRP vs KR
    KRPPKRP, // KRPP vs KRP
    KPsK,    // King and pawns vs king
    KBPKB,   // KBP vs KB
    KBPPKB,  // KBPP vs KB
    KBPKN,   // KBP vs KN
    KNPK,    // KNP vs K
    KPKP     // KP vs KP
};

/// Template abstract base class for all special endgame functions

template<typename T>
class EndgameFunctionBase {
public:
  EndgameFunctionBase(Color c) : strongerSide(c), weakerSide(opposite_color(c)) {}
  virtual ~EndgameFunctionBase() {}
  virtual T apply(const Position&) const = 0;
  Color color() const { return strongerSide; }

protected:
  Color strongerSide, weakerSide;
};

typedef EndgameFunctionBase<Value> EndgameEvaluationFunctionBase;
typedef EndgameFunctionBase<ScaleFactor> EndgameScalingFunctionBase;


/// Templates subclass for various concrete endgames

template<EndgameType>
struct EvaluationFunction : public EndgameEvaluationFunctionBase {
  typedef EndgameEvaluationFunctionBase Base;
  explicit EvaluationFunction(Color c): EndgameEvaluationFunctionBase(c) {}
  Value apply(const Position&) const;
};

template<EndgameType>
struct ScalingFunction : public EndgameScalingFunctionBase {
  typedef EndgameScalingFunctionBase Base;
  explicit ScalingFunction(Color c) : EndgameScalingFunctionBase(c) {}
  ScaleFactor apply(const Position&) const;
};


////
//// Prototypes
////

extern void init_bitbases();


#endif // !defined(ENDGAME_H_INCLUDED)
