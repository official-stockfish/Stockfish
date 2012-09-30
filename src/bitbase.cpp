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

#include <cassert>

#include "bitboard.h"
#include "types.h"

namespace {

  enum Result {
    INVALID = 0,
    UNKNOWN = 1,
    DRAW    = 2,
    WIN     = 4
  };

  inline Result& operator|=(Result& r, Result v) { return r = Result(r | v); }

  struct KPKPosition {

    Result classify_leaf(int idx);
    Result classify(int idx, Result db[]);

  private:
    template<Color Us> Result classify(const Result db[]) const;

    template<Color Us> Bitboard k_attacks() const {
      return Us == WHITE ? StepAttacksBB[W_KING][wksq] : StepAttacksBB[B_KING][bksq];
    }

    Bitboard p_attacks() const { return StepAttacksBB[W_PAWN][psq]; }
    void decode_index(int idx);

    Square wksq, bksq, psq;
    Color stm;
  };

  // The possible pawns squares are 24, the first 4 files and ranks from 2 to 7
  const int IndexMax = 2 * 24 * 64 * 64; // stm * wp_sq * wk_sq * bk_sq = 196608

  // Each uint32_t stores results of 32 positions, one per bit
  uint32_t KPKBitbase[IndexMax / 32];

  int index(Square wksq, Square bksq, Square psq, Color stm);
}


uint32_t Bitbases::probe_kpk(Square wksq, Square wpsq, Square bksq, Color stm) {

  int idx = index(wksq, bksq, wpsq, stm);
  return KPKBitbase[idx / 32] & (1 << (idx & 31));
}


void Bitbases::init_kpk() {

  Result* db = new Result[IndexMax]; // Avoid to hit stack limit on some platforms
  KPKPosition pos;
  int idx, bit, repeat = 1;

  // Initialize table with known win / draw positions
  for (idx = 0; idx < IndexMax; idx++)
      db[idx] = pos.classify_leaf(idx);

  // Iterate until all positions are classified (30 cycles needed)
  while (repeat)
      for (repeat = idx = 0; idx < IndexMax; idx++)
          if (db[idx] == UNKNOWN && (db[idx] = pos.classify(idx, db)) != UNKNOWN)
              repeat = 1;

  // Map 32 position results into one KPKBitbase[] entry
  for (idx = 0; idx < IndexMax / 32; idx++)
      for (bit = 0; bit < 32; bit++)
          if (db[32 * idx + bit] == WIN)
              KPKBitbase[idx] |= 1 << bit;

  delete [] db;
}


namespace {

  // A KPK bitbase index is an integer in [0, IndexMax] range
  //
  // Information is mapped in this way
  //
  // bit     0: side to move (WHITE or BLACK)
  // bit  1- 6: black king square (from SQ_A1 to SQ_H8)
  // bit  7-12: white king square (from SQ_A1 to SQ_H8)
  // bit 13-14: white pawn file (from FILE_A to FILE_D)
  // bit 15-17: white pawn rank - 1 (from RANK_2 - 1 to RANK_7 - 1)

  int index(Square w, Square b, Square p, Color c) {

    assert(file_of(p) <= FILE_D);

    return c + (b << 1) + (w << 7) + (file_of(p) << 13) + ((rank_of(p) - 1) << 15);
  }

  void KPKPosition::decode_index(int idx) {

    stm  = Color(idx & 1);
    bksq = Square((idx >> 1) & 63);
    wksq = Square((idx >> 7) & 63);
    psq  = File((idx >> 13) & 3) | Rank((idx >> 15) + 1);
  }

  Result KPKPosition::classify_leaf(int idx) {

    decode_index(idx);

    // Check if two pieces are on the same square or if a king can be captured
    if (   wksq == psq || wksq == bksq || bksq == psq
        || (k_attacks<WHITE>() & bksq)
        || (stm == WHITE && (p_attacks() & bksq)))
        return INVALID;

    // The position is an immediate win if it is white to move and the white
    // pawn can be promoted without getting captured.
    if (   rank_of(psq) == RANK_7
        && stm == WHITE
        && wksq != psq + DELTA_N
        && (   square_distance(bksq, psq + DELTA_N) > 1
            ||(k_attacks<WHITE>() & (psq + DELTA_N))))
        return WIN;

    // Check for known draw positions
    //
    // Case 1: Stalemate
    if (   stm == BLACK
        && !(k_attacks<BLACK>() & ~(k_attacks<WHITE>() | p_attacks())))
        return DRAW;

    // Case 2: King can capture undefended pawn
    if (   stm == BLACK
        && (k_attacks<BLACK>() & psq & ~k_attacks<WHITE>()))
        return DRAW;

    // Case 3: Black king in front of white pawn
    if (   bksq == psq + DELTA_N
        && rank_of(psq) < RANK_7)
        return DRAW;

    // Case 4: White king in front of pawn and black has opposition
    if (   stm == WHITE
        && wksq == psq + DELTA_N
        && bksq == wksq + DELTA_N + DELTA_N
        && rank_of(psq) < RANK_5)
        return DRAW;

    // Case 5: Stalemate with rook pawn
    if (   bksq == SQ_A8
        && file_of(psq) == FILE_A)
        return DRAW;

    // Case 6: White king trapped on the rook file
    if (   file_of(wksq) == FILE_A
        && file_of(psq) == FILE_A
        && rank_of(wksq) > rank_of(psq)
        && bksq == wksq + 2)
        return DRAW;

    return UNKNOWN;
  }

  template<Color Us>
  Result KPKPosition::classify(const Result db[]) const {

    // White to Move: If one move leads to a position classified as RESULT_WIN,
    // the result of the current position is RESULT_WIN. If all moves lead to
    // positions classified as RESULT_DRAW, the current position is classified
    // RESULT_DRAW otherwise the current position is classified as RESULT_UNKNOWN.
    //
    // Black to Move: If one move leads to a position classified as RESULT_DRAW,
    // the result of the current position is RESULT_DRAW. If all moves lead to
    // positions classified as RESULT_WIN, the position is classified RESULT_WIN.
    // Otherwise, the current position is classified as RESULT_UNKNOWN.

    Result r = INVALID;
    Bitboard b = k_attacks<Us>();

    while (b)
    {
        r |= Us == WHITE ? db[index(pop_lsb(&b), bksq, psq, BLACK)]
                         : db[index(wksq, pop_lsb(&b), psq, WHITE)];

        if (Us == WHITE && (r & WIN))
            return WIN;

        if (Us == BLACK && (r & DRAW))
            return DRAW;
    }

    if (Us == WHITE && rank_of(psq) < RANK_7)
    {
        Square s = psq + DELTA_N;
        r |= db[index(wksq, bksq, s, BLACK)]; // Single push

        if (rank_of(s) == RANK_3 && s != wksq && s != bksq)
            r |= db[index(wksq, bksq, s + DELTA_N, BLACK)]; // Double push

        if (r & WIN)
            return WIN;
    }

    return r & UNKNOWN ? UNKNOWN : Us == WHITE ? DRAW : WIN;
  }

  Result KPKPosition::classify(int idx, Result db[]) {

    decode_index(idx);
    return stm == WHITE ? classify<WHITE>(db) : classify<BLACK>(db);
  }

}
