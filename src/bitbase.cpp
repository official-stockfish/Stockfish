/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2013 Marco Costalba, Joona Kiiski, Tord Romstad

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
  const unsigned IndexMax = 2*24*64*64; // stm * psq * wksq * bksq = 196608

  // Each uint32_t stores results of 32 positions, one per bit
  uint32_t KPKBitbase[IndexMax / 32];

  // A KPK bitbase index is an integer in [0, IndexMax] range
  //
  // Information is mapped in a way that minimizes number of iterations:
  //
  // bit  0- 5: white king square (from SQ_A1 to SQ_H8)
  // bit  6-11: black king square (from SQ_A1 to SQ_H8)
  // bit    12: side to move (WHITE or BLACK)
  // bit 13-14: white pawn file (from FILE_A to FILE_D)
  // bit 15-17: white pawn RANK_7 - rank (from RANK_7 - RANK_7 to RANK_7 - RANK_2)
  unsigned index(Color us, Square bksq, Square wksq, Square psq) {
    return wksq + (bksq << 6) + (us << 12) + (file_of(psq) << 13) + ((RANK_7 - rank_of(psq)) << 15);
  }

  enum Result {
    INVALID = 0,
    UNKNOWN = 1,
    DRAW    = 2,
    WIN     = 4
  };

  inline Result& operator|=(Result& r, Result v) { return r = Result(r | v); }

  struct KPKPosition {

    operator Result() const { return res; }
    Result classify_leaf(unsigned idx);
    Result classify(const std::vector<KPKPosition>& db)
    { return us == WHITE ? classify<WHITE>(db) : classify<BLACK>(db); }

  private:
    template<Color Us> Result classify(const std::vector<KPKPosition>& db);

    Color us;
    Square bksq, wksq, psq;
    Result res;
  };

} // namespace


bool Bitbases::probe_kpk(Square wksq, Square wpsq, Square bksq, Color us) {

  assert(file_of(wpsq) <= FILE_D);

  unsigned idx = index(us, bksq, wksq, wpsq);
  return KPKBitbase[idx / 32] & (1 << (idx & 0x1F));
}


void Bitbases::init_kpk() {

  unsigned idx, repeat = 1;
  std::vector<KPKPosition> db(IndexMax);

  // Initialize db with known win / draw positions
  for (idx = 0; idx < IndexMax; idx++)
      db[idx].classify_leaf(idx);

  // Iterate through the positions until no more of the unknown positions can be
  // changed to either wins or draws (15 cycles needed).
  while (repeat)
      for (repeat = idx = 0; idx < IndexMax; idx++)
          if (db[idx] == UNKNOWN && db[idx].classify(db) != UNKNOWN)
              repeat = 1;

  // Map 32 results into one KPKBitbase[] entry
  for (idx = 0; idx < IndexMax; idx++)
      if (db[idx] == WIN)
          KPKBitbase[idx / 32] |= 1 << (idx & 0x1F);
}


namespace {

  Result KPKPosition::classify_leaf(unsigned idx) {

    wksq = Square((idx >>  0) & 0x3F);
    bksq = Square((idx >>  6) & 0x3F);
    us   = Color ((idx >> 12) & 0x01);
    psq  = File  ((idx >> 13) & 0x03) | Rank(RANK_7 - (idx >> 15));

    // Check if two pieces are on the same square or if a king can be captured
    if (   wksq == psq || wksq == bksq || bksq == psq
        || (StepAttacksBB[KING][wksq] & bksq)
        || (us == WHITE && (StepAttacksBB[PAWN][psq] & bksq)))
        return res = INVALID;

    if (us == WHITE)
    {
        // Immediate win if pawn can be promoted without getting captured
        if (   rank_of(psq) == RANK_7
            && wksq != psq + DELTA_N
            && (   square_distance(bksq, psq + DELTA_N) > 1
                ||(StepAttacksBB[KING][wksq] & (psq + DELTA_N))))
            return res = WIN;
    }
    // Immediate draw if is stalemate or king captures undefended pawn
    else if (  !(StepAttacksBB[KING][bksq] & ~(StepAttacksBB[KING][wksq] | StepAttacksBB[PAWN][psq]))
             || (StepAttacksBB[KING][bksq] & psq & ~StepAttacksBB[KING][wksq]))
        return res = DRAW;

    return res = UNKNOWN;
  }

  template<Color Us>
  Result KPKPosition::classify(const std::vector<KPKPosition>& db) {

    // White to Move: If one move leads to a position classified as WIN, the result
    // of the current position is WIN. If all moves lead to positions classified
    // as DRAW, the current position is classified DRAW otherwise the current
    // position is classified as UNKNOWN.
    //
    // Black to Move: If one move leads to a position classified as DRAW, the result
    // of the current position is DRAW. If all moves lead to positions classified
    // as WIN, the position is classified WIN otherwise the current position is
    // classified UNKNOWN.

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Result r = INVALID;
    Bitboard b = StepAttacksBB[KING][Us == WHITE ? wksq : bksq];

    while (b)
        r |= Us == WHITE ? db[index(Them, bksq, pop_lsb(&b), psq)]
                         : db[index(Them, pop_lsb(&b), wksq, psq)];

    if (Us == WHITE && rank_of(psq) < RANK_7)
    {
        Square s = psq + DELTA_N;
        r |= db[index(BLACK, bksq, wksq, s)]; // Single push

        if (rank_of(s) == RANK_3 && s != wksq && s != bksq)
            r |= db[index(BLACK, bksq, wksq, s + DELTA_N)]; // Double push
    }

    if (Us == WHITE)
        return res = r & WIN  ? WIN  : r & UNKNOWN ? UNKNOWN : DRAW;
    else
        return res = r & DRAW ? DRAW : r & UNKNOWN ? UNKNOWN : WIN;
  }

} // namespace
