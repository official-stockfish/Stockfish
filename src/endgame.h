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

#if !defined(ENDGAME_H_INCLUDED)
#define ENDGAME_H_INCLUDED

#include <map>
#include <string>

#include "position.h"
#include "types.h"


/// EndgameType lists all supported endgames

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
  SCALE_FUNS,

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


/// Some magic to detect family type of endgame from its enum value

template<bool> struct bool_to_type { typedef Value type; };
template<> struct bool_to_type<true> { typedef ScaleFactor type; };
template<EndgameType E> struct eg_family : public bool_to_type<(E > SCALE_FUNS)> {};


/// Base and derived templates for endgame evaluation and scaling functions

template<typename T>
struct EndgameBase {

  virtual ~EndgameBase() {}
  virtual Color color() const = 0;
  virtual T operator()(const Position&) const = 0;
};


template<EndgameType E, typename T = typename eg_family<E>::type>
struct Endgame : public EndgameBase<T> {

  explicit Endgame(Color c) : strongerSide(c), weakerSide(flip(c)) {}
  Color color() const { return strongerSide; }
  T operator()(const Position&) const;

private:
  Color strongerSide, weakerSide;
};


/// Endgames class stores in two std::map the pointers to endgame evaluation
/// and scaling base objects. Then we use polymorphism to invoke the actual
/// endgame function calling its operator() method that is virtual.

class Endgames {

  typedef std::map<Key, EndgameBase<Value>*> M1;
  typedef std::map<Key, EndgameBase<ScaleFactor>*> M2;

  M1 m1;
  M2 m2;

  M1& map(Value*) { return m1; }
  M2& map(ScaleFactor*) { return m2; }

  template<EndgameType E> void add(const std::string& code);

public:
  Endgames();
  ~Endgames();

  template<typename T> EndgameBase<T>* get(Key key) {
    return map((T*)0).count(key) ? map((T*)0)[key] : NULL;
  }
};

#endif // !defined(ENDGAME_H_INCLUDED)
