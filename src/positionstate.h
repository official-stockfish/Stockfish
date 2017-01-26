#ifndef POSITION_STATE_H_INCLUDED
#define POSITION_STATE_H_INCLUDED

#include "position.h"

// Added by JStockfish, see its Java counterpart
enum State {
  ALIVE,
  WHITE_MATE,       // White mates (white wins)
  BLACK_MATE,       // Black mates (black wins)
  WHITE_STALEMATE,  // White is stalemated (white can't move)
  BLACK_STALEMATE,  // Black is stalemated (black can't move)
  DRAW_NO_MATE,     // Draw by insufficient material
  CAN_DRAW_50,      // Can draw by 50-move rule
  CAN_DRAW_REP      // Can draw by 3-fold repetition rule
};

int positionstate(Position& pos);

#endif
