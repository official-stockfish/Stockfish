/*
  Glaurung, a UCI chess playing engine.
  Copyright (C) 2004-2008 Tord Romstad

  Glaurung is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  Glaurung is distributed in the hope that it will be useful,
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

#include "bitbase.h"
#include "bitboard.h"
#include "move.h"
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
    int to_index() const;
    bool is_legal() const;
    bool is_immediate_draw() const;
    bool is_immediate_win() const;
    Bitboard wk_attacks() const;
    Bitboard bk_attacks() const;
    Bitboard pawn_attacks() const;
    
    Square whiteKingSquare, blackKingSquare, pawnSquare;
    Color sideToMove;
  };
    

  Result *Bitbase;
  const int IndexMax = 2*24*64*64;
  int UnknownCount = 0;

  void initialize();
  bool next_iteration();
  Result classify_wtm(const KPKPosition &p);
  Result classify_btm(const KPKPosition &p);
  int compute_index(Square wksq, Square bksq, Square psq, Color stm);
  int compress_result(Result r);
  
}


////
//// Functions
////

void generate_kpk_bitbase(uint8_t bitbase[]) {
  // Allocate array and initialize:
  Bitbase = new Result[IndexMax];
  initialize();

  // Iterate until all positions are classified:
  while(next_iteration());

  // Compress bitbase into the supplied parameter:
  int i, j, b;
  for(i = 0; i < 24576; i++) {
    for(b = 0, j = 0; j < 8; b |= (compress_result(Bitbase[8*i+j]) << j), j++);
    bitbase[i] = b;
  }

  // Release allocated memory:
  delete [] Bitbase;
}


namespace {

  void KPKPosition::from_index(int index) {
    int s;
    sideToMove = Color(index % 2);
    blackKingSquare = Square((index / 2) % 64);
    whiteKingSquare = Square((index / 128) % 64);
    s = (index / 8192) % 24;
    pawnSquare = make_square(File(s % 4), Rank(s / 4 + 1));
  }


  int KPKPosition::to_index() const {
    return compute_index(whiteKingSquare, blackKingSquare, pawnSquare,
                         sideToMove);
  }
    

  bool KPKPosition::is_legal() const {
    if(whiteKingSquare == pawnSquare || whiteKingSquare == blackKingSquare ||
       pawnSquare == blackKingSquare)
      return false;
    if(sideToMove == WHITE) {
      if(bit_is_set(this->wk_attacks(), blackKingSquare))
        return false;
      if(bit_is_set(this->pawn_attacks(), blackKingSquare))
        return false;
    }
    else {
      if(bit_is_set(this->bk_attacks(), whiteKingSquare))
        return false;
    }
    return true;
  }


  bool KPKPosition::is_immediate_draw() const {
    if(sideToMove == BLACK) {
      Bitboard wka = this->wk_attacks();
      Bitboard bka = this->bk_attacks();
    
      // Case 1: Stalemate
      if((bka & ~(wka | this->pawn_attacks())) == EmptyBoardBB)
        return true;

      // Case 2: King can capture pawn
      if(bit_is_set(bka, pawnSquare) && !bit_is_set(wka, pawnSquare))
        return true;
    }
    else {
      // Case 1: Stalemate
      if(whiteKingSquare == SQ_A8 && pawnSquare == SQ_A7 &&
         (blackKingSquare == SQ_C7 || blackKingSquare == SQ_C8))
        return true;
    }

    return false;
  }


  bool KPKPosition::is_immediate_win() const {
    // The position is an immediate win if it is white to move and the white
    // pawn can be promoted without getting captured:
    return
      sideToMove == WHITE &&
      square_rank(pawnSquare) == RANK_7 &&
      (square_distance(blackKingSquare, pawnSquare+DELTA_N) > 1 ||
       bit_is_set(this->wk_attacks(), pawnSquare+DELTA_N));
  }
    

  Bitboard KPKPosition::wk_attacks() const {
    return StepAttackBB[WK][whiteKingSquare];
  }


  Bitboard KPKPosition::bk_attacks() const {
    return StepAttackBB[BK][blackKingSquare];
  }


  Bitboard KPKPosition::pawn_attacks() const {
    return StepAttackBB[WP][pawnSquare];
  }


  void initialize() {
    KPKPosition p;
    for(int i = 0; i < IndexMax; i++) {
      p.from_index(i);
      if(!p.is_legal())
        Bitbase[i] = RESULT_INVALID;
      else if(p.is_immediate_draw())
        Bitbase[i] = RESULT_DRAW;
      else if(p.is_immediate_win())
        Bitbase[i] = RESULT_WIN;
      else {
        Bitbase[i] = RESULT_UNKNOWN;
        UnknownCount++;
      }
    }
  }


  bool next_iteration() {
    KPKPosition p;
    int previousUnknownCount = UnknownCount;
    
    for(int i = 0; i < IndexMax; i++)
      if(Bitbase[i] == RESULT_UNKNOWN) {
        p.from_index(i);

        Bitbase[i] = (p.sideToMove == WHITE)? classify_wtm(p) : classify_btm(p);

        if(Bitbase[i] == RESULT_WIN || Bitbase[i] == RESULT_LOSS ||
           Bitbase[i] == RESULT_DRAW)
          UnknownCount--;
      }

    return UnknownCount != previousUnknownCount;
  }


  Result classify_wtm(const KPKPosition &p) {

    // If one move leads to a position classified as RESULT_LOSS, the result
    // of the current position is RESULT_WIN.  If all moves lead to positions
    // classified as RESULT_DRAW, the current position is classified as
    // RESULT_DRAW.  Otherwise, the current position is classified as
    // RESULT_UNKNOWN.

    bool unknownFound = false;
    Bitboard b;
    Square s;
    
    // King moves
    b = p.wk_attacks();
    while(b) {
      s = pop_1st_bit(&b);
      switch(Bitbase[compute_index(s, p.blackKingSquare, p.pawnSquare,
                                   BLACK)]) {
      case RESULT_LOSS:
        return RESULT_WIN;

      case RESULT_UNKNOWN:
        unknownFound = true;
        break;

      case RESULT_DRAW: case RESULT_INVALID:
        break;

      default:
        assert(false);
      }
    }

    // Pawn moves
    if(square_rank(p.pawnSquare) < RANK_7) {
      s = p.pawnSquare + DELTA_N;
      switch(Bitbase[compute_index(p.whiteKingSquare, p.blackKingSquare, s,
                                   BLACK)]) {
      case RESULT_LOSS:
        return RESULT_WIN;
        
      case RESULT_UNKNOWN:
        unknownFound = true;
        break;
          
      case RESULT_DRAW: case RESULT_INVALID:
        break;
        
      default:
        assert(false);
      }

      if(square_rank(s) == RANK_3 &&
         s != p.whiteKingSquare && s != p.blackKingSquare) {
        s += DELTA_N;
        switch(Bitbase[compute_index(p.whiteKingSquare, p.blackKingSquare, s,
                                     BLACK)]) {
        case RESULT_LOSS:
          return RESULT_WIN;
          
        case RESULT_UNKNOWN:
          unknownFound = true;
          break;
          
        case RESULT_DRAW: case RESULT_INVALID:
          break;
          
        default:
          assert(false);
        }
      }
    }
    
    return unknownFound? RESULT_UNKNOWN : RESULT_DRAW;
  }


  Result classify_btm(const KPKPosition &p) {

    // If one move leads to a position classified as RESULT_DRAW, the result
    // of the current position is RESULT_DRAW.  If all moves lead to positions
    // classified as RESULT_WIN, the current position is classified as
    // RESULT_LOSS.  Otherwise, the current position is classified as
    // RESULT_UNKNOWN.

    bool unknownFound = false;
    Bitboard b;
    Square s;

    // King moves
    b = p.bk_attacks();
    while(b) {
      s = pop_1st_bit(&b);
      switch(Bitbase[compute_index(p.whiteKingSquare, s, p.pawnSquare,
                                   WHITE)]) {
      case RESULT_DRAW:
        return RESULT_DRAW;

      case RESULT_UNKNOWN:
        unknownFound = true;
        break;

      case RESULT_WIN: case RESULT_INVALID:
        break;

      default:
        assert(false);
      }
    }

    return unknownFound? RESULT_UNKNOWN : RESULT_LOSS;
  }


  int compute_index(Square wksq, Square bksq, Square psq, Color stm) {
    int p = int(square_file(psq)) + (int(square_rank(psq)) - 1) * 4;
    int result = int(stm) + 2*int(bksq) + 128*int(wksq) + 8192*p;
    assert(result >= 0 && result < IndexMax);
    return result;
  }


  int compress_result(Result r) {
    return (r == RESULT_WIN || r == RESULT_LOSS)? 1 : 0;
  }
      
}  
