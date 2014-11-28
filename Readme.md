### Overview

Stockfish is a free UCI chess engine derived from Glaurung 2.1. It is
not a complete chess program and requires some UCI-compatible GUI
(e.g. XBoard with PolyGlot, eboard, Arena, Sigma Chess, Shredder, Chess
Partner or Fritz) in order to be used comfortably. Read the
documentation for your GUI of choice for information about how to use
Stockfish with it.

This version of Stockfish supports up to 128 cores. The engine defaults
to one search thread, so it is therefore recommended to inspect the value of
the *Threads* UCI parameter, and to make sure it equals the number of CPU
cores on your computer.

This version of Stockfish has support for Syzygybases.


### Files

This distribution of Stockfish consists of the following files:

  * Readme.md, the file you are currently reading.

  * Copying.txt, a text file containing the GNU General Public License.

  * src, a subdirectory containing the full source code, including a Makefile
    that can be used to compile Stockfish on Unix-like systems.

### Lomonosov Tablebases

Lomonosov TB can be enabled by compilation with LOMONOSOV=yes.
Stockfish will use API from lomonosov_tb.dll.

You can download LomonosovTB API:
https://github.com/vpike/LomonosovTB

**Configuration**

Lomonosov tables are configured using the UCI options "Lomonosov Using",
"Lomonosov Path", "Lomonosov Cache" and "Lomonosov Order".

The option "Lomonosov Using" is the checkbox of the use of tables. It's true by default.

The option "Lomonosov Path" is the directories with tables. Multiple directories should be
separated by ";". For example, "C:\wl345;C:\pl345".

The option "Lomonosov Cache" is the maximal size of cache (in MB) used by lomonosov_tb.dll.
The default value is 2048 MB.

The option "Lomonosov Order" sets the priority of probing different types of tables. There are 4 types: "WL" - ternary tables (*.wlw and *.wlb),
"PL" - tablebases in Distance-To-Mate metric (*.plw and *.plb), "WL50" - ternary tables considering the 50-move rule (*.zwlw and *.zwlb),
"PL50" - tablebases in Distance-To-Zero metric considering the 50-moves rule (*.zplw and *.zplb).

There are two additional options related to Lomonosov TB. The checkbox "Lomonosov Stat" specifies the printing of statistics
in the end of engine working (to file "full_tb_statistics.txt"), it's true by default. The checkbox "Lomonosov Logging" specifies
the printing of statistics of the use of tables while engine is working, it's false by default.

**Tables**

You can download some tables:
https://github.com/vpike/WDL50
https://github.com/vpike/DTZ50

You can download 

### Syzygybases

Syzygy TB can be enabled by compilation with SYZYGY=yes.

**Configuration**

Syzygybases are configured using the UCI options "SyzygyPath",
"SyzygyProbeDepth", "Syzygy50MoveRule" and "SyzygyProbeLimit".

The option "SyzygyPath" should be set to the directory or directories that
contain the .rtbw and .rtbz files. Multiple directories should be
separated by ";" on Windows and by ":" on Unix-based operating systems.
**Do not use spaces around the ";" or ":".**

Example: `C:\tablebases\wdl345;C:\tablebases\wdl6;D:\tablebases\dtz345;D:\tablebases\dtz6`

It is recommended to store .rtbw files on an SSD. There is no loss in
storing the .rtbz files on a regular HD.

Increasing the "SyzygyProbeDepth" option lets the engine probe less
aggressively. Set this option to a higher value if you experience too much
slowdown (in terms of nps) due to TB probing.

Set the "Syzygy50MoveRule" option to false if you want tablebase positions
that are drawn by the 50-move rule to count as win or loss. This may be useful
for correspondence games (because of tablebase adjudication).

The "SyzygyProbeLimit" option should normally be left at its default value.

**What to expect**  
If the engine is searching a position that is not in the tablebases (e.g.
a position with 7 pieces), it will access the tablebases during the search.
If the engine reports a very large score (typically 123.xx), this means
that it has found a winning line into a tablebase position.

If the engine is given a position to search that is in the tablebases, it
will use the tablebases at the beginning of the search to preselect all
good moves, i.e. all moves that preserve the win or preserve the draw while
taking into account the 50-move rule.
It will then perform a search only on those moves. **The engine will not move
immediately**, unless there is only a single good move. **The engine likely
will not report a mate score even if the position is known to be won.**

It is therefore clear that behaviour is not identical to what one might
be used to with Nalimov tablebases. There are technical reasons for this
difference, the main technical reason being that Nalimov tablebases use the
DTM metric (distance-to-mate), while Syzygybases use a variation of the
DTZ metric (distance-to-zero, zero meaning any move that resets the 50-move
counter). This special metric is one of the reasons that Syzygybases are
more compact than Nalimov tablebases, while still storing all information
needed for optimal play and in addition being able to take into account
the 50-move rule.


### Compiling it yourself

On Unix-like systems, it should be possible to compile Stockfish
directly from the source code with the included Makefile.

Stockfish has support for 32 or 64-bit CPUs, the hardware POPCNT
instruction, big-endian machines such as Power PC, and other platforms.

In general it is recommended to run `make help` to see a list of make
targets with corresponding descriptions. When not using the Makefile to
compile (for instance with Microsoft MSVC) you need to manually
set/unset some switches in the compiler command line; see file *types.h*
for a quick reference.


### Terms of use

Stockfish is free, and distributed under the **GNU General Public License**
(GPL). Essentially, this means that you are free to do almost exactly
what you want with the program, including distributing it among your
friends, making it available for download from your web site, selling
it (either by itself or as part of some bigger software package), or
using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute Stockfish in
some way, you must always include the full source code, or a pointer
to where the source code can be found. If you make any changes to the
source code, these changes must also be made available under the GPL.

For full details, read the copy of the GPL found in the file named
*Copying.txt*
