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


////
//// Includes
////

#include <cassert>

#include "material.h"


////
//// Local definitions
////

namespace {

  const Value BishopPairMidgameBonus = Value(100);
  const Value BishopPairEndgameBonus = Value(100);

  Key KPKMaterialKey, KKPMaterialKey;
  Key KBNKMaterialKey, KKBNMaterialKey;
  Key KRKPMaterialKey, KPKRMaterialKey;
  Key KRKBMaterialKey, KBKRMaterialKey;
  Key KRKNMaterialKey, KNKRMaterialKey;
  Key KQKRMaterialKey, KRKQMaterialKey;
  Key KRPKRMaterialKey, KRKRPMaterialKey;
  Key KRPPKRPMaterialKey, KRPKRPPMaterialKey;
  Key KNNKMaterialKey, KKNNMaterialKey;
  Key KBPKBMaterialKey, KBKBPMaterialKey;
  Key KBPKNMaterialKey, KNKBPMaterialKey;
  Key KNPKMaterialKey, KKNPMaterialKey;
  Key KPKPMaterialKey;

};


////
//// Functions
////

/// MaterialInfo::init() is called during program initialization.  It 
/// precomputes material hash keys for a few basic endgames, in order
/// to make it easy to recognize such endgames when they occur.

void MaterialInfo::init() {
  KPKMaterialKey = Position::zobMaterial[WHITE][PAWN][1];
  KKPMaterialKey = Position::zobMaterial[BLACK][PAWN][1];
  KBNKMaterialKey =
    Position::zobMaterial[WHITE][BISHOP][1] ^
    Position::zobMaterial[WHITE][KNIGHT][1];
  KKBNMaterialKey =
    Position::zobMaterial[BLACK][BISHOP][1] ^
    Position::zobMaterial[BLACK][KNIGHT][1];
  KRKPMaterialKey =
    Position::zobMaterial[WHITE][ROOK][1] ^
    Position::zobMaterial[BLACK][PAWN][1];
  KPKRMaterialKey =
    Position::zobMaterial[WHITE][PAWN][1] ^
    Position::zobMaterial[BLACK][ROOK][1];
  KRKBMaterialKey =
    Position::zobMaterial[WHITE][ROOK][1] ^
    Position::zobMaterial[BLACK][BISHOP][1];
  KBKRMaterialKey =
    Position::zobMaterial[WHITE][BISHOP][1] ^
    Position::zobMaterial[BLACK][ROOK][1];
  KRKNMaterialKey =
    Position::zobMaterial[WHITE][ROOK][1] ^
    Position::zobMaterial[BLACK][KNIGHT][1];
  KNKRMaterialKey =
    Position::zobMaterial[WHITE][KNIGHT][1] ^
    Position::zobMaterial[BLACK][ROOK][1];
  KQKRMaterialKey =
    Position::zobMaterial[WHITE][QUEEN][1] ^
    Position::zobMaterial[BLACK][ROOK][1];
  KRKQMaterialKey =
    Position::zobMaterial[WHITE][ROOK][1] ^
    Position::zobMaterial[BLACK][QUEEN][1];
  KRPKRMaterialKey =
    Position::zobMaterial[WHITE][ROOK][1] ^
    Position::zobMaterial[WHITE][PAWN][1] ^
    Position::zobMaterial[BLACK][ROOK][1];
  KRKRPMaterialKey =
    Position::zobMaterial[WHITE][ROOK][1] ^
    Position::zobMaterial[BLACK][ROOK][1] ^
    Position::zobMaterial[BLACK][PAWN][1];
  KRPPKRPMaterialKey =
    Position::zobMaterial[WHITE][ROOK][1] ^
    Position::zobMaterial[WHITE][PAWN][1] ^
    Position::zobMaterial[WHITE][PAWN][2] ^
    Position::zobMaterial[BLACK][ROOK][1] ^
    Position::zobMaterial[BLACK][PAWN][1];
  KRPKRPPMaterialKey =
    Position::zobMaterial[WHITE][ROOK][1] ^
    Position::zobMaterial[WHITE][PAWN][1] ^
    Position::zobMaterial[BLACK][ROOK][1] ^
    Position::zobMaterial[BLACK][PAWN][1] ^
    Position::zobMaterial[BLACK][PAWN][2];
  KNNKMaterialKey =
    Position::zobMaterial[WHITE][KNIGHT][1] ^
    Position::zobMaterial[WHITE][KNIGHT][2];
  KKNNMaterialKey =
    Position::zobMaterial[BLACK][KNIGHT][1] ^
    Position::zobMaterial[BLACK][KNIGHT][2];
  KBPKBMaterialKey =
    Position::zobMaterial[WHITE][BISHOP][1] ^
    Position::zobMaterial[WHITE][PAWN][1] ^
    Position::zobMaterial[BLACK][BISHOP][1];
  KBKBPMaterialKey =
    Position::zobMaterial[WHITE][BISHOP][1] ^
    Position::zobMaterial[BLACK][BISHOP][1] ^
    Position::zobMaterial[BLACK][PAWN][1];
  KBPKNMaterialKey =
    Position::zobMaterial[WHITE][BISHOP][1] ^
    Position::zobMaterial[WHITE][PAWN][1] ^
    Position::zobMaterial[BLACK][KNIGHT][1];
  KNKBPMaterialKey =
    Position::zobMaterial[WHITE][KNIGHT][1] ^
    Position::zobMaterial[BLACK][BISHOP][1] ^
    Position::zobMaterial[BLACK][PAWN][1];
  KNPKMaterialKey =
    Position::zobMaterial[WHITE][KNIGHT][1] ^
    Position::zobMaterial[WHITE][PAWN][1];
  KKNPMaterialKey =
    Position::zobMaterial[BLACK][KNIGHT][1] ^
    Position::zobMaterial[BLACK][PAWN][1];
  KPKPMaterialKey =
    Position::zobMaterial[WHITE][PAWN][1] ^
    Position::zobMaterial[BLACK][PAWN][1];
}


/// Constructor for the MaterialInfoTable class.

MaterialInfoTable::MaterialInfoTable(unsigned numOfEntries) {
  size = numOfEntries;
  entries = new MaterialInfo[size];
  if(entries == NULL) {
    std::cerr << "Failed to allocate " << (numOfEntries * sizeof(MaterialInfo))
              << " bytes for material hash table." << std::endl;
    exit(EXIT_FAILURE);
  }
  this->clear();
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
  if(key == KNNKMaterialKey || key == KKNNMaterialKey) {
    mi->factor[WHITE] = mi->factor[BLACK] = 0;
    return mi;
  }

  // Let's look if we have a specialized evaluation function for this
  // particular material configuration:
  if(key == KPKMaterialKey) {
    mi->evaluationFunction = &EvaluateKPK;
    return mi;
  }
  else if(key == KKPMaterialKey) {
    mi->evaluationFunction = &EvaluateKKP;
    return mi;
  }
  else if(key == KBNKMaterialKey) {
    mi->evaluationFunction = &EvaluateKBNK;
    return mi;
  }
  else if(key == KKBNMaterialKey) {
    mi->evaluationFunction = &EvaluateKKBN;
    return mi;
  }
  else if(key == KRKPMaterialKey) {
    mi->evaluationFunction = &EvaluateKRKP;
    return mi;
  }
  else if(key == KPKRMaterialKey) {
    mi->evaluationFunction = &EvaluateKPKR;
    return mi;
  }
  else if(key == KRKBMaterialKey) {
    mi->evaluationFunction = &EvaluateKRKB;
    return mi;
  }
  else if(key == KBKRMaterialKey) {
    mi->evaluationFunction = &EvaluateKBKR;
    return mi;
  }
  else if(key == KRKNMaterialKey) {
    mi->evaluationFunction = &EvaluateKRKN;
    return mi;
  }
  else if(key == KNKRMaterialKey) {
    mi->evaluationFunction = &EvaluateKNKR;
    return mi;
  }
  else if(key == KQKRMaterialKey) {
    mi->evaluationFunction = &EvaluateKQKR;
    return mi;
  }
  else if(key == KRKQMaterialKey) {
    mi->evaluationFunction = &EvaluateKRKQ;
    return mi;
  }
  else if(pos.non_pawn_material(BLACK) == Value(0) &&
          pos.pawn_count(BLACK) == 0 &&
          pos.non_pawn_material(WHITE) >= RookValueEndgame) {
    mi->evaluationFunction = &EvaluateKXK;
    return mi;
  }
  else if(pos.non_pawn_material(WHITE) == Value(0) &&
          pos.pawn_count(WHITE) == 0 &&
          pos.non_pawn_material(BLACK) >= RookValueEndgame) {
    mi->evaluationFunction = &EvaluateKKX;
    return mi;
  }

  // OK, we didn't find any special evaluation function for the current
  // material configuration.  Is there a suitable scaling function?
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
     pos.bishop_count(WHITE) == 1 && pos.pawn_count(WHITE) >= 1)
    mi->scalingFunction[WHITE] = &ScaleKBPK;
  if(pos.non_pawn_material(BLACK) == BishopValueMidgame &&
     pos.bishop_count(BLACK) == 1 && pos.pawn_count(BLACK) >= 1)
    mi->scalingFunction[BLACK] = &ScaleKKBP;

  if(pos.pawn_count(WHITE) == 0 &&
     pos.non_pawn_material(WHITE) == QueenValueMidgame &&
     pos.queen_count(WHITE) == 1 &&
     pos.rook_count(BLACK) == 1 && pos.pawn_count(BLACK) >= 1)
    mi->scalingFunction[WHITE] = &ScaleKQKRP;
  else if(pos.pawn_count(BLACK) == 0 &&
          pos.non_pawn_material(BLACK) == QueenValueMidgame &&
          pos.queen_count(BLACK) == 1 &&
          pos.rook_count(WHITE) == 1 && pos.pawn_count(WHITE) >= 1)
    mi->scalingFunction[BLACK] = &ScaleKRPKQ;

  if(pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK) == Value(0)) {
    if(pos.pawn_count(BLACK) == 0) {
      assert(pos.pawn_count(WHITE) >= 2);
      mi->scalingFunction[WHITE] = &ScaleKPsK;
    }
    else if(pos.pawn_count(WHITE) == 0) {
      assert(pos.pawn_count(BLACK) >= 2);
      mi->scalingFunction[BLACK] = &ScaleKKPs;
    }
    else if(pos.pawn_count(WHITE) == 1 && pos.pawn_count(BLACK) == 1) {
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
    if(pos.pawn_count(c) == 0 &&
       pos.non_pawn_material(c) - pos.non_pawn_material(opposite_color(c))
       <= BishopValueMidgame) {
      if(pos.non_pawn_material(c) == pos.non_pawn_material(opposite_color(c)))
        mi->factor[c] = 0;
      else if(pos.non_pawn_material(c) < RookValueMidgame)
        mi->factor[c] = 0;
      else {
        switch(pos.bishop_count(c)) {
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
    if(pos.bishop_count(c) >= 2) {
      mgValue += sign * BishopPairMidgameBonus;
      egValue += sign * BishopPairEndgameBonus;
    }

    // Knights are stronger when there are many pawns on the board.  The 
    // formula is taken from Larry Kaufman's paper "The Evaluation of Material
    // Imbalances in Chess": 
    // http://mywebpages.comcast.net/danheisman/Articles/evaluation_of_material_imbalance.htm
    mgValue += sign * Value(pos.knight_count(c)*(pos.pawn_count(c)-5)*16);
    egValue += sign * Value(pos.knight_count(c)*(pos.pawn_count(c)-5)*16);

    // Redundancy of major pieces, again based on Kaufman's paper:
    if(pos.rook_count(c) >= 1) {
      Value v = Value((pos.rook_count(c) - 1) * 32 + pos.queen_count(c) * 16);
      mgValue -= sign * v;
      egValue -= sign * v;
    }
      
  }

  mi->mgValue = int16_t(mgValue);
  mi->egValue = int16_t(egValue);

  return mi;
}
