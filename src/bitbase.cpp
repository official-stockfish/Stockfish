/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2020 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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
#include <bitset>

#include "bitboard.h"
#include "types.h"

namespace {

  // There are 24 possible pawn squares: files A to D and ranks from 2 to 7.
  // Positions with the pawn on files E to H will be mirrored before probing.
  constexpr unsigned MAX_INDEX = 2*24*64*64; // stm * psq * wksq * bksq = 196608

  std::bitset<MAX_INDEX> KPKBitbase;

  // A KPK bitbase index is an integer in [0, IndexMax] range
  //
  // Information is mapped in a way that minimizes the number of iterations:
  //
  // bit  0- 5: white king square (from SQ_A1 to SQ_H8)
  // bit  6-11: black king square (from SQ_A1 to SQ_H8)
  // bit    12: side to move (WHITE or BLACK)
  // bit 13-14: white pawn file (from FILE_A to FILE_D)
  // bit 15-17: white pawn RANK_7 - rank (from RANK_7 - RANK_7 to RANK_7 - RANK_2)
  unsigned index(Color stm, Square bksq, Square wksq, Square psq) {
    return int(wksq) | (bksq << 6) | (stm << 12) | (file_of(psq) << 13) | ((RANK_7 - rank_of(psq)) << 15);
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
    Result classify(const std::vector<KPKPosition>& db);

    Color stm;
    Square ksq[COLOR_NB], psq;
    Result result;
  };

} // namespace


bool Bitbases::probe(Square wksq, Square wpsq, Square bksq, Color stm) {

  assert(file_of(wpsq) <= FILE_D);

  return KPKBitbase[index(stm, bksq, wksq, wpsq)];
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

  // Fill the bitbase with the decisive results
  for (idx = 0; idx < MAX_INDEX; ++idx)
      if (db[idx] == WIN)
          KPKBitbase.set(idx);
}


namespace {

  KPKPosition::KPKPosition(unsigned idx) {

    ksq[WHITE] = Square((idx >>  0) & 0x3F);
    ksq[BLACK] = Square((idx >>  6) & 0x3F);
    stm        = Color ((idx >> 12) & 0x01);
    psq        = make_square(File((idx >> 13) & 0x3), Rank(RANK_7 - ((idx >> 15) & 0x7)));

    // Check if two pieces are on the same square or if a king can be captured
    if (   distance(ksq[WHITE], ksq[BLACK]) <= 1
        || ksq[WHITE] == psq
        || ksq[BLACK] == psq
        || (stm == WHITE && (PawnAttacks[WHITE][psq] & ksq[BLACK])))
        result = INVALID;

    // Immediate win if a pawn can be promoted without getting captured
    else if (   stm == WHITE
             && rank_of(psq) == RANK_7
             && ksq[stm] != psq + NORTH
             && (    distance(ksq[~stm], psq + NORTH) > 1
                 || (PseudoAttacks[KING][ksq[stm]] & (psq + NORTH))))
        result = WIN;

    // Immediate draw if it is a stalemate or a king captures undefended pawn
    else if (   stm == BLACK
             && (  !(PseudoAttacks[KING][ksq[stm]] & ~(PseudoAttacks[KING][ksq[~stm]] | PawnAttacks[~stm][psq]))
                 || (PseudoAttacks[KING][ksq[stm]] & psq & ~PseudoAttacks[KING][ksq[~stm]])))
        result = DRAW;

    // Position will be classified later
    else
        result = UNKNOWN;
  }

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
    const Result Good = (stm == WHITE ? WIN   : DRAW);
    const Result Bad  = (stm == WHITE ? DRAW  : WIN);

    Result r = INVALID;
    Bitboard b = PseudoAttacks[KING][ksq[stm]];

    while (b)
        r |= stm == WHITE ? db[index(BLACK, ksq[BLACK] , pop_lsb(&b), psq)]
                          : db[index(WHITE, pop_lsb(&b),  ksq[WHITE], psq)];

    if (stm == WHITE)
    {
        if (rank_of(psq) < RANK_7)      // Single push
            r |= db[index(BLACK, ksq[BLACK], ksq[WHITE], psq + NORTH)];

        if (   rank_of(psq) == RANK_2   // Double push
            && psq + NORTH != ksq[WHITE]
            && psq + NORTH != ksq[BLACK])
            r |= db[index(BLACK, ksq[BLACK], ksq[WHITE], psq + NORTH + NORTH)];
    }

    return result = r & Good  ? Good  : r & UNKNOWN ? UNKNOWN : Bad;
  }

} // namespace
