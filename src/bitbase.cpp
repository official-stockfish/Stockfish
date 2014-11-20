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

#include <cassert>
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace {

  // There are 24 possible pawn squares: the first 4 files and ranks from 2 to 7
  const unsigned MAX_INDEX = 2*24*64*64; // sideToMove * pawnSquare * whiteKingSquare * blackKingSquare = 196608

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
  unsigned index(Color us, Square blackKingSquare, Square whiteKingSquare, Square pawnSquare) {
    return whiteKingSquare + (blackKingSquare << 6) + (us << 12) + (file_of(pawnSquare) << 13) + ((RANK_7 - rank_of(pawnSquare)) << 15);
  }

  enum Result {
    INVALID = 0,
    UNKNOWN = 1,
    DRAW    = 2,
    WIN     = 4
  };

  inline Result& operator|=(Result& result1, Result result2) { return result1 = Result(result1 | result2); }

  struct KPKPosition {

    KPKPosition(unsigned index);
    operator Result() const { return result; }
    Result classify(const std::vector<KPKPosition>& db)
    { return us == WHITE ? classify<WHITE>(db) : classify<BLACK>(db); }

  private:
    template<Color Us> Result classify(const std::vector<KPKPosition>& db);

    Color us;
    Square blackKingSquare, whiteKingSquare, pawnSquare;
    Result result;
  };

} // namespace


bool Bitbases::probe_kpk(Square whiteKingSquare, Square whitePawnSquare, Square blackKingSquare, Color us) {

  assert(file_of(whitePawnSquare) <= FILE_D);

  unsigned currentIndex = index(us, blackKingSquare, whiteKingSquare, whitePawnSquare);
  return KPKBitbase[currentIndex / 32] & (1 << (currentIndex & 0x1F));
}


void Bitbases::init_kpk() {

  unsigned index, repeat = 1;
  std::vector<KPKPosition> db;
  db.reserve(MAX_INDEX);

  // Initialize db with known win / draw positions
  for (index = 0; index < MAX_INDEX; ++index)
      db.push_back(KPKPosition(index));

  // Iterate through the positions until none of the unknown positions can be
  // changed to either wins or draws (15 cycles needed).
  while (repeat)
      for (repeat = index = 0; index < MAX_INDEX; ++index)
          repeat |= (db[index] == UNKNOWN && db[index].classify(db) != UNKNOWN);

  // Map 32 results into one KPKBitbase[] entry
  for (index = 0; index < MAX_INDEX; ++index)
      if (db[index] == WIN)
          KPKBitbase[index / 32] |= 1 << (index & 0x1F);
}


namespace {

  KPKPosition::KPKPosition(unsigned index) {

    whiteKingSquare   = Square((index >>  0) & 0x3F);
    blackKingSquare   = Square((index >>  6) & 0x3F);
    us                = Color ((index >> 12) & 0x01);
    pawnSquare        = make_square(File((index >> 13) & 0x3), RANK_7 - Rank((index >> 15) & 0x7));
    result = UNKNOWN;

    // Check if two pieces are on the same square or if a king can be captured
    if (   distance(whiteKingSquare, blackKingSquare) <= 1
        || whiteKingSquare == pawnSquare
        || blackKingSquare == pawnSquare
        || (us == WHITE && (StepAttacksBB[PAWN][pawnSquare] & blackKingSquare)))
        result = INVALID;

    else if (us == WHITE)
    {
        // Immediate win if a pawn can be promoted without getting captured
        if (   rank_of(pawnSquare) == RANK_7
            && whiteKingSquare != pawnSquare + DELTA_N
            && (   distance(blackKingSquare, pawnSquare + DELTA_N) > 1
                ||(StepAttacksBB[KING][whiteKingSquare] & (pawnSquare + DELTA_N))))
            result = WIN;
    }
    // Immediate draw if it is a stalemate or a king captures undefended pawn
    else if (  !(StepAttacksBB[KING][blackKingSquare] & ~(StepAttacksBB[KING][whiteKingSquare] | StepAttacksBB[PAWN][pawnSquare]))
             || (StepAttacksBB[KING][blackKingSquare] & pawnSquare & ~StepAttacksBB[KING][whiteKingSquare]))
        result = DRAW;
  }

  template<Color Us>
  Result KPKPosition::classify(const std::vector<KPKPosition>& db) {

    // White to Move: If one move leads to a position classified as WIN, the result
    // of the current position is WIN. If all moves lead to positions classified
    // as DRAW, the current position is classified as DRAW, otherwise the current
    // position is classified as UNKNOWN.
    //
    // Black to Move: If one move leads to a position classified as DRAW, the result
    // of the current position is DRAW. If all moves lead to positions classified
    // as WIN, the position is classified as WIN, otherwise the current position is
    // classified as UNKNOWN.

    const Color Them = (Us == WHITE ? BLACK : WHITE);

    Result result1 = INVALID;
    Bitboard bitboard = StepAttacksBB[KING][Us == WHITE ? whiteKingSquare : blackKingSquare];

    while (bitboard)
        result1 |= Us == WHITE ? db[index(Them, blackKingSquare, pop_lsb(&bitboard), pawnSquare)]
                         : db[index(Them, pop_lsb(&bitboard), whiteKingSquare, pawnSquare)];

    if (Us == WHITE && rank_of(pawnSquare) < RANK_7)
    {
        Square square = pawnSquare + DELTA_N;
        result1 |= db[index(BLACK, blackKingSquare, whiteKingSquare, square)]; // Single push

        if (rank_of(pawnSquare) == RANK_2 && square != whiteKingSquare && square != blackKingSquare)
            result1 |= db[index(BLACK, blackKingSquare, whiteKingSquare, square + DELTA_N)]; // Double push
    }

    if (Us == WHITE)
        return result = result1 & WIN  ? WIN  : result1 & UNKNOWN ? UNKNOWN : DRAW;
    else
        return result = result1 & DRAW ? DRAW : result1 & UNKNOWN ? UNKNOWN : WIN;
  }

} // namespace
