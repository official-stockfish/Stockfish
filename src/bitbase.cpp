/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#include <algorithm>
#include <cassert>
#include <numeric>
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace {

  // There are 24 possible pawn squares: the first 4 files and ranks from 2 to 7
  const unsigned MAX_INDEX = 2*24*64*64; // stm * psq * wksq * bksq = 196608

  // Each uint32_t stores results of 32 positions, one per bit
  uint32_t KPKBitbase[MAX_INDEX / 32];

  // A KPK bitbase index is an integer in [0, IndexMax] range
  //
  // Information is mapped in a way that minimizes the number of iterations:
  //
  // bit  0- 5: white king square (from SQ_A1 to SQ_H8)
  // bit  6-11: black king square (from SQ_A1 to SQ_H8)
  // bit    12: side to move (WHITE or BLACK)
  // bit 13-14: white pawn file (from FILE_A to FILE_D)
  // bit 15-17: white pawn RANK_7 - rank (from RANK_7 - RANK_7 to RANK_7 - RANK_2)
  unsigned index(Color us, Square bksq, Square wksq, Square psq) {
    return wksq | (bksq << 6) | (us << 12) | (file_of(psq) << 13) | ((RANK_7 - rank_of(psq)) << 15);
  }

  enum Result {
    INVALID = 0,
    UNKNOWN = 1,
    DRAW    = 2,
    WIN     = 4
  };

  Result& operator|=(Result& r, Result v) { return r = Result(r | v); }

  struct KPKPosition {
    KPKPosition() = default;
    explicit KPKPosition(unsigned idx);
    operator Result() const { return result; }
    Result classify(const std::vector<KPKPosition>& db)
    { return us == WHITE ? classify<WHITE>(db) : classify<BLACK>(db); }

    template<Color Us> Result classify(const std::vector<KPKPosition>& db);

    Color us;
    Square ksq[COLOR_NB], psq;
    Result result;
  };

} // namespace


bool Bitbases::probe(Square wksq, Square wpsq, Square bksq, Color us) {

  assert(file_of(wpsq) <= FILE_D);

  unsigned idx = index(us, bksq, wksq, wpsq);
  return KPKBitbase[idx / 32] & (1 << (idx & 0x1F));
}


void Bitbases::init() {

  std::vector<KPKPosition> db(MAX_INDEX);
  unsigned idx, repeat = 1;

  // Initialize db with known win / draw positions
  for (idx = 0; idx < MAX_INDEX; ++idx)
      db[idx] = KPKPosition(idx);

  // Iterate through the positions until none of the unknown positions can be
  // changed to either wins or draws (15 cycles needed).
  while (repeat)
      for (repeat = idx = 0; idx < MAX_INDEX; ++idx)
          repeat |= (db[idx] == UNKNOWN && db[idx].classify(db) != UNKNOWN);

  // Map 32 results into one KPKBitbase[] entry
  for (idx = 0; idx < MAX_INDEX; ++idx)
      if (db[idx] == WIN)
          KPKBitbase[idx / 32] |= 1 << (idx & 0x1F);
}


namespace {

  KPKPosition::KPKPosition(unsigned idx) {

    ksq[WHITE] = Square((idx >>  0) & 0x3F);
    ksq[BLACK] = Square((idx >>  6) & 0x3F);
    us         = Color ((idx >> 12) & 0x01);
    psq        = make_square(File((idx >> 13) & 0x3), Rank(RANK_7 - ((idx >> 15) & 0x7)));

    // Check if two pieces are on the same square or if a king can be captured
    if (   distance(ksq[WHITE], ksq[BLACK]) <= 1
        || ksq[WHITE] == psq
        || ksq[BLACK] == psq
        || (us == WHITE && (PawnAttacks[WHITE][psq] & ksq[BLACK])))
        result = INVALID;

    // Immediate win if a pawn can be promoted without getting captured
    else if (   us == WHITE
             && rank_of(psq) == RANK_7
             && ksq[us] != psq + NORTH
             && (    distance(ksq[~us], psq + NORTH) > 1
                 || (PseudoAttacks[KING][ksq[us]] & (psq + NORTH))))
        result = WIN;

    // Immediate draw if it is a stalemate or a king captures undefended pawn
    else if (   us == BLACK
             && (  !(PseudoAttacks[KING][ksq[us]] & ~(PseudoAttacks[KING][ksq[~us]] | PawnAttacks[~us][psq]))
                 || (PseudoAttacks[KING][ksq[us]] & psq & ~PseudoAttacks[KING][ksq[~us]])))
        result = DRAW;

    // Position will be classified later
    else
        result = UNKNOWN;
  }

  template<Color Us>
  Result KPKPosition::classify(const std::vector<KPKPosition>& db) {

    // White to move: If one move leads to a position classified as WIN, the result
    // of the current position is WIN. If all moves lead to positions classified
    // as DRAW, the current position is classified as DRAW, otherwise the current
    // position is classified as UNKNOWN.
    //
    // Black to move: If one move leads to a position classified as DRAW, the result
    // of the current position is DRAW. If all moves lead to positions classified
    // as WIN, the position is classified as WIN, otherwise the current position is
    // classified as UNKNOWN.

    const Color  Them = (Us == WHITE ? BLACK : WHITE);
    const Result Good = (Us == WHITE ? WIN   : DRAW);
    const Result Bad  = (Us == WHITE ? DRAW  : WIN);

    Result r = INVALID;
    Bitboard b = PseudoAttacks[KING][ksq[Us]];

    while (b)
        r |= Us == WHITE ? db[index(Them, ksq[Them]  , pop_lsb(&b), psq)]
                         : db[index(Them, pop_lsb(&b), ksq[Them]  , psq)];

    if (Us == WHITE)
    {
        if (rank_of(psq) < RANK_7)      // Single push
            r |= db[index(Them, ksq[Them], ksq[Us], psq + NORTH)];

        if (   rank_of(psq) == RANK_2   // Double push
            && psq + NORTH != ksq[Us]
            && psq + NORTH != ksq[Them])
            r |= db[index(Them, ksq[Them], ksq[Us], psq + NORTH + NORTH)];
    }

    return result = r & Good  ? Good  : r & UNKNOWN ? UNKNOWN : Bad;
  }

} // namespace
