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
    RESULT_UNKNOWN,
    RESULT_INVALID,
    RESULT_WIN,
    RESULT_DRAW
  };

  struct KPKPosition {
    Result classify_knowns(int index);
    Result classify(int index, Result db[]);

  private:
    void from_index(int index);
    Result classify_white(const Result db[]);
    Result classify_black(const Result db[]);
    Bitboard wk_attacks()   const { return StepAttacksBB[W_KING][whiteKingSquare]; }
    Bitboard bk_attacks()   const { return StepAttacksBB[B_KING][blackKingSquare]; }
    Bitboard pawn_attacks() const { return StepAttacksBB[W_PAWN][pawnSquare]; }

    Square whiteKingSquare, blackKingSquare, pawnSquare;
    Color sideToMove;
  };

  // The possible pawns squares are 24, the first 4 files and ranks from 2 to 7
  const int IndexMax = 2 * 24 * 64 * 64; // color * wp_sq * wk_sq * bk_sq = 196608

  // Each uint32_t stores results of 32 positions, one per bit
  uint32_t KPKBitbase[IndexMax / 32];

  int compute_index(Square wksq, Square bksq, Square wpsq, Color stm);
}


uint32_t probe_kpk_bitbase(Square wksq, Square wpsq, Square bksq, Color stm) {

  int index = compute_index(wksq, bksq, wpsq, stm);

  return KPKBitbase[index / 32] & (1 << (index & 31));
}


void kpk_bitbase_init() {

  Result db[IndexMax];
  KPKPosition pos;
  int index, bit, repeat = 1;

  // Initialize table
  for (index = 0; index < IndexMax; index++)
      db[index] = pos.classify_knowns(index);

  // Iterate until all positions are classified (30 cycles needed)
  while (repeat)
      for (repeat = index = 0; index < IndexMax; index++)
          if (   db[index] == RESULT_UNKNOWN
              && pos.classify(index, db) != RESULT_UNKNOWN)
              repeat = 1;

  // Map 32 position results into one KPKBitbase[] entry
  for (index = 0; index < IndexMax / 32; index++)
      for (bit = 0; bit < 32; bit++)
          if (db[32 * index + bit] == RESULT_WIN)
              KPKBitbase[index] |= (1 << bit);
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

  int compute_index(Square wksq, Square bksq, Square wpsq, Color stm) {

    assert(file_of(wpsq) <= FILE_D);

    int p = file_of(wpsq) + 4 * (rank_of(wpsq) - 1);
    int r = stm + 2 * bksq + 128 * wksq + 8192 * p;

    assert(r >= 0 && r < IndexMax);

    return r;
  }

  void KPKPosition::from_index(int index) {

    int s = index >> 13;
    sideToMove = Color(index & 1);
    blackKingSquare = Square((index >> 1) & 63);
    whiteKingSquare = Square((index >> 7) & 63);
    pawnSquare = make_square(File(s & 3), Rank((s >> 2) + 1));
  }

  Result KPKPosition::classify_knowns(int index) {

    from_index(index);

    // Check if two pieces are on the same square
    if (   whiteKingSquare == pawnSquare
        || whiteKingSquare == blackKingSquare
        || blackKingSquare == pawnSquare)
        return RESULT_INVALID;

    // Check if a king can be captured
    if (    bit_is_set(wk_attacks(), blackKingSquare)
        || (bit_is_set(pawn_attacks(), blackKingSquare) && sideToMove == WHITE))
        return RESULT_INVALID;

    // The position is an immediate win if it is white to move and the
    // white pawn can be promoted without getting captured.
    if (   rank_of(pawnSquare) == RANK_7
        && sideToMove == WHITE
        && whiteKingSquare != pawnSquare + DELTA_N
        && (   square_distance(blackKingSquare, pawnSquare + DELTA_N) > 1
            || bit_is_set(wk_attacks(), pawnSquare + DELTA_N)))
        return RESULT_WIN;

    // Check for known draw positions
    //
    // Case 1: Stalemate
    if (   sideToMove == BLACK
        && !(bk_attacks() & ~(wk_attacks() | pawn_attacks())))
        return RESULT_DRAW;

    // Case 2: King can capture pawn
    if (   sideToMove == BLACK
        && bit_is_set(bk_attacks(), pawnSquare) && !bit_is_set(wk_attacks(), pawnSquare))
        return RESULT_DRAW;

    // Case 3: Black king in front of white pawn
    if (   blackKingSquare == pawnSquare + DELTA_N
        && rank_of(pawnSquare) < RANK_7)
        return RESULT_DRAW;

    //  Case 4: White king in front of pawn and black has opposition
    if (   whiteKingSquare == pawnSquare + DELTA_N
        && blackKingSquare == pawnSquare + DELTA_N + DELTA_N + DELTA_N
        && rank_of(pawnSquare) < RANK_5
        && sideToMove == WHITE)
        return RESULT_DRAW;

    // Case 5: Stalemate with rook pawn
    if (   blackKingSquare == SQ_A8
        && file_of(pawnSquare) == FILE_A)
        return RESULT_DRAW;

    return RESULT_UNKNOWN;
  }

  Result KPKPosition::classify(int index, Result db[]) {

    from_index(index);
    db[index] = (sideToMove == WHITE ? classify_white(db) : classify_black(db));
    return db[index];
  }

  Result KPKPosition::classify_white(const Result db[]) {

    // If one move leads to a position classified as RESULT_WIN, the result
    // of the current position is RESULT_WIN. If all moves lead to positions
    // classified as RESULT_DRAW, the current position is classified RESULT_DRAW
    // otherwise the current position is classified as RESULT_UNKNOWN.

    bool unknownFound = false;
    Bitboard b;
    Square s;
    Result r;

    // King moves
    b = wk_attacks();
    while (b)
    {
        s = pop_1st_bit(&b);
        r = db[compute_index(s, blackKingSquare, pawnSquare, BLACK)];

        if (r == RESULT_WIN)
            return RESULT_WIN;

        if (r == RESULT_UNKNOWN)
            unknownFound = true;
    }

    // Pawn moves
    if (rank_of(pawnSquare) < RANK_7)
    {
        s = pawnSquare + DELTA_N;
        r = db[compute_index(whiteKingSquare, blackKingSquare, s, BLACK)];

        if (r == RESULT_WIN)
            return RESULT_WIN;

        if (r == RESULT_UNKNOWN)
            unknownFound = true;

        // Double pawn push
        if (rank_of(s) == RANK_3 && r != RESULT_INVALID)
        {
            s += DELTA_N;
            r = db[compute_index(whiteKingSquare, blackKingSquare, s, BLACK)];

            if (r == RESULT_WIN)
                return RESULT_WIN;

            if (r == RESULT_UNKNOWN)
                unknownFound = true;
        }
    }
    return unknownFound ? RESULT_UNKNOWN : RESULT_DRAW;
  }

  Result KPKPosition::classify_black(const Result db[]) {

    // If one move leads to a position classified as RESULT_DRAW, the result
    // of the current position is RESULT_DRAW. If all moves lead to positions
    // classified as RESULT_WIN, the position is classified as RESULT_WIN.
    // Otherwise, the current position is classified as RESULT_UNKNOWN.

    bool unknownFound = false;
    Bitboard b;
    Square s;
    Result r;

    // King moves
    b = bk_attacks();
    while (b)
    {
        s = pop_1st_bit(&b);
        r = db[compute_index(whiteKingSquare, s, pawnSquare, WHITE)];

        if (r == RESULT_DRAW)
            return RESULT_DRAW;

        if (r == RESULT_UNKNOWN)
            unknownFound = true;
    }
    return unknownFound ? RESULT_UNKNOWN : RESULT_WIN;
  }

}
