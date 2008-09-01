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


#if !defined(THREAD_H_INCLUDED)
#define THREAD_H_INCLUDED


////
//// Includes
////

#include "lock.h"
#include "movepick.h"
#include "position.h"
#include "search.h"


////
//// Constants and variables
////

const int THREAD_MAX = 8;


////
//// Types
////

struct SplitPoint {
  SplitPoint *parent;
  Position pos;
  SearchStack sstack[THREAD_MAX][PLY_MAX];
  SearchStack *parentSstack;
  int ply;
  Depth depth;
  volatile Value alpha, beta, bestValue;
  bool pvNode;
  Bitboard dcCandidates;
  int master, slaves[THREAD_MAX];
  Lock lock;
  MovePicker *mp;
  volatile int moves;
  volatile int cpus;
  bool finished;
};


struct Thread {
  SplitPoint *splitPoint;
  int activeSplitPoints;
  uint64_t nodes;
  bool failHighPly1;
  volatile bool stop;
  volatile bool running;
  volatile bool idle;
  volatile bool workIsWaiting;
  volatile bool printCurrentLine;
  unsigned char pad[64];
};


#endif // !defined(THREAD_H_INCLUDED)
