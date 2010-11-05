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


////
//// Includes
////

#include <cassert>

#include "bitboard.h"
#include "square.h"


////
//// Local definitions
////

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
    Bitboard wk_attacks()   const { return StepAttackBB[WK][whiteKingSquare]; }
    Bitboard bk_attacks()   const { return StepAttackBB[BK][blackKingSquare]; }
    Bitboard pawn_attacks() const { return StepAttackBB[WP][pawnSquare]; }

    Square whiteKingSquare, blackKingSquare, pawnSquare;
    Color sideToMove;
  };

  const int IndexMax = 2 * 24 * 64 * 64;

  Result classify_wtm(const KPKPosition& pos, const Result bb[]);
  Result classify_btm(const KPKPosition& pos, const Result bb[]);
  int compute_index(Square wksq, Square bksq, Square psq, Color stm);
}


////
//// Functions
////

void generate_kpk_bitbase(uint8_t bitbase[]) {

  bool repeat;
  int i, j, b;
  KPKPosition pos;
  Result bb[IndexMax];

  // Initialize table
  for (i = 0; i < IndexMax; i++)
  {
      pos.from_index(i);
      bb[i] = !pos.is_legal()          ? RESULT_INVALID
             : pos.is_immediate_draw() ? RESULT_DRAW
             : pos.is_immediate_win()  ? RESULT_WIN : RESULT_UNKNOWN;
  }

  // Iterate until all positions are classified (30 cycles needed)
  do {
      repeat = false;

      for (i = 0; i < IndexMax; i++)
          if (bb[i] == RESULT_UNKNOWN)
          {
              pos.from_index(i);

              bb[i] = (pos.sideToMove == WHITE) ? classify_wtm(pos, bb)
                                                : classify_btm(pos, bb);
              if (bb[i] != RESULT_UNKNOWN)
                  repeat = true;
          }

  } while (repeat);

  // Compress result and map into supplied bitbase parameter
  for (i = 0; i < 24576; i++)
  {
      b = 0;
      for (j = 0; j < 8; j++)
          if (bb[8*i+j] == RESULT_WIN || bb[8*i+j] == RESULT_LOSS)
              b |= (1 << j);

      bitbase[i] = (uint8_t)b;
  }
}


namespace {

  int compute_index(Square wksq, Square bksq, Square psq, Color stm) {

      int p = int(square_file(psq)) + (int(square_rank(psq)) - 1) * 4;
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
        || pawnSquare == blackKingSquare)
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
        // Case 1: Stalemate
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
    int idx;

    // King moves
    b = pos.wk_attacks();
    while (b)
    {
        s = pop_1st_bit(&b);
        idx = compute_index(s, pos.blackKingSquare, pos.pawnSquare, BLACK);

        switch (bb[idx]) {

        case RESULT_LOSS:
            return RESULT_WIN;

        case RESULT_UNKNOWN:
            unknownFound = true;

        case RESULT_DRAW:
        case RESULT_INVALID:
             break;

         default:
             assert(false);
        }
    }

    // Pawn moves
    if (square_rank(pos.pawnSquare) < RANK_7)
    {
        s = pos.pawnSquare + DELTA_N;
        idx = compute_index(pos.whiteKingSquare, pos.blackKingSquare, s, BLACK);

        switch (bb[idx]) {

        case RESULT_LOSS:
            return RESULT_WIN;

        case RESULT_UNKNOWN:
            unknownFound = true;

        case RESULT_DRAW:
        case RESULT_INVALID:
            break;

        default:
            assert(false);
        }

        // Double pawn push
        if (   square_rank(s) == RANK_3
            && s != pos.whiteKingSquare
            && s != pos.blackKingSquare)
        {
            s += DELTA_N;
            idx = compute_index(pos.whiteKingSquare, pos.blackKingSquare, s, BLACK);

            switch (bb[idx]) {

            case RESULT_LOSS:
                return RESULT_WIN;

            case RESULT_UNKNOWN:
                unknownFound = true;

            case RESULT_DRAW:
            case RESULT_INVALID:
                break;

            default:
                assert(false);
            }
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
    int idx;

    // King moves
    b = pos.bk_attacks();
    while (b)
    {
        s = pop_1st_bit(&b);
        idx = compute_index(pos.whiteKingSquare, s, pos.pawnSquare, WHITE);

        switch (bb[idx]) {

        case RESULT_DRAW:
            return RESULT_DRAW;

        case RESULT_UNKNOWN:
            unknownFound = true;

        case RESULT_WIN:
        case RESULT_INVALID:
            break;

        default:
            assert(false);
        }
    }
    return unknownFound ? RESULT_UNKNOWN : RESULT_LOSS;
  }

}
