package jstockfish;

/**
 * States of a chess board position.
 */
public enum State {
    // The order is NOT similar to that of Cuckoo chess engine:
    // https://code.google.com/p/cuckoochess/source/browse/trunk/CuckooChessEngine/src/chess/Game.java#134

    ALIVE,

    WHITE_MATE,       // White mates (white wins)
    BLACK_MATE,       // Black mates (black wins)

    // Automatic draw
    WHITE_STALEMATE,  // White is stalemated (white can't move)
    BLACK_STALEMATE,  // Black is stalemated (black can't move)
    DRAW_NO_MATE,     // Draw by insufficient material

    // Can draw, but players must claim
    CAN_DRAW_50,      // Can draw by 50-move rule
    CAN_DRAW_REP;     // Can draw by 3-fold repetition rule

    public boolean isAutomaticOver() {
        return
            this == WHITE_MATE || this == BLACK_MATE ||
            this == WHITE_STALEMATE || this == BLACK_STALEMATE ||
            this == DRAW_NO_MATE;
    }
}
