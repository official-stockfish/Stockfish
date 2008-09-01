/*
  Glaurung, a UCI chess playing engine.
  Copyright (C) 2004-2008 Tord Romstad

  Glaurung is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  Glaurung is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#if !defined(VALUE_H_INCLUDED)
#define VALUE_H_INCLUDED

////
//// Includes
////

#include "piece.h"


////
//// Types
////

enum ValueType {
  VALUE_TYPE_NONE = 0,
  VALUE_TYPE_UPPER = 1,  // Upper bound
  VALUE_TYPE_LOWER = 2,  // Lower bound
  VALUE_TYPE_EXACT = 3   // Exact score
};


enum Value {
  VALUE_DRAW = 0,
  VALUE_KNOWN_WIN = 15000,
  VALUE_MATE = 30000,
  VALUE_INFINITE = 30001,
  VALUE_NONE = 30002
};


////
//// Constants and variables
////

/// Piece values, middle game and endgame

/// Important: If the material values are changed, one must also
/// adjust the piece square tables, and the method game_phase() in the
/// Position class!

const Value PawnValueMidgame = Value(0xCC);
const Value PawnValueEndgame = Value(0x100);
const Value KnightValueMidgame = Value(0x340);
const Value KnightValueEndgame = Value(0x340);
const Value BishopValueMidgame = Value(0x340);
const Value BishopValueEndgame = Value(0x340);
const Value RookValueMidgame = Value(0x505);
const Value RookValueEndgame = Value(0x505);
const Value QueenValueMidgame = Value(0xA00);
const Value QueenValueEndgame = Value(0xA00);

const Value PieceValueMidgame[17] = {
  Value(0),
  PawnValueMidgame, KnightValueMidgame, BishopValueMidgame,
  RookValueMidgame, QueenValueMidgame,
  Value(0), Value(0), Value(0),
  PawnValueMidgame, KnightValueMidgame, BishopValueMidgame,
  RookValueMidgame, QueenValueMidgame,
  Value(0), Value(0), Value(0)
};

const Value PieceValueEndgame[17] = {
  Value(0),
  PawnValueEndgame, KnightValueEndgame, BishopValueEndgame,
  RookValueEndgame, QueenValueEndgame,
  Value(0), Value(0), Value(0),
  PawnValueEndgame, KnightValueEndgame, BishopValueEndgame,
  RookValueEndgame, QueenValueEndgame,
  Value(0), Value(0), Value(0)
};

/// Bonus for having the side to move

const Value TempoValueMidgame = Value(50);
const Value TempoValueEndgame = Value(20);


////
//// Inline functions
////

inline Value operator+ (Value v, int i) { return Value(int(v) + i); }
inline Value operator+ (Value v1, Value v2) { return Value(int(v1) + int(v2)); }
inline void operator+= (Value &v1, Value v2) {
  v1 = Value(int(v1) + int(v2));
}
inline Value operator- (Value v, int i) { return Value(int(v) - i); }
inline Value operator- (Value v) { return Value(-int(v)); }
inline Value operator- (Value v1, Value v2) { return Value(int(v1) - int(v2)); }
inline void operator-= (Value &v1, Value v2) {
  v1 = Value(int(v1) - int(v2));
}
inline Value operator* (Value v, int i) { return Value(int(v) * i); }
inline void operator*= (Value &v, int i) { v = Value(int(v) * i); }
inline Value operator* (int i, Value v) { return Value(int(v) * i); }
inline Value operator/ (Value v, int i) { return Value(int(v) / i); }
inline void operator/= (Value &v, int i) { v = Value(int(v) / i); }


inline Value value_mate_in(int ply) {
  return Value(VALUE_MATE - Value(ply));
}
               
inline Value value_mated_in(int ply) {
  return Value(-VALUE_MATE + Value(ply));
}

inline bool is_upper_bound(ValueType vt) {
  return (int(vt) & int(VALUE_TYPE_UPPER)) != 0;
}

inline bool is_lower_bound(ValueType vt) {
  return (int(vt) & int(VALUE_TYPE_LOWER)) != 0;
}

inline Value piece_value_midgame(PieceType pt) {
  return PieceValueMidgame[pt];
}

inline Value piece_value_endgame(PieceType pt) {
  return PieceValueEndgame[pt];
}

inline Value piece_value_midgame(Piece p) {
  return PieceValueMidgame[p];
}

inline Value piece_value_endgame(Piece p) {
  return PieceValueEndgame[p];
}


////
//// Prototypes
////

extern Value value_to_tt(Value v, int ply);
extern Value value_from_tt(Value v, int ply);
extern int value_to_centipawns(Value v);
extern Value value_from_centipawns(int cp);
extern const std::string value_to_string(Value v);


#endif // !defined(VALUE_H_INCLUDED)
