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
    KmmKm, // K and two minors vs K and one or two minors

    // Scaling functions
    KBPK,    // KBP vs K
    KQKRP,   // KQ vs KRP
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
  EndgameFunctionBase(Color c) : strongerSide(c) { weakerSide = opposite_color(strongerSide); }
  virtual ~EndgameFunctionBase() {}
  virtual T apply(const Position&) = 0;

protected:
  Color strongerSide, weakerSide;
};

typedef EndgameFunctionBase<Value> EndgameEvaluationFunctionBase;
typedef EndgameFunctionBase<ScaleFactor> EndgameScalingFunctionBase;


/// Templates subclass for various concrete endgames

template<EndgameType>
struct EvaluationFunction : public EndgameEvaluationFunctionBase {
  explicit EvaluationFunction(Color c): EndgameEvaluationFunctionBase(c) {}
  Value apply(const Position&);
};

template<EndgameType>
struct ScalingFunction : public EndgameScalingFunctionBase {
  explicit ScalingFunction(Color c) : EndgameScalingFunctionBase(c) {}
  ScaleFactor apply(const Position&);
};


////
//// Constants and variables
////

extern EvaluationFunction<KXK> EvaluateKXK, EvaluateKKX;       // Generic "mate lone king" eval
extern EvaluationFunction<KBNK> EvaluateKBNK, EvaluateKKBN;    // KBN vs K
extern EvaluationFunction<KPK> EvaluateKPK, EvaluateKKP;       // KP vs K
extern EvaluationFunction<KRKP> EvaluateKRKP, EvaluateKPKR;    // KR vs KP
extern EvaluationFunction<KRKB> EvaluateKRKB, EvaluateKBKR;    // KR vs KB
extern EvaluationFunction<KRKN> EvaluateKRKN, EvaluateKNKR;    // KR vs KN
extern EvaluationFunction<KQKR> EvaluateKQKR, EvaluateKRKQ;    // KQ vs KR
extern EvaluationFunction<KBBKN> EvaluateKBBKN, EvaluateKNKBB; // KBB vs KN
extern EvaluationFunction<KmmKm> EvaluateKmmKm; // K and two minors vs K and one or two minors:

extern ScalingFunction<KBPK> ScaleKBPK, ScaleKKBP;    // KBP vs K
extern ScalingFunction<KQKRP> ScaleKQKRP, ScaleKRPKQ; // KQ vs KRP
extern ScalingFunction<KRPKR> ScaleKRPKR, ScaleKRKRP; // KRP vs KR
extern ScalingFunction<KRPPKRP> ScaleKRPPKRP, ScaleKRPKRPP; // KRPP vs KRP
extern ScalingFunction<KPsK> ScaleKPsK, ScaleKKPs;    // King and pawns vs king
extern ScalingFunction<KBPKB> ScaleKBPKB, ScaleKBKBP; // KBP vs KB
extern ScalingFunction<KBPPKB> ScaleKBPPKB, ScaleKBKBPP; // KBPP vs KB
extern ScalingFunction<KBPKN> ScaleKBPKN, ScaleKNKBP; // KBP vs KN
extern ScalingFunction<KNPK> ScaleKNPK, ScaleKKNP;    // KNP vs K
extern ScalingFunction<KPKP> ScaleKPKPw, ScaleKPKPb;  // KP vs KP

////
//// Prototypes
////

extern void init_bitbases();


#endif // !defined(ENDGAME_H_INCLUDED)
