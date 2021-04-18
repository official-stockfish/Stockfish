## Overview

[![Build Status](https://travis-ci.org/official-stockfish/Stockfish.svg?branch=master)](https://travis-ci.org/official-stockfish/Stockfish)
[![Build Status](https://ci.appveyor.com/api/projects/status/github/official-stockfish/Stockfish?branch=master&svg=true)](https://ci.appveyor.com/project/mcostalba/stockfish/branch/master)

[Stockfish](https://stockfishchess.org) is a free, powerful UCI chess engine
derived from Glaurung 2.1. Stockfish is not a complete chess program and requires a
UCI-compatible graphical user interface (GUI) (e.g. XBoard with PolyGlot, Scid,
Cute Chess, eboard, Arena, Sigma Chess, Shredder, Chess Partner or Fritz) in order
to be used comfortably. Read the documentation for your GUI of choice for information
about how to use Stockfish with it.

The Stockfish engine features two evaluation functions for chess, the classical
evaluation based on handcrafted terms, and the NNUE evaluation based on efficiently
updatable neural networks. The classical evaluation runs efficiently on almost all
CPU architectures, while the NNUE evaluation benefits from the vector
intrinsics available on most CPUs (sse2, avx2, neon, or similar).


## Files

This distribution of Stockfish consists of the following files:

  * Readme.md, the file you are currently reading.

  * Copying.txt, a text file containing the GNU General Public License version 3.
  
  * AUTHORS, a text file with the list of authors for the project

Stockfish NNUE is a port of a shogi neural network named NNUE (efficiently updateable neural network backwards) to Stockfish 11. To learn more about the Stockfish chess engine, look [here](stockfish.md) for an overview and [here](https://github.com/official-stockfish/Stockfish) for the official repository.

=======
## Building

To compile:
```
make -jN ARCH=... build
```

To compile with Profile Guided Optimizations. Requires that the computer that is used for compilation supports the selected `ARCH`.
```
make -jN ARCH=... profile-build
```

`N` is the number of threads to use for compilation.

`ARCH` is one of:
`x86-64-vnni512`, `x86-64-vnni256`, `x86-64-avx512`, `x86-64-bmi2`, `x86-64-avx2`,
`x86-64-sse41-popcnt`, `x86-64-modern`, `x86-64-ssse3`, `x86-64-sse3-popcnt`,
`x86-64`, `x86-32-sse41-popcnt`, `x86-32-sse2`, `x86-32`, `ppc-64`, `ppc-32,
armv7`, `armv7-neon`, `armv8`, `apple-silicon`, `general-64`, `general-32`.

`ARCH` needs to be chosen based based on the instruction set of the CPU that will run stockfish. `x86-64-modern` will produce a binary that works on most common processors, but other options may increase performance for specific hardware.

Additional options:

### Building Instructions for Mac

1. Ensure that you have OpenBlas Installed
```
brew install openblas
```
2. Go to src then build using the makefile
```
cd src
make build ARCH=x86-64 COMP=gcc blas=yes
```
or
```
cd src
make profile-build ARCH=x86-64 COMP=gcc blas=yes
```

## Training Guide

### Generating Training Data

To generate training data from the classic eval, use the gensfen command with the setting "Use NNUE" set to "false". The given example is generation in its simplest form. There are more commands.

```
uci
setoption name PruneAtShallowDepth value false
setoption name Use NNUE value false
setoption name Threads value x
setoption name Hash value y
setoption name SyzygyPath value path
isready
gensfen depth a loop b use_draw_in_training_data_generation 1 eval_limit 32000
```

- `depth` is the searched depth per move, or how far the engine looks forward. This value is an integer.
- `loop` is the amount of positions generated. This value is also an integer.

Specify how many threads and how much memory you would like to use with the `x` and `y` values. The option SyzygyPath is not necessary, but if you would like to use it, you must first have Syzygy endgame tablebases on your computer, which you can find [here](http://oics.olympuschess.com/tracker/index.php). You will need to have a torrent client to download these tablebases, as that is probably the fastest way to obtain them. The `path` is the path to the folder containing those tablebases. It does not have to be surrounded in quotes.

This will create a file named "generated_kifu.binpack" in the same folder as the binary containing the generated training data. Once generation is done, you can rename the file to something like "1billiondepth12.binpack" to remember the depth and quantity of the positions and move it to a folder named "trainingdata" in the same directory as the binaries.

You will also need validation data that is used for loss calculation and accuracy computation. Validation data is generated in the same way as training data, but generally at most 1 million positions should be used as there's no need for more and it would just slow the learning process down. It may also be better to slightly increase the depth for validation data. After generation you can rename the validation data file to "val.binpack" and drop it in a folder named "validationdata" in the same directory to make it easier.

## Training data formats.

- `.bin` - the original training data format. Uses 40 bytes per entry. Is supported directly by the `gensfen` and `learn` commands.
- `.plain` - a human readable training data format. This one is not supported directly by the `gensfen` and `learn` commands. It should not be used for data exchange because it's less compact than other formats. It is mostly useful for inspection of the data.
- `.binpack` - a compact binary training data format that exploits positions chains to further reduce size. It uses on average between 2 to 3 bytes per entry when generating data with `gensfen`. It is supported directly by `gensfen` and `learn` commands. It is currently the default for the `gensfen` command. A more in depth description can be found [here](docs/binpack.md)

### Conversion between formats.

There is a builting converted that support all 3 formats described above. Any of them can be converted to any other. For more information and usage guide see [here](docs/convert.md).

A more updated list can be found in the #sf-nnue-resources channel in the Discord.
