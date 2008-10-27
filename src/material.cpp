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

  Key KRPKRMaterialKey, KRKRPMaterialKey;
  Key KNNKMaterialKey,  KKNNMaterialKey;
  Key KBPKBMaterialKey, KBKBPMaterialKey;
  Key KBPKNMaterialKey, KNKBPMaterialKey;
  Key KNPKMaterialKey,  KKNPMaterialKey;
  Key KPKPMaterialKey;
  Key KRPPKRPMaterialKey, KRPKRPPMaterialKey;

  std::map<Key, EndgameEvaluationFunction*> EEFmap;

  void EEFAdd(Key k, EndgameEvaluationFunction* f) {

      EEFmap.insert(std::pair<Key, EndgameEvaluationFunction*>(k, f));
  }
}


////
//// Functions
////

/// MaterialInfo::init() is called during program initialization. It
/// precomputes material hash keys for a few basic endgames, in order
/// to make it easy to recognize such endgames when they occur.

void MaterialInfo::init() {

  typedef Key ZM[2][8][16];
  const ZM& z = Position::zobMaterial;

  static const Color W = WHITE;
  static const Color B = BLACK;
  
  EEFAdd(z[W][PAWN][1], &EvaluateKPK);
  EEFAdd(z[B][PAWN][1], &EvaluateKKP);

  EEFAdd(z[W][BISHOP][1] ^ z[W][KNIGHT][1], &EvaluateKBNK);
  EEFAdd(z[B][BISHOP][1] ^ z[B][KNIGHT][1], &EvaluateKKBN);
  EEFAdd(z[W][ROOK][1]   ^ z[B][PAWN][1],   &EvaluateKRKP);
  EEFAdd(z[W][PAWN][1]   ^ z[B][ROOK][1],   &EvaluateKPKR);
  EEFAdd(z[W][ROOK][1]   ^ z[B][BISHOP][1], &EvaluateKRKB);
  EEFAdd(z[W][BISHOP][1] ^ z[B][ROOK][1],   &EvaluateKBKR);
  EEFAdd(z[W][ROOK][1]   ^ z[B][KNIGHT][1], &EvaluateKRKN);
  EEFAdd(z[W][KNIGHT][1] ^ z[B][ROOK][1],   &EvaluateKNKR);
  EEFAdd(z[W][QUEEN][1]  ^ z[B][ROOK][1],   &EvaluateKQKR);
  EEFAdd(z[W][ROOK][1]   ^ z[B][QUEEN][1],  &EvaluateKRKQ);

  KRPKRMaterialKey = z[W][ROOK][1]
                   ^ z[W][PAWN][1]
                   ^ z[B][ROOK][1];

  KRKRPMaterialKey = z[W][ROOK][1]
                   ^ z[B][ROOK][1]
                   ^ z[B][PAWN][1];

  KRPPKRPMaterialKey =
    z[W][ROOK][1] ^
    z[W][PAWN][1] ^
    z[W][PAWN][2] ^
    z[B][ROOK][1] ^
    z[B][PAWN][1];
  KRPKRPPMaterialKey =
    z[W][ROOK][1] ^
    z[W][PAWN][1] ^
    z[B][ROOK][1] ^
    z[B][PAWN][1] ^
    z[B][PAWN][2];
  KNNKMaterialKey =
    z[W][KNIGHT][1] ^
    z[W][KNIGHT][2];
  KKNNMaterialKey =
    z[B][KNIGHT][1] ^
    z[B][KNIGHT][2];
  KBPKBMaterialKey =
    z[W][BISHOP][1] ^
    z[W][PAWN][1] ^
    z[B][BISHOP][1];
  KBKBPMaterialKey =
    z[W][BISHOP][1] ^
    z[B][BISHOP][1] ^
    z[B][PAWN][1];
  KBPKNMaterialKey =
    z[W][BISHOP][1] ^
    z[W][PAWN][1] ^
    z[B][KNIGHT][1];
  KNKBPMaterialKey =
    z[W][KNIGHT][1] ^
    z[B][BISHOP][1] ^
    z[B][PAWN][1];
  KNPKMaterialKey =
    z[W][KNIGHT][1] ^
    z[W][PAWN][1];
  KKNPMaterialKey =
    z[B][KNIGHT][1] ^
    z[B][PAWN][1];
  KPKPMaterialKey =
    z[W][PAWN][1] ^
    z[B][PAWN][1];


}


/// Constructor for the MaterialInfoTable class.

MaterialInfoTable::MaterialInfoTable(unsigned numOfEntries) {

  size = numOfEntries;
  entries = new MaterialInfo[size];
  if (!entries)
  {
      std::cerr << "Failed to allocate " << (numOfEntries * sizeof(MaterialInfo))
                << " bytes for material hash table." << std::endl;
      exit(EXIT_FAILURE);
  }
  clear();
}


/// Destructor for the MaterialInfoTable class.

MaterialInfoTable::~MaterialInfoTable() {

  delete [] entries;
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

MaterialInfo *MaterialInfoTable::get_material_info(const Position &pos) {

  Key key = pos.get_material_key();
  int index = key & (size - 1);
  MaterialInfo *mi = entries + index;

  // If mi->key matches the position's material hash key, it means that we
  // have analysed this material configuration before, and we can simply
  // return the information we found the last time instead of recomputing it:
  if(mi->key == key)
    return mi;

  // Clear the MaterialInfo object, and set its key:
  mi->clear();
  mi->key = key;

  // A special case before looking for a specialized evaluation function:
  // KNN vs K is a draw:
  if (key == KNNKMaterialKey || key == KKNNMaterialKey)
  {
    mi->factor[WHITE] = mi->factor[BLACK] = 0;
    return mi;
  }

  // Let's look if we have a specialized evaluation function for this
  // particular material configuration
  if (EEFmap.find(key) != EEFmap.end())
  {
      mi->evaluationFunction = EEFmap[key];
      return mi;
  }
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

  if(key == KRPKRMaterialKey) {
    mi->scalingFunction[WHITE] = &ScaleKRPKR;
    return mi;
  }
  if(key == KRKRPMaterialKey) {
    mi->scalingFunction[BLACK] = &ScaleKRKRP;
    return mi;
  }
  if(key == KRPPKRPMaterialKey) {
    mi->scalingFunction[WHITE] = &ScaleKRPPKRP;
    return mi;
  }
  else if(key == KRPKRPPMaterialKey) {
    mi->scalingFunction[BLACK] = &ScaleKRPKRPP;
    return mi;
  }
  if(key == KBPKBMaterialKey) {
    mi->scalingFunction[WHITE] = &ScaleKBPKB;
    return mi;
  }
  if(key == KBKBPMaterialKey) {
    mi->scalingFunction[BLACK] = &ScaleKBKBP;
    return mi;
  }
  if(key == KBPKNMaterialKey) {
    mi->scalingFunction[WHITE] = &ScaleKBPKN;
    return mi;
  }
  if(key == KNKBPMaterialKey) {
    mi->scalingFunction[BLACK] = &ScaleKNKBP;
    return mi;
  }
  if(key == KNPKMaterialKey) {
    mi->scalingFunction[WHITE] = &ScaleKNPK;
    return mi;
  }
  if(key == KKNPMaterialKey) {
    mi->scalingFunction[BLACK] = &ScaleKKNP;
    return mi;
  }

  if(pos.non_pawn_material(WHITE) == BishopValueMidgame &&
     pos.piece_count(WHITE, BISHOP) == 1 && pos.piece_count(WHITE, PAWN) >= 1)
    mi->scalingFunction[WHITE] = &ScaleKBPK;
  if(pos.non_pawn_material(BLACK) == BishopValueMidgame &&
     pos.piece_count(BLACK, BISHOP) == 1 && pos.piece_count(BLACK, PAWN) >= 1)
    mi->scalingFunction[BLACK] = &ScaleKKBP;

  if(pos.piece_count(WHITE, PAWN) == 0 &&
     pos.non_pawn_material(WHITE) == QueenValueMidgame &&
     pos.piece_count(WHITE, QUEEN) == 1 &&
     pos.piece_count(BLACK, ROOK) == 1 && pos.piece_count(BLACK, PAWN) >= 1)
    mi->scalingFunction[WHITE] = &ScaleKQKRP;
  else if(pos.piece_count(BLACK, PAWN) == 0 &&
          pos.non_pawn_material(BLACK) == QueenValueMidgame &&
          pos.piece_count(BLACK, QUEEN) == 1 &&
          pos.piece_count(WHITE, ROOK) == 1 && pos.piece_count(WHITE, PAWN) >= 1)
    mi->scalingFunction[BLACK] = &ScaleKRPKQ;

  if(pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) == Value(0)) {
    if(pos.piece_count(BLACK, PAWN) == 0) {
      assert(pos.piece_count(WHITE, PAWN) >= 2);
      mi->scalingFunction[WHITE] = &ScaleKPsK;
    }
    else if(pos.piece_count(WHITE, PAWN) == 0) {
      assert(pos.piece_count(BLACK, PAWN) >= 2);
      mi->scalingFunction[BLACK] = &ScaleKKPs;
    }
    else if(pos.piece_count(WHITE, PAWN) == 1 && pos.piece_count(BLACK, PAWN) == 1) {
      mi->scalingFunction[WHITE] = &ScaleKPKPw;
      mi->scalingFunction[BLACK] = &ScaleKPKPb;
    }
  }

  // Evaluate the material balance.

  Color c;
  int sign;
  Value egValue = Value(0), mgValue = Value(0);

  for(c = WHITE, sign = 1; c <= BLACK; c++, sign = -sign) {

    // No pawns makes it difficult to win, even with a material advantage:
    if(pos.piece_count(c, PAWN) == 0 &&
       pos.non_pawn_material(c) - pos.non_pawn_material(opposite_color(c))
       <= BishopValueMidgame) {
      if(pos.non_pawn_material(c) == pos.non_pawn_material(opposite_color(c)))
        mi->factor[c] = 0;
      else if(pos.non_pawn_material(c) < RookValueMidgame)
        mi->factor[c] = 0;
      else {
        switch(pos.piece_count(c, BISHOP)) {
        case 2:
          mi->factor[c] = 32; break;
        case 1:
          mi->factor[c] = 12; break;
        case 0:
          mi->factor[c] = 6; break;
        }
      }
    }

    // Bishop pair:
    if(pos.piece_count(c, BISHOP) >= 2) {
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
    if(pos.piece_count(c, ROOK) >= 1) {
      Value v = Value((pos.piece_count(c, ROOK) - 1) * 32 + pos.piece_count(c, QUEEN) * 16);
      mgValue -= sign * v;
      egValue -= sign * v;
    }

  }

  mi->mgValue = int16_t(mgValue);
  mi->egValue = int16_t(egValue);

  return mi;
}
