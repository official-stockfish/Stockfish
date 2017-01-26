## Features

* [UCI commands](https://github.com/ngocdaothanh/JStockfish/blob/master/engine-interface.txt)
* Additional commands convenient for checking if a move is legal, getting FEN
  and state from a position (check mate, stale mate, draw by 50-move rule or
  3-fold repetition rule)

## Build

You need [CMake](http://www.cmake.org/), JVM, and a C++ compiler to build:

```
./build.sh
```

`build` directory will be created, containing the compiled library.

If you see error like this:

```
Could NOT find JNI
```

Then try exporting `JAVA_HOME` and try building again:

```
export JAVA_HOME=/usr/lib/jvm/java-8-oracle
```

## Try with SBT

For details, please see [Javadoc](http://ngocdaothanh.github.io/JStockfish/).

```
sbt console -Djava.library.path=build
```

### Standard UCI commands

```
import jstockfish.Uci

Uci.uci
Uci.setoption("Threads", Runtime.getRuntime.availableProcessors.toString)
Uci.setoption("UCI_Chess960", "true")

Uci.ucinewgame()
Uci.position("startpos moves d2d4")
Uci.go("infinite")
Uci.stop()
Uci.ponderhit()
```

### Additional commands added by Stockfish

```
Uci.flip
Uci.bench("512 4 16")
Uci.d
Uci.eval
Uci.perft(16)
```

### Additional commands added by JStockfish

```
Uci.islegal("g8f6")
Uci.fen
Uci.state
```

State enum:

```
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
```

The order is similar to that of
[Cuckoo](https://code.google.com/p/cuckoochess/source/browse/trunk/CuckooChessEngine/src/chess/Game.java#134)
chess engine.

### Additional commands added by JStockfish, independent of the current game

Stockfish can't process multiple games at a time. Calls to methods of class
`Uci` should be synchronized. However, methods of class `Position` can be called
any time, no need to synchronize:

```
import jstockfish.Position

val chess960 = false
Position.islegal(chess960, "startpos moves d2d4", "g8f6")
Position.fen(chess960, "startpos moves d2d4")
Position.state(chess960, "startpos moves d2d4")
```

### Get additional info together with bestmove

Instead of sending `Uci.go` to get `bestmove`, then `Position.state` to get
state of the position after the move is made, you can set option to tell `UCI.go`
to also output `info state <state enum>` before outputing `bestmove`:

```
Uci.setoption("Info State", "true")
```

This way, you can get better performance.

Similarly, to get `info fen <FEN>`:

```
Uci.setoption("Info FEN", "true")
```
