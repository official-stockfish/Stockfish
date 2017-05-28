#include "positionstate.h"
#include "bitboard.h"
#include "movegen.h"

// https://code.google.com/p/cuckoochess/source/browse/trunk/CuckooChessEngine/src/chess/Game.java#527
bool is_draw_insufficient_material(Position& pos) {
  if (pos.pieces(QUEEN) != 0) return false;
  if (pos.pieces(ROOK)  != 0) return false;
  if (pos.pieces(PAWN)  != 0) return false;

  Bitboard wb = pos.pieces(WHITE, BISHOP);
  Bitboard bb = pos.pieces(BLACK, BISHOP);

  int nwb = popcount(wb);
  int nbb = popcount(bb);
  int nwn = popcount(pos.pieces(WHITE, KNIGHT));
  int nbn = popcount(pos.pieces(BLACK, KNIGHT));

  // King + bishop/knight vs king is draw
  if (nwb + nwn + nbb + nbn <= 1) return true;

  if (nwn + nbn == 0) {
    // Only bishops. If they are all on the same color, the position is a draw.
    Bitboard bMask = wb | bb;
    if (((bMask & DarkSquares) == 0) || ((bMask & LightSquares) == 0)) return true;
  }

  return false;
}

int positionstate(Position& pos) {
  Bitboard checkers = pos.checkers();
  int noLegalMove = MoveList<LEGAL>(pos).size() == 0;

  if (noLegalMove) {
    if (checkers) {
      return (pos.side_to_move() == BLACK)? WHITE_MATE : BLACK_MATE;
    } else {
      return (pos.side_to_move() == BLACK)? BLACK_STALEMATE : WHITE_STALEMATE;
    }
  }

  if (is_draw_insufficient_material(pos)) return DRAW_NO_MATE;

  // Don't use Posistion::is_draw_rule50 to avoid checking noLegalMove again,
  // see the implementation of is_draw_rule50
  if (pos.rule50_count() > 99 && !checkers) return CAN_DRAW_50;

  if (pos.is_draw_repetition(3)) return CAN_DRAW_REP;

  return ALIVE;
}
