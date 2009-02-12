/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008 Marco Costalba

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

/// Abstract base class for all special endgame evaluation functions

class EndgameEvaluationFunction {
public:
  EndgameEvaluationFunction(Color c);
  virtual ~EndgameEvaluationFunction() { }

  virtual Value apply(const Position &pos) = 0;

protected:
  Color strongerSide, weakerSide;
};


/// Template subclass for various concrete endgames

enum EndgameType {
    KXK,   // Generic "mate lone king" eval
    KBNK,  // KBN vs K
    KPK,   // KP vs K
    KRKP,  // KR vs KP
    KRKB,  // KR vs KB
    KRKN,  // KR vs KN
    KQKR,  // KQ vs KR
    KBBKN, // KBB vs KN
    KmmKm  // K and two minors vs K and one or two minors
};

template<EndgameType>
class EvaluationFunction : public EndgameEvaluationFunction {
public:
  explicit EvaluationFunction(Color c): EndgameEvaluationFunction(c) {}
  Value apply(const Position& pos);
};

/// Abstract base class for all evaluation scaling functions:

class ScalingFunction {
public:
  ScalingFunction(Color c);
  virtual ~ScalingFunction() { }

  virtual ScaleFactor apply(const Position &pos) =0;

protected:
  Color strongerSide, weakerSide;
};


/// Subclasses for various concrete endgames:

// KBP vs K:
class KBPKScalingFunction : public ScalingFunction {
public:
  KBPKScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
};

// KQ vs KRP:
class KQKRPScalingFunction: public ScalingFunction {
public:
  KQKRPScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
};

// KRP vs KR:
class KRPKRScalingFunction : public ScalingFunction {
public:
  KRPKRScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
};

// KRPP vs KRP:
class KRPPKRPScalingFunction : public ScalingFunction {
public:
  KRPPKRPScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
};

// King and pawns vs king:
class KPsKScalingFunction : public ScalingFunction {
public:
  KPsKScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
};

// KBP vs KB:
class KBPKBScalingFunction : public ScalingFunction {
public:
  KBPKBScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
};

// KBP vs KN:
class KBPKNScalingFunction : public ScalingFunction {
public:
  KBPKNScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
};

// KNP vs K:
class KNPKScalingFunction : public ScalingFunction {
public:
  KNPKScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
};

// KP vs KP:
class KPKPScalingFunction : public ScalingFunction {
public:
  KPKPScalingFunction(Color c);
  ScaleFactor apply(const Position &pos);
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

// KBP vs K:
extern KBPKScalingFunction ScaleKBPK, ScaleKKBP;

// KQ vs KRP:
extern KQKRPScalingFunction ScaleKQKRP, ScaleKRPKQ;

// KRP vs KR:
extern KRPKRScalingFunction ScaleKRPKR, ScaleKRKRP;

// KRPP vs KRP:
extern KRPPKRPScalingFunction ScaleKRPPKRP, ScaleKRPKRPP;

// King and pawns vs king:
extern KPsKScalingFunction ScaleKPsK, ScaleKKPs;

// KBP vs KB:
extern KBPKBScalingFunction ScaleKBPKB, ScaleKBKBP;

// KBP vs KN:
extern KBPKNScalingFunction ScaleKBPKN, ScaleKNKBP;

// KNP vs K:
extern KNPKScalingFunction ScaleKNPK, ScaleKKNP;

// KP vs KP:
extern KPKPScalingFunction ScaleKPKPw, ScaleKPKPb;


////
//// Prototypes
////

extern void init_bitbases();


#endif // !defined(ENDGAME_H_INCLUDED)
