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

#include <string>
#include <map>

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


/// Base and derived templates for endgame evaluation and scaling functions

template<typename T>
struct EndgameBase {

  typedef EndgameBase<T> Base;

  virtual ~EndgameBase() {}
  virtual Color color() const = 0;
  virtual T apply(const Position&) const = 0;
};


template<typename T, EndgameType>
struct Endgame : public EndgameBase<T> {

  explicit Endgame(Color c) : strongerSide(c), weakerSide(opposite_color(c)) {}
  Color color() const { return strongerSide; }
  T apply(const Position&) const;

private:
  Color strongerSide, weakerSide;
};


/// Endgames class stores in two std::map the pointers to endgame evaluation
/// and scaling base objects. Then we use polymorphism to invoke the actual
/// endgame function calling its apply() method that is virtual.

class Endgames {

  typedef std::map<Key, EndgameBase<Value>* > EFMap;
  typedef std::map<Key, EndgameBase<ScaleFactor>* > SFMap;

public:
  Endgames();
  ~Endgames();
  template<class T> T* get(Key key) const;

private:
  template<class T> void add(const std::string& keyCode);

  // Here we store two maps, for evaluate and scaling functions...
  std::pair<EFMap, SFMap> maps;

  // ...and here is the accessing template function
  template<typename T> const std::map<Key, T*>& get() const;
};

#endif // !defined(ENDGAME_H_INCLUDED)
