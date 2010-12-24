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


#if !defined(THREAD_H_INCLUDED)
#define THREAD_H_INCLUDED


////
//// Includes
////

#include <cstring>

#include "lock.h"
#include "movepick.h"
#include "position.h"
#include "search.h"


////
//// Constants and variables
////

const int MAX_THREADS = 16;
const int MAX_ACTIVE_SPLIT_POINTS = 8;


////
//// Types
////

struct SplitPoint {

  // Const data after splitPoint has been setup
  SplitPoint* parent;
  const Position* pos;
  Depth depth;
  bool pvNode, mateThreat;
  Value beta;
  int ply;
  int master;
  Move threatMove;
  SearchStack sstack[MAX_THREADS][PLY_MAX_PLUS_2];

  // Const pointers to shared data
  MovePicker* mp;
  SearchStack* parentSstack;

  // Shared data
  Lock lock;
  volatile int64_t nodes;
  volatile Value alpha;
  volatile Value bestValue;
  volatile int moveCount;
  volatile bool betaCutoff;
  volatile int slaves[MAX_THREADS];
};

// ThreadState type is used to represent thread's current state

enum ThreadState
{
  THREAD_INITIALIZING,  // thread is initializing itself
  THREAD_SEARCHING,     // thread is performing work
  THREAD_AVAILABLE,     // thread is waiting for work
  THREAD_BOOKED,        // other thread (master) has booked us as a slave
  THREAD_WORKISWAITING, // master has ordered us to start
  THREAD_TERMINATED     // we are quitting and thread is terminated
};

struct Thread {
  volatile ThreadState state;
  SplitPoint* volatile splitPoint;
  volatile int activeSplitPoints;
  SplitPoint splitPoints[MAX_ACTIVE_SPLIT_POINTS];
};


#endif // !defined(THREAD_H_INCLUDED)
