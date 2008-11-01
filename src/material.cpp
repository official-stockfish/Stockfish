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


////
//// Includes
////

#include <cassert>
#include <map>

#include "material.h"


////
//// Local definitions
////

namespace {

  const Value BishopPairMidgameBonus = Value(100);
  const Value BishopPairEndgameBonus = Value(100);

  Key KNNKMaterialKey, KKNNMaterialKey;

}

////
//// Classes
////


/// See header for a class description. It is declared here to avoid
/// to include <map> in the header file.

class EndgameFunctions {

public:
  EndgameFunctions();
  EndgameEvaluationFunction* getEEF(Key key) const;
  ScalingFunction* getESF(Key key, Color* c) const;

private:
  void add(Key k, EndgameEvaluationFunction* f);
  void add(Key k, Color c, ScalingFunction* f);

  struct ScalingInfo
  {
      Color col;
      ScalingFunction* fun;
  };

  std::map<Key, EndgameEvaluationFunction*> EEFmap;
  std::map<Key, ScalingInfo> ESFmap;
};


////
//// Functions
////


/// Constructor for the MaterialInfoTable class

MaterialInfoTable::MaterialInfoTable(unsigned int numOfEntries) {

  size = numOfEntries;
  entries = new MaterialInfo[size];
  funcs = new EndgameFunctions();
  if (!entries || !funcs)
  {
      std::cerr << "Failed to allocate " << (numOfEntries * sizeof(MaterialInfo))
                << " bytes for material hash table." << std::endl;
      exit(EXIT_FAILURE);
  }
  clear();
}


/// Destructor for the MaterialInfoTable class

MaterialInfoTable::~MaterialInfoTable() {

  delete [] entries;
  delete funcs;
}


/// MaterialInfoTable::clear() clears a material hash table by setting
/// all entries to 0.

void MaterialInfoTable::clear() {

  memset(entries, 0, size * sizeof(MaterialInfo));
}


/// MaterialInfoTable::get_material_info() takes a position object as input,
/// computes or looks up a MaterialInfo object, and returns a pointer to it.
/// If the material configuration is not already present in the table, it
/// is stored there, so we don't have to recompute everything when the
/// same material configuration occurs again.

MaterialInfo* MaterialInfoTable::get_material_info(const Position& pos) {

  Key key = pos.get_material_key();
  int index = key & (size - 1);
  MaterialInfo* mi = entries + index;

  // If mi->key matches the position's material hash key, it means that we
  // have analysed this material configuration before, and we can simply
  // return the information we found the last time instead of recomputing it.
  if (mi->key == key)
      return mi;

  // Clear the MaterialInfo object, and set its key
  mi->clear();
  mi->key = key;

  // A special case before looking for a specialized evaluation function
  // KNN vs K is a draw.
  if (key == KNNKMaterialKey || key == KKNNMaterialKey)
  {
      mi->factor[WHITE] = mi->factor[BLACK] = 0;
      return mi;
  }

  // Let's look if we have a specialized evaluation function for this
  // particular material configuration.
  if ((mi->evaluationFunction = funcs->getEEF(key)) != NULL)
      return mi;

  else if (   pos.non_pawn_material(BLACK) == Value(0)
           && pos.piece_count(BLACK, PAWN) == 0
           && pos.non_pawn_material(WHITE) >= RookValueEndgame)
  {
      mi->evaluationFunction = &EvaluateKXK;
      return mi;
  }
  else if (   pos.non_pawn_material(WHITE) == Value(0)
           && pos.piece_count(WHITE, PAWN) == 0
           && pos.non_pawn_material(BLACK) >= RookValueEndgame)
  {
      mi->evaluationFunction = &EvaluateKKX;
      return mi;
  }

  // OK, we didn't find any special evaluation function for the current
  // material configuration. Is there a suitable scaling function?
  //
  // The code below is rather messy, and it could easily get worse later,
  // if we decide to add more special cases.  We face problems when there
  // are several conflicting applicable scaling functions and we need to
  // decide which one to use.
  Color c;
  ScalingFunction* sf;

  if ((sf = funcs->getESF(key, &c)) != NULL)
  {
      mi->scalingFunction[c] = sf;
      return mi;
  }

  if (   pos.non_pawn_material(WHITE) == BishopValueMidgame
      && pos.piece_count(WHITE, BISHOP) == 1
      && pos.piece_count(WHITE, PAWN) >= 1)
      mi->scalingFunction[WHITE] = &ScaleKBPK;

  if (   pos.non_pawn_material(BLACK) == BishopValueMidgame
      && pos.piece_count(BLACK, BISHOP) == 1
      && pos.piece_count(BLACK, PAWN) >= 1)
      mi->scalingFunction[BLACK] = &ScaleKKBP;

  if (   pos.piece_count(WHITE, PAWN) == 0
      && pos.non_pawn_material(WHITE) == QueenValueMidgame
      && pos.piece_count(WHITE, QUEEN) == 1
      && pos.piece_count(BLACK, ROOK) == 1
      && pos.piece_count(BLACK, PAWN) >= 1)
      mi->scalingFunction[WHITE] = &ScaleKQKRP;

  else if (   pos.piece_count(BLACK, PAWN) == 0
           && pos.non_pawn_material(BLACK) == QueenValueMidgame
           && pos.piece_count(BLACK, QUEEN) == 1
           && pos.piece_count(WHITE, ROOK) == 1
           && pos.piece_count(WHITE, PAWN) >= 1)
      mi->scalingFunction[BLACK] = &ScaleKRPKQ;

  if (pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) == Value(0))
  {
      if (pos.piece_count(BLACK, PAWN) == 0)
      {
          assert(pos.piece_count(WHITE, PAWN) >= 2);
          mi->scalingFunction[WHITE] = &ScaleKPsK;
      }
      else if (pos.piece_count(WHITE, PAWN) == 0)
      {
          assert(pos.piece_count(BLACK, PAWN) >= 2);
          mi->scalingFunction[BLACK] = &ScaleKKPs;
      }
      else if (pos.piece_count(WHITE, PAWN) == 1 && pos.piece_count(BLACK, PAWN) == 1)
      {
          mi->scalingFunction[WHITE] = &ScaleKPKPw;
          mi->scalingFunction[BLACK] = &ScaleKPKPb;
      }
  }

  // Evaluate the material balance

  int sign;
  Value egValue = Value(0);
  Value mgValue = Value(0);

  for (c = WHITE, sign = 1; c <= BLACK; c++, sign = -sign)
  {
    // No pawns makes it difficult to win, even with a material advantage
    if (   pos.piece_count(c, PAWN) == 0
        && pos.non_pawn_material(c) - pos.non_pawn_material(opposite_color(c)) <= BishopValueMidgame)
    {
        if (   pos.non_pawn_material(c) == pos.non_pawn_material(opposite_color(c))
            || pos.non_pawn_material(c) < RookValueMidgame)
            mi->factor[c] = 0;
        else
        {
            switch (pos.piece_count(c, BISHOP)) {
            case 2:
                mi->factor[c] = 32;
                break;
            case 1:
                mi->factor[c] = 12;
                break;
            case 0:
                mi->factor[c] = 6;
                break;
            }
        }
    }

    // Bishop pair
    if (pos.piece_count(c, BISHOP) >= 2)
    {
        mgValue += sign * BishopPairMidgameBonus;
        egValue += sign * BishopPairEndgameBonus;
    }

    // Knights are stronger when there are many pawns on the board.  The
    // formula is taken from Larry Kaufman's paper "The Evaluation of Material
    // Imbalances in Chess":
    // http://mywebpages.comcast.net/danheisman/Articles/evaluation_of_material_imbalance.htm
    mgValue += sign * Value(pos.piece_count(c, KNIGHT)*(pos.piece_count(c, PAWN)-5)*16);
    egValue += sign * Value(pos.piece_count(c, KNIGHT)*(pos.piece_count(c, PAWN)-5)*16);

    // Redundancy of major pieces, again based on Kaufman's paper:
    if (pos.piece_count(c, ROOK) >= 1)
    {
        Value v = Value((pos.piece_count(c, ROOK) - 1) * 32 + pos.piece_count(c, QUEEN) * 16);
        mgValue -= sign * v;
        egValue -= sign * v;
    }
  }
  mi->mgValue = int16_t(mgValue);
  mi->egValue = int16_t(egValue);
  return mi;
}


/// EndgameFunctions member definitions. This class is used to store the maps
/// of end game and scaling functions that MaterialInfoTable will query for 
/// each key. The maps are constant and are populated only at construction,
/// but are per-thread instead of globals to avoid expensive locks.

EndgameFunctions::EndgameFunctions() {

  typedef Key ZM[2][8][16];
  const ZM& z = Position::zobMaterial;

  static const Color W = WHITE;
  static const Color B = BLACK;

  KNNKMaterialKey = z[W][KNIGHT][1] ^ z[W][KNIGHT][2];
  KKNNMaterialKey = z[B][KNIGHT][1] ^ z[B][KNIGHT][2];

  add(z[W][PAWN][1], &EvaluateKPK);
  add(z[B][PAWN][1], &EvaluateKKP);

  add(z[W][BISHOP][1] ^ z[W][KNIGHT][1], &EvaluateKBNK);
  add(z[B][BISHOP][1] ^ z[B][KNIGHT][1], &EvaluateKKBN);
  add(z[W][ROOK][1]   ^ z[B][PAWN][1],   &EvaluateKRKP);
  add(z[W][PAWN][1]   ^ z[B][ROOK][1],   &EvaluateKPKR);
  add(z[W][ROOK][1]   ^ z[B][BISHOP][1], &EvaluateKRKB);
  add(z[W][BISHOP][1] ^ z[B][ROOK][1],   &EvaluateKBKR);
  add(z[W][ROOK][1]   ^ z[B][KNIGHT][1], &EvaluateKRKN);
  add(z[W][KNIGHT][1] ^ z[B][ROOK][1],   &EvaluateKNKR);
  add(z[W][QUEEN][1]  ^ z[B][ROOK][1],   &EvaluateKQKR);
  add(z[W][ROOK][1]   ^ z[B][QUEEN][1],  &EvaluateKRKQ);

  add(z[W][KNIGHT][1] ^ z[W][PAWN][1], W, &ScaleKNPK);
  add(z[B][KNIGHT][1] ^ z[B][PAWN][1], B, &ScaleKKNP);

  add(z[W][ROOK][1]   ^ z[W][PAWN][1]   ^ z[B][ROOK][1]  , W, &ScaleKRPKR);
  add(z[W][ROOK][1]   ^ z[B][ROOK][1]   ^ z[B][PAWN][1]  , B, &ScaleKRKRP);
  add(z[W][BISHOP][1] ^ z[W][PAWN][1]   ^ z[B][BISHOP][1], W, &ScaleKBPKB);
  add(z[W][BISHOP][1] ^ z[B][BISHOP][1] ^ z[B][PAWN][1]  , B, &ScaleKBKBP);
  add(z[W][BISHOP][1] ^ z[W][PAWN][1]   ^ z[B][KNIGHT][1], W, &ScaleKBPKN);
  add(z[W][KNIGHT][1] ^ z[B][BISHOP][1] ^ z[B][PAWN][1]  , B, &ScaleKNKBP);

  add(z[W][ROOK][1] ^ z[W][PAWN][1] ^ z[W][PAWN][2] ^ z[B][ROOK][1] ^ z[B][PAWN][1], W, &ScaleKRPPKRP);
  add(z[W][ROOK][1] ^ z[W][PAWN][1] ^ z[B][ROOK][1] ^ z[B][PAWN][1] ^ z[B][PAWN][2], B, &ScaleKRPKRPP);
}

void EndgameFunctions::add(Key k, EndgameEvaluationFunction* f) {

  EEFmap.insert(std::pair<Key, EndgameEvaluationFunction*>(k, f));
}

void EndgameFunctions::add(Key k, Color c, ScalingFunction* f) {

  ScalingInfo s = {c, f};
  ESFmap.insert(std::pair<Key, ScalingInfo>(k, s));
}

EndgameEvaluationFunction* EndgameFunctions::getEEF(Key key) const {

  std::map<Key, EndgameEvaluationFunction*>::const_iterator it(EEFmap.find(key));
  return (it != EEFmap.end() ? it->second : NULL);
}

ScalingFunction* EndgameFunctions::getESF(Key key, Color* c) const {

  std::map<Key, ScalingInfo>::const_iterator it(ESFmap.find(key));
  if (it == ESFmap.end())
      return NULL;

  *c = it->second.col;
  return it->second.fun;
}
