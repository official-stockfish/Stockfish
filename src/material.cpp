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

#include <algorithm>  // For std::min
#include <cassert>
#include <cstring>

#include "material.h"

using namespace std;

namespace {

  // Polynomial material balance parameters

  //                                  pair  pawn knight bishop rook queen
  const int LinearCoefficients[6] = { 1852, -162, -1122, -183,  249, -154 };

  const int QuadraticCoefficientsSameSide[][PIECE_TYPE_NB] = {
    //            OUR PIECES
    // pair pawn knight bishop rook queen
    {   0                               }, // Bishop pair
    {  39,    2                         }, // Pawn
    {  35,  271,  -4                    }, // knight      OUR PIECES
    {   0,  105,   4,    0              }, // Bishop
    { -27,   -2,  46,   100,  -141      }, // Rook
    {-177,   25, 129,   142,  -137,   0 }  // Queen
  };

  const int QuadraticCoefficientsOppositeSide[][PIECE_TYPE_NB] = {
    //           THEIR PIECES
    // pair pawn knight bishop rook queen
    {   0                               }, // Bishop pair
    {  37,    0                         }, // Pawn
    {  10,   62,   0                    }, // Knight      OUR PIECES
    {  57,   64,  39,     0             }, // Bishop
    {  50,   40,  23,   -22,    0       }, // Rook
    {  98,  105, -39,   141,  274,    0 }  // Queen
  };

  // Endgame evaluation and scaling functions are accessed directly and not through
  // the function maps because they correspond to more than one material hash key.
  Endgame<KXK>   EvaluateKXK[]   = { Endgame<KXK>(WHITE),   Endgame<KXK>(BLACK) };

  Endgame<KBPsK>  ScaleKBPsK[]  = { Endgame<KBPsK>(WHITE),  Endgame<KBPsK>(BLACK) };
  Endgame<KQKRPs> ScaleKQKRPs[] = { Endgame<KQKRPs>(WHITE), Endgame<KQKRPs>(BLACK) };
  Endgame<KPsK>   ScaleKPsK[]   = { Endgame<KPsK>(WHITE),   Endgame<KPsK>(BLACK) };
  Endgame<KPKP>   ScaleKPKP[]   = { Endgame<KPKP>(WHITE),   Endgame<KPKP>(BLACK) };

  // Helper templates used to detect a given material distribution
  template<Color Us> bool is_KXK(const Position& pos) {
    const Color Them = (Us == WHITE ? BLACK : WHITE);
    return  !pos.count<PAWN>(Them)
          && pos.non_pawn_material(Them) == VALUE_ZERO
          && pos.non_pawn_material(Us) >= RookValueMg;
  }

  template<Color Us> bool is_KBPsKs(const Position& pos) {
    return   pos.non_pawn_material(Us) == BishopValueMg
          && pos.count<BISHOP>(Us) == 1
          && pos.count<PAWN  >(Us) >= 1;
  }

  template<Color Us> bool is_KQKRPs(const Position& pos) {
    const Color Them = (Us == WHITE ? BLACK : WHITE);
    return  !pos.count<PAWN>(Us)
          && pos.non_pawn_material(Us) == QueenValueMg
          && pos.count<QUEEN>(Us)  == 1
          && pos.count<ROOK>(Them) == 1
          && pos.count<PAWN>(Them) >= 1;
  }

  /// imbalance() calculates the imbalance by comparing the piece count of each
  /// piece type for both colors.

  template<Color Us>
  int imbalance(const int pieceCount[][PIECE_TYPE_NB]) {

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    int pt1, pt2, pc, v;
    int value = 0;

    // Second-degree polynomial material imbalance by Tord Romstad
    for (pt1 = NO_PIECE_TYPE; pt1 <= QUEEN; ++pt1)
    {
        pc = pieceCount[Us][pt1];
        if (!pc)
            continue;

        v = LinearCoefficients[pt1];

        for (pt2 = NO_PIECE_TYPE; pt2 <= pt1; ++pt2)
            v +=  QuadraticCoefficientsSameSide[pt1][pt2] * pieceCount[Us][pt2]
                + QuadraticCoefficientsOppositeSide[pt1][pt2] * pieceCount[Them][pt2];

        value += pc * v;
    }

    return value;
  }

} // namespace

namespace Material {

/// Material::probe() takes a position object as input, looks up a MaterialEntry
/// object, and returns a pointer to it. If the material configuration is not
/// already present in the table, it is computed and stored there, so we don't
/// have to recompute everything when the same material configuration occurs again.

Entry* probe(const Position& pos, Table& entries, Endgames& endgames) {

  Key key = pos.material_key();
  Entry* e = entries[key];

  // If e->key matches the position's material hash key, it means that we
  // have analysed this material configuration before, and we can simply
  // return the information we found the last time instead of recomputing it.
  if (e->key == key)
      return e;

  std::memset(e, 0, sizeof(Entry));
  e->key = key;
  e->factor[WHITE] = e->factor[BLACK] = (uint8_t)SCALE_FACTOR_NORMAL;
  e->gamePhase = game_phase(pos);

  // Let's look if we have a specialized evaluation function for this particular
  // material configuration. Firstly we look for a fixed configuration one, then
  // for a generic one if the previous search failed.
  if (endgames.probe(key, e->evaluationFunction))
      return e;

  if (is_KXK<WHITE>(pos))
  {
      e->evaluationFunction = &EvaluateKXK[WHITE];
      return e;
  }

  if (is_KXK<BLACK>(pos))
  {
      e->evaluationFunction = &EvaluateKXK[BLACK];
      return e;
  }

  // OK, we didn't find any special evaluation function for the current
  // material configuration. Is there a suitable scaling function?
  //
  // We face problems when there are several conflicting applicable
  // scaling functions and we need to decide which one to use.
  EndgameBase<ScaleFactor>* sf;

  if (endgames.probe(key, sf))
  {
      e->scalingFunction[sf->color()] = sf;
      return e;
  }

  // Generic scaling functions that refer to more than one material
  // distribution. They should be probed after the specialized ones.
  // Note that these ones don't return after setting the function.
  if (is_KBPsKs<WHITE>(pos))
      e->scalingFunction[WHITE] = &ScaleKBPsK[WHITE];

  if (is_KBPsKs<BLACK>(pos))
      e->scalingFunction[BLACK] = &ScaleKBPsK[BLACK];

  if (is_KQKRPs<WHITE>(pos))
      e->scalingFunction[WHITE] = &ScaleKQKRPs[WHITE];

  else if (is_KQKRPs<BLACK>(pos))
      e->scalingFunction[BLACK] = &ScaleKQKRPs[BLACK];

  Value npm_w = pos.non_pawn_material(WHITE);
  Value npm_b = pos.non_pawn_material(BLACK);

  if (npm_w + npm_b == VALUE_ZERO && pos.pieces(PAWN))
  {
      if (!pos.count<PAWN>(BLACK))
      {
          assert(pos.count<PAWN>(WHITE) >= 2);
          e->scalingFunction[WHITE] = &ScaleKPsK[WHITE];
      }
      else if (!pos.count<PAWN>(WHITE))
      {
          assert(pos.count<PAWN>(BLACK) >= 2);
          e->scalingFunction[BLACK] = &ScaleKPsK[BLACK];
      }
      else if (pos.count<PAWN>(WHITE) == 1 && pos.count<PAWN>(BLACK) == 1)
      {
          // This is a special case because we set scaling functions
          // for both colors instead of only one.
          e->scalingFunction[WHITE] = &ScaleKPKP[WHITE];
          e->scalingFunction[BLACK] = &ScaleKPKP[BLACK];
      }
  }

  // No pawns makes it difficult to win, even with a material advantage. This
  // catches some trivial draws like KK, KBK and KNK and gives a very drawish
  // scale factor for cases such as KRKBP and KmmKm (except for KBBKN).
  if (!pos.count<PAWN>(WHITE) && npm_w - npm_b <= BishopValueMg)
      e->factor[WHITE] = uint8_t(npm_w < RookValueMg ? SCALE_FACTOR_DRAW : npm_b <= BishopValueMg ? 4 : 12);

  if (!pos.count<PAWN>(BLACK) && npm_b - npm_w <= BishopValueMg)
      e->factor[BLACK] = uint8_t(npm_b < RookValueMg ? SCALE_FACTOR_DRAW : npm_w <= BishopValueMg ? 4 : 12);

  if (pos.count<PAWN>(WHITE) == 1 && npm_w - npm_b <= BishopValueMg)
      e->factor[WHITE] = (uint8_t) SCALE_FACTOR_ONEPAWN;

  if (pos.count<PAWN>(BLACK) == 1 && npm_b - npm_w <= BishopValueMg)
      e->factor[BLACK] = (uint8_t) SCALE_FACTOR_ONEPAWN;

  // Compute the space weight
  if (npm_w + npm_b >= 2 * QueenValueMg + 4 * RookValueMg + 2 * KnightValueMg)
  {
      int minorPieceCount =  pos.count<KNIGHT>(WHITE) + pos.count<BISHOP>(WHITE)
                           + pos.count<KNIGHT>(BLACK) + pos.count<BISHOP>(BLACK);

      e->spaceWeight = make_score(minorPieceCount * minorPieceCount, 0);
  }

  // Evaluate the material imbalance. We use PIECE_TYPE_NONE as a place holder
  // for the bishop pair "extended piece", which allows us to be more flexible
  // in defining bishop pair bonuses.
  const int pieceCount[COLOR_NB][PIECE_TYPE_NB] = {
  { pos.count<BISHOP>(WHITE) > 1, pos.count<PAWN>(WHITE), pos.count<KNIGHT>(WHITE),
    pos.count<BISHOP>(WHITE)    , pos.count<ROOK>(WHITE), pos.count<QUEEN >(WHITE) },
  { pos.count<BISHOP>(BLACK) > 1, pos.count<PAWN>(BLACK), pos.count<KNIGHT>(BLACK),
    pos.count<BISHOP>(BLACK)    , pos.count<ROOK>(BLACK), pos.count<QUEEN >(BLACK) } };

  e->value = (int16_t)((imbalance<WHITE>(pieceCount) - imbalance<BLACK>(pieceCount)) / 16);
  return e;
}


/// Material::game_phase() calculates the phase given the current
/// position. Because the phase is strictly a function of the material, it
/// is stored in MaterialEntry.

Phase game_phase(const Position& pos) {

  Value npm = pos.non_pawn_material(WHITE) + pos.non_pawn_material(BLACK);

  return  npm >= MidgameLimit ? PHASE_MIDGAME
        : npm <= EndgameLimit ? PHASE_ENDGAME
        : Phase(((npm - EndgameLimit) * 128) / (MidgameLimit - EndgameLimit));
}

} // namespace Material
