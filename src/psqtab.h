/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2009 Marco Costalba

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

#include "value.h"


////
//// Constants modified by Joona Kiiski
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
    MP-34, MP-12, MP- 2, MP+ 8, MP+ 8, MP- 2, MP-12, MP-34,
    MP-34, MP-12, MP+ 3, MP+30, MP+30, MP+ 3, MP-12, MP-34,
    MP-34, MP-12, MP+11, MP+52, MP+52, MP+11, MP-12, MP-34,
    MP-34, MP-12, MP+11, MP+30, MP+30, MP+11, MP-12, MP-34,
    MP-34, MP-12, MP+ 3, MP+ 8, MP+ 8, MP+ 3, MP-12, MP-34,
    MP-34, MP-12, MP- 2, MP+ 8, MP+ 8, MP- 2, MP-12, MP-34,
        0,     0,     0,     0,     0,     0,     0,     0
  },
  {// Knight
   //  A      B       C      D      E      F      G       H
    MK-136, MK-108, MK-81, MK-68, MK-68, MK-81, MK-108, MK-136,
    MK- 94, MK- 68, MK-40, MK-26, MK-26, MK-40, MK- 68, MK- 94,
    MK- 54, MK- 26, MK+ 0, MK+12, MK+12, MK+ 0, MK- 26, MK- 54,
    MK- 26, MK+  0, MK+26, MK+40, MK+40, MK+26, MK+  0, MK- 26,
    MK- 12, MK+ 12, MK+40, MK+54, MK+54, MK+40, MK+ 12, MK- 12,
    MK- 12, MK+ 12, MK+40, MK+54, MK+54, MK+40, MK+ 12, MK- 12,
    MK- 54, MK- 26, MK+ 0, MK+12, MK+12, MK+ 0, MK- 26, MK- 54,
    MK-194, MK- 68, MK-40, MK-26, MK-26, MK-40, MK- 68, MK-194
  },
  {// Bishop
   // A      B      C      D      E      F      G      H
    MB-41, MB-41, MB-36, MB-31, MB-31, MB-36, MB-41, MB-41,
    MB-18, MB- 1, MB- 5, MB- 1, MB- 1, MB- 5, MB- 1, MB-18,
    MB-14, MB- 5, MB+ 7, MB+ 3, MB+ 3, MB+ 7, MB- 5, MB-14,
    MB- 9, MB- 1, MB+ 3, MB+16, MB+16, MB+ 3, MB- 1, MB- 9,
    MB- 9, MB- 1, MB+ 3, MB+16, MB+16, MB+ 3, MB- 1, MB- 9,
    MB-14, MB- 5, MB+ 7, MB+ 3, MB+ 3, MB+ 7, MB- 5, MB-14,
    MB-18, MB- 1, MB- 5, MB- 1, MB- 1, MB- 5, MB- 1, MB-18,
    MB-18, MB-18, MB-14, MB- 9, MB- 9, MB-14, MB-18, MB-18
  },
  {// Rook
   // A      B     C     D     E     F     G      H
    MR-14, MR-9, MR-4, MR-0, MR-0, MR-4, MR-9, MR-14,
    MR-14, MR-9, MR-4, MR-0, MR-0, MR-4, MR-9, MR-14,
    MR-14, MR-9, MR-4, MR-0, MR-0, MR-4, MR-9, MR-14,
    MR-14, MR-9, MR-4, MR-0, MR-0, MR-4, MR-9, MR-14,
    MR-14, MR-9, MR-4, MR-0, MR-0, MR-4, MR-9, MR-14,
    MR-14, MR-9, MR-4, MR-0, MR-0, MR-4, MR-9, MR-14,
    MR-14, MR-9, MR-4, MR-0, MR-0, MR-4, MR-9, MR-14,
    MR-14, MR-9, MR-4, MR-0, MR-0, MR-4, MR-9, MR-14
  },
  {// Queen
   // A      B      C      D      E      F      G      H
    MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12,
    MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12,
    MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12,
    MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12,
    MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12,
    MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12,
    MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12,
    MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12, MQ+12
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
   // A     B     C     D     E     F     G     H
       0,    0,    0,    0,    0,    0,    0,    0,
    EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7,
    EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7,
    EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7,
    EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7,
    EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7,
    EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7, EP-7,
       0,    0,    0,    0,    0,    0,    0,    0
  },
  {// Knight
   // A       B      C      D      E      F      G      H
    EK-104, EK-79, EK-55, EK-42, EK-42, EK-55, EK-79, EK-104,
    EK- 79, EK-55, EK-30, EK-17, EK-17, EK-30, EK-55, EK- 79,
    EK- 55, EK-30, EK- 6, EK+ 5, EK+ 5, EK- 6, EK-30, EK- 55,
    EK- 42, EK-17, EK+ 5, EK+18, EK+18, EK+ 5, EK-17, EK- 42,
    EK- 42, EK-17, EK+ 5, EK+18, EK+18, EK+ 5, EK-17, EK- 42,
    EK- 55, EK-30, EK- 6, EK+ 5, EK+ 5, EK- 6, EK-30, EK- 55,
    EK- 79, EK-55, EK-30, EK-17, EK-17, EK-30, EK-55, EK- 79,
    EK-104, EK-79, EK-55, EK-42, EK-42, EK-55, EK-79, EK-104
  },
  {// Bishop
   // A      B      C      D      E      F      G      H
    EB-56, EB-39, EB-32, EB-23, EB-23, EB-32, EB-39, EB-56,
    EB-39, EB-23, EB-15, EB- 8, EB- 8, EB-15, EB-23, EB-39,
    EB-32, EB-15, EB- 8, EB- 1, EB- 1, EB- 8, EB-15, EB-32,
    EB-23, EB- 8, EB- 1, EB+ 7, EB+ 7, EB- 1, EB- 8, EB-23,
    EB-23, EB- 8, EB- 1, EB+ 7, EB+ 7, EB- 1, EB- 8, EB-23,
    EB-32, EB-15, EB- 8, EB- 1, EB- 1, EB- 8, EB-15, EB-32,
    EB-39, EB-23, EB-15, EB- 8, EB- 8, EB-15, EB-23, EB-39,
    EB-56, EB-39, EB-32, EB-23, EB-23, EB-32, EB-39, EB-56
  },
  {// Rook
   // A     B     C     D     E     F     G     H
    ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1,
    ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1,
    ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1,
    ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1,
    ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1,
    ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1,
    ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1,
    ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1, ER+1
  },
  {// Queen
   // A      B      C      D      E      F      G      H
    EQ-77, EQ-51, EQ-39, EQ-27, EQ-27, EQ-39, EQ-51, EQ-77,
    EQ-51, EQ-27, EQ-15, EQ- 3, EQ- 3, EQ-15, EQ-27, EQ-51,
    EQ-39, EQ-15, EQ- 3, EQ+ 9, EQ+ 9, EQ- 3, EQ-15, EQ-39,
    EQ-27, EQ- 3, EQ+ 9, EQ+21, EQ+21, EQ+ 9, EQ- 3, EQ-27,
    EQ-27, EQ- 3, EQ+ 9, EQ+21, EQ+21, EQ+ 9, EQ- 3, EQ-27,
    EQ-39, EQ-15, EQ- 3, EQ+ 9, EQ+ 9, EQ- 3, EQ-15, EQ-39,
    EQ-51, EQ-27, EQ-15, EQ- 3, EQ- 3, EQ-15, EQ-27, EQ-51,
    EQ-77, EQ-51, EQ-39, EQ-27, EQ-27, EQ-39, EQ-51, EQ-77
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
