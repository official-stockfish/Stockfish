/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2014 Marco Costalba, Joona Kiiski, Tord Romstad

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

#ifndef ENDGAME_H_INCLUDED
#define ENDGAME_H_INCLUDED

#include <map>
#include <string>

#include "position.h"
#include "types.h"


/// EndgameType lists all supported endgames

enum EndgameType {

  // Evaluation functions

  KNNK,  // KNN vs K
  KXK,   // Generic "mate lone king" eval
  KBNK,  // KBN vs K
  KPK,   // KP vs K
  KRKP,  // KR vs KP
  KRKB,  // KR vs KB
  KRKN,  // KR vs KN
  KQKP,  // KQ vs KP
  KQKR,  // KQ vs KR


  // Scaling functions
  SCALE_FUNS,

  KBPsK,   // KB and pawns vs K
  KQKRPs,  // KQ vs KR and pawns
  KRPKR,   // KRP vs KR
  KRPKB,   // KRP vs KB
  KRPPKRP, // KRPP vs KRP
  KPsK,    // K and pawns vs K
  KBPKB,   // KBP vs KB
  KBPPKB,  // KBPP vs KB
  KBPKN,   // KBP vs KN
  KNPK,    // KNP vs K
  KNPKB,   // KNP vs KB
  KPKP     // KP vs KP
};


/// Endgame functions can be of two types depending on whether they return a
/// Value or a ScaleFactor. Type eg_fun<int>::type returns either ScaleFactor
/// or Value depending on whether the template parameter is 0 or 1.

template<int> struct eg_fun { typedef Value type; };
template<> struct eg_fun<1> { typedef ScaleFactor type; };


/// Base and derived templates for endgame evaluation and scaling functions

template<typename T>
struct EndgameBase {

  virtual ~EndgameBase() {}
  virtual Color color() const = 0;
  virtual T operator()(const Position&) const = 0;
};


template<EndgameType E, typename T = typename eg_fun<(E > SCALE_FUNS)>::type>
struct Endgame : public EndgameBase<T> {

  explicit Endgame(Color c) : strongSide(c), weakSide(~c) {}
  Color color() const { return strongSide; }
  T operator()(const Position&) const;

private:
  const Color strongSide, weakSide;
};


/// The Endgames class stores the pointers to endgame evaluation and scaling
/// base objects in two std::map typedefs. We then use polymorphism to invoke
/// the actual endgame function by calling its virtual operator().

class Endgames {

  typedef std::map<Key, EndgameBase<eg_fun<0>::type>*> M1;
  typedef std::map<Key, EndgameBase<eg_fun<1>::type>*> M2;

  M1 m1;
  M2 m2;

  M1& map(M1::mapped_type) { return m1; }
  M2& map(M2::mapped_type) { return m2; }

  template<EndgameType E> void add(const std::string& code);

public:
  Endgames();
 ~Endgames();

  template<typename T> T probe(Key key, T& eg)
  { return eg = map(eg).count(key) ? map(eg)[key] : NULL; }
};

#endif // #ifndef ENDGAME_H_INCLUDED
