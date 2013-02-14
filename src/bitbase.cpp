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
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace {

  // The possible pawns squares are 24, the first 4 files and ranks from 2 to 7
  const unsigned IndexMax = 24*64*64*2; // wp_sq * wk_sq * bk_sq * stm = 196608

  // Each uint32_t stores results of 32 positions, one per bit
  uint32_t KPKBitbase[IndexMax / 32];

  // A KPK bitbase index is an integer in [0, IndexMax] range
  //
  // Information is mapped in this way
  //
  // bit     0: side to move (WHITE or BLACK)
  // bit  1- 6: black king square (from SQ_A1 to SQ_H8)
  // bit  7-12: white king square (from SQ_A1 to SQ_H8)
  // bit 13-14: white pawn file (from FILE_A to FILE_D)
  // bit 15-17: white pawn 6 - rank (from 6 - RANK_7 to 6 - RANK_2)
  unsigned index(Color stm, Square bksq, Square wksq, Square psq) {
    return stm + (bksq << 1) + (wksq << 7) + (file_of(psq) << 13) + ((6 - rank_of(psq)) << 15);
  }

  enum Result {
    INVALID = 0,
    UNKNOWN = 1,
    DRAW    = 2,
    WIN     = 4
  };

  inline Result& operator|=(Result& r, Result v) { return r = Result(r | v); }

  struct KPKPosition {

    void classify_leaf(unsigned idx);

    Result classify(const std::vector<KPKPosition>& db)
    { return stm == WHITE ? classify<WHITE>(db) : classify<BLACK>(db); }

    operator Result() const { return res; }

  private:
    template<Color Us> Bitboard k_attacks() const
    { return StepAttacksBB[KING][Us == WHITE ? wksq : bksq]; }

    template<Color Us> Result classify(const std::vector<KPKPosition>& db);

    Color stm;
    Square bksq, wksq, psq;
    Result res;
  };

} // namespace


bool Bitbases::probe_kpk(Square wksq, Square wpsq, Square bksq, Color stm) {

  assert(file_of(wpsq) <= FILE_D);

  unsigned idx = index(stm, bksq, wksq, wpsq);
  return KPKBitbase[idx / 32] & (1 << (idx & 31));
}


void Bitbases::init_kpk() {

  unsigned idx, repeat = 1;
  std::vector<KPKPosition> db(IndexMax);

  // Initialize db with known win / draw positions
  for (idx = 0; idx < IndexMax; idx++)
      db[idx].classify_leaf(idx);

  // Iterate until all positions are classified (26 cycles needed)
  while (repeat)
      for (repeat = idx = 0; idx < IndexMax; idx++)
          if (db[idx] == UNKNOWN && db[idx].classify(db) != UNKNOWN)
              repeat = 1;

  // Map 32 results into one KPKBitbase[] entry
  for (idx = 0; idx < IndexMax; idx++)
      if (db[idx] == WIN)
          KPKBitbase[idx / 32] |= 1 << (idx & 31);
}


namespace {

  void KPKPosition::classify_leaf(unsigned idx) {

    stm  = Color(idx & 1);
    bksq = Square((idx >> 1) & 0x3F);
    wksq = Square((idx >> 7) & 0x3F);
    psq  = File((idx >> 13) & 3) | Rank(6 - (idx >> 15));

    // Check if two pieces are on the same square or if a king can be captured
    if (   wksq == psq || wksq == bksq || bksq == psq
        || (k_attacks<WHITE>() & bksq)
        || (stm == WHITE && (StepAttacksBB[PAWN][psq] & bksq)))
        res = INVALID;

    // The position is an immediate win if it is white to move and the white
    // pawn can be promoted without getting captured.
    else if (   rank_of(psq) == RANK_7
             && stm == WHITE
             && wksq != psq + DELTA_N
             && (   square_distance(bksq, psq + DELTA_N) > 1
                 ||(k_attacks<WHITE>() & (psq + DELTA_N))))
        res = WIN;

    // Check for known draw positions
    //
    // Case 1: Stalemate
    else if (   stm == BLACK
             && !(k_attacks<BLACK>() & ~(k_attacks<WHITE>() | StepAttacksBB[PAWN][psq])))
        res = DRAW;

    // Case 2: King can capture undefended pawn
    else if (   stm == BLACK
             && (k_attacks<BLACK>() & psq & ~k_attacks<WHITE>()))
        res = DRAW;

    // Case 3: Black king in front of white pawn
    else if (   bksq == psq + DELTA_N
             && rank_of(psq) < RANK_7)
        res = DRAW;

    // Case 4: White king in front of pawn and black has opposition
    else if (   stm == WHITE
             && wksq == psq + DELTA_N
             && bksq == wksq + DELTA_N + DELTA_N
             && rank_of(psq) < RANK_5)
        res = DRAW;

    // Case 5: Stalemate with rook pawn
    else if (   bksq == SQ_A8
             && file_of(psq) == FILE_A)
        res = DRAW;

    // Case 6: White king trapped on the rook file
    else if (   file_of(wksq) == FILE_A
             && file_of(psq) == FILE_A
             && rank_of(wksq) > rank_of(psq)
             && bksq == wksq + 2)
        res = DRAW;

    else
        res = UNKNOWN;
  }

  template<Color Us>
  Result KPKPosition::classify(const std::vector<KPKPosition>& db) {

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
        r |= Us == WHITE ? db[index(BLACK, bksq, pop_lsb(&b), psq)]
                         : db[index(WHITE, pop_lsb(&b), wksq, psq)];

        if (Us == WHITE && (r & WIN))
            return res = WIN;

        if (Us == BLACK && (r & DRAW))
            return res = DRAW;
    }

    if (Us == WHITE && rank_of(psq) < RANK_7)
    {
        Square s = psq + DELTA_N;
        r |= db[index(BLACK, bksq, wksq, s)]; // Single push

        if (rank_of(s) == RANK_3 && s != wksq && s != bksq)
            r |= db[index(BLACK, bksq, wksq, s + DELTA_N)]; // Double push

        if (r & WIN)
            return res = WIN;
    }

    return res = r & UNKNOWN ? UNKNOWN : Us == WHITE ? DRAW : WIN;
  }

}
