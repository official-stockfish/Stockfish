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

/// Abstract base class for all special endgame evaluation functions:

class EndgameEvaluationFunction {
public:
  EndgameEvaluationFunction(Color c);
  virtual ~EndgameEvaluationFunction() { }

  virtual Value apply(const Position &pos) =0;

protected:
  Color strongerSide, weakerSide;
};


/// Subclasses for various concrete endgames:

// Generic "mate lone king" eval:
class KXKEvaluationFunction : public EndgameEvaluationFunction {
public:
  KXKEvaluationFunction(Color c);
  Value apply(const Position &pos);
};

// KBN vs K:
class KBNKEvaluationFunction : public EndgameEvaluationFunction {
public:
  KBNKEvaluationFunction(Color c);
  Value apply(const Position &pos);
};

// KP vs K:
class KPKEvaluationFunction : public EndgameEvaluationFunction {
public:
  KPKEvaluationFunction(Color c);
  Value apply(const Position &pos);
};

// KR vs KP:
class KRKPEvaluationFunction : public EndgameEvaluationFunction {
public:
  KRKPEvaluationFunction(Color c);
  Value apply(const Position &pos);
};

// KR vs KB:
class KRKBEvaluationFunction : public EndgameEvaluationFunction {
public:
  KRKBEvaluationFunction(Color c);
  Value apply(const Position &pos);
};

// KR vs KN:
class KRKNEvaluationFunction : public EndgameEvaluationFunction {
public:
  KRKNEvaluationFunction(Color c);
  Value apply(const Position &pos);
};

// KQ vs KR:
class KQKREvaluationFunction : public EndgameEvaluationFunction {
public:
  KQKREvaluationFunction(Color c);
  Value apply(const Position &pos);
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

// Generic "mate lone king" eval:
extern KXKEvaluationFunction EvaluateKXK, EvaluateKKX;

// KBN vs K:
extern KBNKEvaluationFunction EvaluateKBNK, EvaluateKKBN;

// KP vs K:
extern KPKEvaluationFunction EvaluateKPK, EvaluateKKP;

// KR vs KP:
extern KRKPEvaluationFunction EvaluateKRKP, EvaluateKPKR;

// KR vs KB:
extern KRKBEvaluationFunction EvaluateKRKB, EvaluateKBKR;

// KR vs KN:
extern KRKNEvaluationFunction EvaluateKRKN, EvaluateKNKR;

// KQ vs KR:
extern KQKREvaluationFunction EvaluateKQKR, EvaluateKRKQ;

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
