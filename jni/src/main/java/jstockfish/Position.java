package jstockfish;

/**
 * Additional commands added by JStockfish, independent of the current game,
 * can be called any time.
 */
public class Position {
    static {
        System.loadLibrary("jstockfish");
    }

    public static native boolean islegal(boolean chess960, String position, String move);

    public static native String fen(boolean chess960, String position);

    private static native int positionstate(boolean chess960, String position);

    public static State state(boolean chess960, String position) {
        int ordinal = positionstate(chess960, position);
        return State.values()[ordinal];
    }
}
