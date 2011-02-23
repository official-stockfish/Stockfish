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

#if !defined(PSQTAB_H_INCLUDED)
#define PSQTAB_H_INCLUDED

#include "types.h"

namespace {

////
//// Constants modified by Joona Kiiski
////

const Value MP = PawnValueMidgame;
const Value MK = KnightValueMidgame;
const Value MB = BishopValueMidgame;
const Value MR = RookValueMidgame;
const Value MQ = QueenValueMidgame;

const Value EP = PawnValueEndgame;
const Value EK = KnightValueEndgame;
const Value EB = BishopValueEndgame;
const Value ER = RookValueEndgame;
const Value EQ = QueenValueEndgame;

const int MgPST[][64] = {
  { },
  {// Pawn
   // A      B      C     D      E      F      G     H
        0,    0,     0,     0,     0,     0,    0,     0,
    MP-28, MP-6, MP+ 4, MP+14, MP+14, MP+ 4, MP-6, MP-28,
    MP-28, MP-6, MP+ 9, MP+36, MP+36, MP+ 9, MP-6, MP-28,
    MP-28, MP-6, MP+17, MP+58, MP+58, MP+17, MP-6, MP-28,
    MP-28, MP-6, MP+17, MP+36, MP+36, MP+17, MP-6, MP-28,
    MP-28, MP-6, MP+ 9, MP+14, MP+14, MP+ 9, MP-6, MP-28,
    MP-28, MP-6, MP+ 4, MP+14, MP+14, MP+ 4, MP-6, MP-28,
        0,    0,     0,     0,     0,     0,    0,     0
  },
  {// Knight
   //  A      B       C      D      E      F      G       H
    MK-135, MK-107, MK-80, MK-67, MK-67, MK-80, MK-107, MK-135,
    MK- 93, MK- 67, MK-39, MK-25, MK-25, MK-39, MK- 67, MK- 93,
    MK- 53, MK- 25, MK+ 1, MK+13, MK+13, MK+ 1, MK- 25, MK- 53,
    MK- 25, MK+  1, MK+27, MK+41, MK+41, MK+27, MK+  1, MK- 25,
    MK- 11, MK+ 13, MK+41, MK+55, MK+55, MK+41, MK+ 13, MK- 11,
    MK- 11, MK+ 13, MK+41, MK+55, MK+55, MK+41, MK+ 13, MK- 11,
    MK- 53, MK- 25, MK+ 1, MK+13, MK+13, MK+ 1, MK- 25, MK- 53,
    MK-193, MK- 67, MK-39, MK-25, MK-25, MK-39, MK- 67, MK-193
  },
  {// Bishop
   // A      B      C      D      E      F      G      H
    MB-40, MB-40, MB-35, MB-30, MB-30, MB-35, MB-40, MB-40,
    MB-17, MB+ 0, MB- 4, MB+ 0, MB+ 0, MB- 4, MB+ 0, MB-17,
    MB-13, MB- 4, MB+ 8, MB+ 4, MB+ 4, MB+ 8, MB- 4, MB-13,
    MB- 8, MB+ 0, MB+ 4, MB+17, MB+17, MB+ 4, MB+ 0, MB- 8,
    MB- 8, MB+ 0, MB+ 4, MB+17, MB+17, MB+ 4, MB+ 0, MB- 8,
    MB-13, MB- 4, MB+ 8, MB+ 4, MB+ 4, MB+ 8, MB- 4, MB-13,
    MB-17, MB+ 0, MB- 4, MB+ 0, MB+ 0, MB- 4, MB+ 0, MB-17,
    MB-17, MB-17, MB-13, MB- 8, MB- 8, MB-13, MB-17, MB-17
  },
  {// Rook
   // A      B     C     D     E     F     G     H
    MR-12, MR-7, MR-2, MR+2, MR+2, MR-2, MR-7, MR-12,
    MR-12, MR-7, MR-2, MR+2, MR+2, MR-2, MR-7, MR-12,
    MR-12, MR-7, MR-2, MR+2, MR+2, MR-2, MR-7, MR-12,
    MR-12, MR-7, MR-2, MR+2, MR+2, MR-2, MR-7, MR-12,
    MR-12, MR-7, MR-2, MR+2, MR+2, MR-2, MR-7, MR-12,
    MR-12, MR-7, MR-2, MR+2, MR+2, MR-2, MR-7, MR-12,
    MR-12, MR-7, MR-2, MR+2, MR+2, MR-2, MR-7, MR-12,
    MR-12, MR-7, MR-2, MR+2, MR+2, MR-2, MR-7, MR-12
  },
  {// Queen
   // A     B     C     D     E     F     G     H
    MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8,
    MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8,
    MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8,
    MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8,
    MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8,
    MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8,
    MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8,
    MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8, MQ+8
  },
  {// King
   //A    B    C    D    E    F    G    H
    287, 311, 262, 214, 214, 262, 311, 287,
    262, 287, 238, 190, 190, 238, 287, 262,
    214, 238, 190, 142, 142, 190, 238, 214,
    190, 214, 167, 119, 119, 167, 214, 190,
    167, 190, 142,  94,  94, 142, 190, 167,
    142, 167, 119,  69,  69, 119, 167, 142,
    119, 142,  94,  46,  46,  94, 142, 119,
     94, 119,  69,  21,  21,  69, 119,  94
  }
};

const int EgPST[][64] = {
  { },
  {// Pawn
   // A     B     C     D     E     F     G     H
       0,    0,    0,    0,    0,    0,    0,    0,
    EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8,
    EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8,
    EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8,
    EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8,
    EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8,
    EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8, EP-8,
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
    EB-59, EB-42, EB-35, EB-26, EB-26, EB-35, EB-42, EB-59,
    EB-42, EB-26, EB-18, EB-11, EB-11, EB-18, EB-26, EB-42,
    EB-35, EB-18, EB-11, EB- 4, EB- 4, EB-11, EB-18, EB-35,
    EB-26, EB-11, EB- 4, EB+ 4, EB+ 4, EB- 4, EB-11, EB-26,
    EB-26, EB-11, EB- 4, EB+ 4, EB+ 4, EB- 4, EB-11, EB-26,
    EB-35, EB-18, EB-11, EB- 4, EB- 4, EB-11, EB-18, EB-35,
    EB-42, EB-26, EB-18, EB-11, EB-11, EB-18, EB-26, EB-42,
    EB-59, EB-42, EB-35, EB-26, EB-26, EB-35, EB-42, EB-59
  },
  {// Rook
   // A     B     C     D     E     F     G     H
    ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3,
    ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3,
    ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3,
    ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3,
    ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3,
    ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3,
    ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3,
    ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3, ER+3
  },
  {// Queen
   // A      B      C      D      E      F      G      H
    EQ-80, EQ-54, EQ-42, EQ-30, EQ-30, EQ-42, EQ-54, EQ-80,
    EQ-54, EQ-30, EQ-18, EQ- 6, EQ- 6, EQ-18, EQ-30, EQ-54,
    EQ-42, EQ-18, EQ- 6, EQ+ 6, EQ+ 6, EQ- 6, EQ-18, EQ-42,
    EQ-30, EQ- 6, EQ+ 6, EQ+18, EQ+18, EQ+ 6, EQ- 6, EQ-30,
    EQ-30, EQ- 6, EQ+ 6, EQ+18, EQ+18, EQ+ 6, EQ- 6, EQ-30,
    EQ-42, EQ-18, EQ- 6, EQ+ 6, EQ+ 6, EQ- 6, EQ-18, EQ-42,
    EQ-54, EQ-30, EQ-18, EQ- 6, EQ- 6, EQ-18, EQ-30, EQ-54,
    EQ-80, EQ-54, EQ-42, EQ-30, EQ-30, EQ-42, EQ-54, EQ-80
  },
  {// King
   //A    B    C    D    E    F    G    H
     18,  77, 105, 135, 135, 105,  77,  18,
     77, 135, 165, 193, 193, 165, 135,  77,
    105, 165, 193, 222, 222, 193, 165, 105,
    135, 193, 222, 251, 251, 222, 193, 135,
    135, 193, 222, 251, 251, 222, 193, 135,
    105, 165, 193, 222, 222, 193, 165, 105,
     77, 135, 165, 193, 193, 165, 135,  77,
     18,  77, 105, 135, 135, 105,  77,  18
  }
};

} // namespace

#endif // !defined(PSQTAB_H_INCLUDED)
