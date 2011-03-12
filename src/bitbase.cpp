/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2010 Marco Costalba, Joona Kiiski, Tord Romstad

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
    RESULT_LOSS,
    RESULT_DRAW
  };

  struct KPKPosition {
    void from_index(int index);
    bool is_legal() const;
    bool is_immediate_draw() const;
    bool is_immediate_win() const;
    Bitboard wk_attacks()   const { return StepAttacksBB[WK][whiteKingSquare]; }
    Bitboard bk_attacks()   const { return StepAttacksBB[BK][blackKingSquare]; }
    Bitboard pawn_attacks() const { return StepAttacksBB[WP][pawnSquare]; }

    Square whiteKingSquare, blackKingSquare, pawnSquare;
    Color sideToMove;
  };

  // The possible pawns squares are 24, the first 4 files and ranks from 2 to 7
  const int IndexMax = 2 * 24 * 64 * 64; // color * wp_sq * wk_sq * bk_sq

  // Each uint32_t stores results of 32 positions, one per bit
  uint32_t KPKBitbase[IndexMax / 32];

  Result classify_wtm(const KPKPosition& pos, const Result bb[]);
  Result classify_btm(const KPKPosition& pos, const Result bb[]);
  int compute_index(Square wksq, Square bksq, Square wpsq, Color stm);
}


uint32_t probe_kpk_bitbase(Square wksq, Square wpsq, Square bksq, Color stm) {

  int index = compute_index(wksq, bksq, wpsq, stm);

  return KPKBitbase[index / 32] & (1 << (index & 31));
}


void init_kpk_bitbase() {

  Result bb[IndexMax];
  KPKPosition pos;
  bool repeat;

  // Initialize table
  for (int i = 0; i < IndexMax; i++)
  {
      pos.from_index(i);
      bb[i] = !pos.is_legal()          ? RESULT_INVALID
             : pos.is_immediate_draw() ? RESULT_DRAW
             : pos.is_immediate_win()  ? RESULT_WIN : RESULT_UNKNOWN;
  }

  // Iterate until all positions are classified (30 cycles needed)
  do {
      repeat = false;

      for (int i = 0; i < IndexMax; i++)
          if (bb[i] == RESULT_UNKNOWN)
          {
              pos.from_index(i);

              bb[i] = (pos.sideToMove == WHITE) ? classify_wtm(pos, bb)
                                                : classify_btm(pos, bb);
              if (bb[i] != RESULT_UNKNOWN)
                  repeat = true;
          }

  } while (repeat);

  // Map 32 position results into one KPKBitbase[] entry
  for (int i = 0; i < IndexMax / 32; i++)
      for (int j = 0; j < 32; j++)
          if (bb[32 * i + j] == RESULT_WIN || bb[32 * i + j] == RESULT_LOSS)
              KPKBitbase[i] |= (1 << j);
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

    assert(square_file(wpsq) <= FILE_D);

    int p = int(square_file(wpsq)) + 4 * int(square_rank(wpsq) - 1);
    int r = int(stm) + 2 * int(bksq) + 128 * int(wksq) + 8192 * p;

    assert(r >= 0 && r < IndexMax);

    return r;
  }

  void KPKPosition::from_index(int index) {

    int s = (index / 8192) % 24;

    sideToMove = Color(index % 2);
    blackKingSquare = Square((index / 2) % 64);
    whiteKingSquare = Square((index / 128) % 64);
    pawnSquare = make_square(File(s % 4), Rank(s / 4 + 1));
  }

  bool KPKPosition::is_legal() const {

    if (   whiteKingSquare == pawnSquare
        || whiteKingSquare == blackKingSquare
        || blackKingSquare == pawnSquare)
        return false;

    if (sideToMove == WHITE)
    {
        if (   bit_is_set(wk_attacks(), blackKingSquare)
            || bit_is_set(pawn_attacks(), blackKingSquare))
            return false;
    }
    else if (bit_is_set(bk_attacks(), whiteKingSquare))
        return false;

    return true;
  }

  bool KPKPosition::is_immediate_draw() const {

    if (sideToMove == BLACK)
    {
        Bitboard wka = wk_attacks();
        Bitboard bka = bk_attacks();

        // Case 1: Stalemate
        if ((bka & ~(wka | pawn_attacks())) == EmptyBoardBB)
            return true;

        // Case 2: King can capture pawn
        if (bit_is_set(bka, pawnSquare) && !bit_is_set(wka, pawnSquare))
            return true;
    }
    else
    {
        // Case 1: Stalemate (possible pawn files are only from A to D)
        if (   whiteKingSquare == SQ_A8
            && pawnSquare == SQ_A7
            && (blackKingSquare == SQ_C7 || blackKingSquare == SQ_C8))
            return true;
    }
    return false;
  }

  bool KPKPosition::is_immediate_win() const {

    // The position is an immediate win if it is white to move and the
    // white pawn can be promoted without getting captured.
    return   sideToMove == WHITE
          && square_rank(pawnSquare) == RANK_7
          && whiteKingSquare != pawnSquare + DELTA_N
          && (   square_distance(blackKingSquare, pawnSquare + DELTA_N) > 1
              || bit_is_set(wk_attacks(), pawnSquare + DELTA_N));
  }

  Result classify_wtm(const KPKPosition& pos, const Result bb[]) {

    // If one move leads to a position classified as RESULT_LOSS, the result
    // of the current position is RESULT_WIN. If all moves lead to positions
    // classified as RESULT_DRAW, the current position is classified RESULT_DRAW
    // otherwise the current position is classified as RESULT_UNKNOWN.

    bool unknownFound = false;
    Bitboard b;
    Square s;
    Result r;

    // King moves
    b = pos.wk_attacks();
    while (b)
    {
        s = pop_1st_bit(&b);
        r = bb[compute_index(s, pos.blackKingSquare, pos.pawnSquare, BLACK)];

        if (r == RESULT_LOSS)
            return RESULT_WIN;

        if (r == RESULT_UNKNOWN)
            unknownFound = true;
    }

    // Pawn moves
    if (square_rank(pos.pawnSquare) < RANK_7)
    {
        s = pos.pawnSquare + DELTA_N;
        r = bb[compute_index(pos.whiteKingSquare, pos.blackKingSquare, s, BLACK)];

        if (r == RESULT_LOSS)
            return RESULT_WIN;

        if (r == RESULT_UNKNOWN)
            unknownFound = true;

        // Double pawn push
        if (   square_rank(s) == RANK_3
            && s != pos.whiteKingSquare
            && s != pos.blackKingSquare)
        {
            s += DELTA_N;
            r = bb[compute_index(pos.whiteKingSquare, pos.blackKingSquare, s, BLACK)];

            if (r == RESULT_LOSS)
                return RESULT_WIN;

            if (r == RESULT_UNKNOWN)
                unknownFound = true;
        }
    }
    return unknownFound ? RESULT_UNKNOWN : RESULT_DRAW;
  }


  Result classify_btm(const KPKPosition& pos, const Result bb[]) {

    // If one move leads to a position classified as RESULT_DRAW, the result
    // of the current position is RESULT_DRAW. If all moves lead to positions
    // classified as RESULT_WIN, the current position is classified as
    // RESULT_LOSS. Otherwise, the current position is classified as
    // RESULT_UNKNOWN.

    bool unknownFound = false;
    Bitboard b;
    Square s;
    Result r;

    // King moves
    b = pos.bk_attacks();
    while (b)
    {
        s = pop_1st_bit(&b);
        r = bb[compute_index(pos.whiteKingSquare, s, pos.pawnSquare, WHITE)];

        if (r == RESULT_DRAW)
            return RESULT_DRAW;

        if (r == RESULT_UNKNOWN)
            unknownFound = true;
    }
    return unknownFound ? RESULT_UNKNOWN : RESULT_LOSS;
  }

}
