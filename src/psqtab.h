/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008 Marco Costalba

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


#if !defined(PSQTAB_H_INCLUDED)
#define PSQTAB_H_INCLUDED

////
//// Includes
////

#include "position.h"
#include "value.h"


////
//// Variables
////

static const Value MP = PawnValueMidgame;
static const Value MK = KnightValueMidgame;
static const Value MB = BishopValueMidgame;
static const Value MR = RookValueMidgame;
static const Value MQ = QueenValueMidgame;

static const int MgPST[][64] = {
  { },
  {// Pawn
   // A      B      C      D      E      F      G      H
        0,     0,     0,     0,     0,     0,     0,     0,
    MP-38, MP-12, MP- 0, MP+12, MP+12, MP- 0, MP-12, MP-38,
    MP-38, MP-12, MP+ 6, MP+38, MP+38, MP+ 6, MP-12, MP-38,
    MP-38, MP-12, MP+16, MP+64, MP+64, MP+16, MP-12, MP-38,
    MP-38, MP-12, MP+16, MP+38, MP+38, MP+16, MP-12, MP-38,
    MP-38, MP-12, MP+ 6, MP+12, MP+12, MP+ 6, MP-12, MP-38,
    MP-38, MP-12, MP- 0, MP+12, MP+12, MP- 0, MP-12, MP-38,
        0,     0,     0,     0,     0,     0,     0,     0
  },
  {// Knight
   //  A      B       C      D      E      F      G       H
    MK-128, MK-102, MK-76, MK-64, MK-64, MK-76, MK-102, MK-128,
    MK- 89, MK- 64, MK-38, MK-25, MK-25, MK-38, MK- 64, MK- 89,
    MK- 51, MK- 25, MK- 0, MK+12, MK+12, MK- 0, MK- 25, MK- 51,
    MK- 25, MK-  0, MK+25, MK+38, MK+38, MK+25, MK-  0, MK- 25,
    MK- 12, MK+ 12, MK+38, MK+51, MK+51, MK+38, MK+ 12, MK- 12,
    MK- 12, MK+ 12, MK+38, MK+51, MK+51, MK+38, MK+ 12, MK- 12,
    MK- 51, MK- 25, MK- 0, MK+12, MK+12, MK- 0, MK- 25, MK- 51,
    MK-182, MK- 64, MK-38, MK-25, MK-25, MK-38, MK- 64, MK-182
  },
  {// Bishop
   // A      B      C      D      E      F      G      H
    MB-46, MB-46, MB-40, MB-35, MB-35, MB-40, MB-46, MB-46,
    MB-20, MB- 0, MB- 5, MB- 0, MB- 0, MB- 5, MB- 0, MB-20,
    MB-15, MB- 5, MB+10, MB+ 5, MB+ 5, MB+10, MB- 5, MB-15,
    MB-10, MB- 0, MB+ 5, MB+20, MB+20, MB+ 5, MB- 0, MB-10,
    MB-10, MB- 0, MB+ 5, MB+20, MB+20, MB+ 5, MB- 0, MB-10,
    MB-15, MB- 5, MB+10, MB+ 5, MB+ 5, MB+10, MB- 5, MB-15,
    MB-20, MB- 0, MB- 5, MB- 0, MB- 0, MB- 5, MB- 0, MB-20,
    MB-20, MB-20, MB-15, MB-10, MB-10, MB-15, MB-20, MB-20
  },
  {// Rook
   // A      B      C     D     E     F     G      H
    MR-18, MR-10, MR-3, MR+4, MR+4, MR-3, MR-10, MR-18,
    MR-18, MR-10, MR-3, MR+4, MR+4, MR-3, MR-10, MR-18,
    MR-18, MR-10, MR-3, MR+4, MR+4, MR-3, MR-10, MR-18,
    MR-18, MR-10, MR-3, MR+4, MR+4, MR-3, MR-10, MR-18,
    MR-18, MR-10, MR-3, MR+4, MR+4, MR-3, MR-10, MR-18,
    MR-18, MR-10, MR-3, MR+4, MR+4, MR-3, MR-10, MR-18,
    MR-18, MR-10, MR-3, MR+4, MR+4, MR-3, MR-10, MR-18,
    MR-18, MR-10, MR-3, MR+4, MR+4, MR-3, MR-10, MR-18
  },
  {// Queen
   //A   B   C   D   E   F   G   H
    MQ, MQ, MQ, MQ, MQ, MQ, MQ, MQ,
    MQ, MQ, MQ, MQ, MQ, MQ, MQ, MQ,
    MQ, MQ, MQ, MQ, MQ, MQ, MQ, MQ,
    MQ, MQ, MQ, MQ, MQ, MQ, MQ, MQ,
    MQ, MQ, MQ, MQ, MQ, MQ, MQ, MQ,
    MQ, MQ, MQ, MQ, MQ, MQ, MQ, MQ,
    MQ, MQ, MQ, MQ, MQ, MQ, MQ, MQ,
    MQ, MQ, MQ, MQ, MQ, MQ, MQ, MQ
  },
  {// King
   //A    B    C    D    E    F    G    H
    302, 328, 276, 225, 225, 276, 328, 302,
    276, 302, 251, 200, 200, 251, 302, 276,
    225, 251, 200, 149, 149, 200, 251, 225,
    200, 225, 175, 124, 124, 175, 225, 200,
    175, 200, 149,  98,  98, 149, 200, 175,
    149, 175, 124,  72,  72, 124, 175, 149,
    124, 149,  98,  47,  47,  98, 149, 124,
     98, 124,  72,  21,  21,  72, 124,  98
  }
};

static const Value EP = PawnValueEndgame;
static const Value EK = KnightValueEndgame;
static const Value EB = BishopValueEndgame;
static const Value ER = RookValueEndgame;
static const Value EQ = QueenValueEndgame;

static const int EgPST[][64] = {
  { },
  {// Pawn
   //A   B   C   D   E   F   G   H
     0,  0,  0,  0,  0,  0,  0,  0,
    EP, EP, EP, EP, EP, EP, EP, EP,
    EP, EP, EP, EP, EP, EP, EP, EP,
    EP, EP, EP, EP, EP, EP, EP, EP,
    EP, EP, EP, EP, EP, EP, EP, EP,
    EP, EP, EP, EP, EP, EP, EP, EP,
    EP, EP, EP, EP, EP, EP, EP, EP,
     0,  0,  0,  0,  0,  0,  0,  0
  },
  {// Knight
   // A       B      C      D      E      F      G      H
    EK-102, EK-76, EK-51, EK-38, EK-38, EK-51, EK-76, EK-102,
    EK- 76, EK-51, EK-25, EK-12, EK-12, EK-25, EK-51, EK-76,
    EK- 51, EK-25, EK- 0, EK+12, EK+12, EK- 0, EK-25, EK-51,
    EK- 38, EK-12, EK+12, EK+25, EK+25, EK+12, EK-12, EK-38,
    EK- 38, EK-12, EK+12, EK+25, EK+25, EK+12, EK-12, EK-38,
    EK- 51, EK-25, EK- 0, EK+12, EK+12, EK- 0, EK-25, EK-51,
    EK- 76, EK-51, EK-25, EK-12, EK-12, EK-25, EK-51, EK-76,
    EK-102, EK-76, EK-51, EK-38, EK-38, EK-51, EK-76, EK-102
  },
  {// Bishop
   // A      B      C      D      E      F      G      H
    EB-46, EB-30, EB-23, EB-15, EB-15, EB-23, EB-30, EB-46,
    EB-30, EB-15, EB- 7, EB- 0, EB- 0, EB- 7, EB-15, EB-30,
    EB-23, EB- 7, EB- 0, EB+ 7, EB+ 7, EB- 0, EB- 7, EB-23,
    EB-15, EB- 0, EB+ 7, EB+15, EB+15, EB+ 7, EB- 0, EB-15,
    EB-15, EB- 0, EB+ 7, EB+15, EB+15, EB+ 7, EB- 0, EB-15,
    EB-23, EB- 7, EB- 0, EB+ 7, EB+ 7, EB- 0, EB- 7, EB-23,
    EB-30, EB-15, EB- 7, EB- 0, EB- 0, EB- 7, EB-15, EB-30,
    EB-46, EB-30, EB-23, EB-15, EB-15, EB-23, EB-30, EB-46
  },
  {// Rook
   // A     B     C     D     E     F     G     H
    ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3,
    ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3,
    ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3,
    ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3,
    ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3,
    ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3,
    ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3,
    ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3, ER-3
  },
  {// Queen
   // A      B      C      D      E      F      G      H
    EQ-61, EQ-40, EQ-30, EQ-20, EQ-20, EQ-30, EQ-40, EQ-61,
    EQ-40, EQ-20, EQ-10, EQ- 0, EQ- 0, EQ-10, EQ-20, EQ-40,
    EQ-30, EQ-10, EQ- 0, EQ+10, EQ+10, EQ- 0, EQ-10, EQ-30,
    EQ-20, EQ- 0, EQ+10, EQ+20, EQ+20, EQ+10, EQ- 0, EQ-20,
    EQ-20, EQ- 0, EQ+10, EQ+20, EQ+20, EQ+10, EQ- 0, EQ-20,
    EQ-30, EQ-10, EQ- 0, EQ+10, EQ+10, EQ- 0, EQ-10, EQ-30,
    EQ-40, EQ-20, EQ-10, EQ- 0, EQ- 0, EQ-10, EQ-20, EQ-40,
    EQ-61, EQ-40, EQ-30, EQ-20, EQ-20, EQ-30, EQ-40, EQ-61
  },
  {// King
   //A    B    C    D    E    F    G    H
     16,  78, 108, 139, 139, 108,  78,  16,
     78, 139, 170, 200, 200, 170, 139,  78,
    108, 170, 200, 230, 230, 200, 170, 108,
    139, 200, 230, 261, 261, 230, 200, 139,
    139, 200, 230, 261, 261, 230, 200, 139,
    108, 170, 200, 230, 230, 200, 170, 108,
     78, 139, 170, 200, 200, 170, 139,  78,
     16,  78, 108, 139, 139, 108,  78,  16
  }
};


#endif // !defined(PSQTAB_H_INCLUDED)
