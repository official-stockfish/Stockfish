1. Introduction
---------------

Stockfish is a free UCI chess engine derived from Glaurung 2.1. It is
not a complete chess program, but requires some UCI compatible GUI
(like XBoard with PolyGlot, eboard, Jos�, Arena, Sigma Chess, Shredder,
Chess Partner, or Fritz) in order to be used comfortably.  Read the
documentation for your GUI of choice for information about how to use
Stockfish with your GUI.

This version of Stockfish supports up to 8 CPUs, but has not been
tested thoroughly with more than 2.  The program tries to detect the
number of CPUs on your computer and set the number of search threads
accordingly, but please be aware that the detection is not always
correct.  It is therefore recommended to inspect the value of the
"Threads" UCI parameter, and to make sure it equals the number of CPU
cores on your computer.


2. Files
--------

This distribution of Stockfish consists of the following files:

  * Readme.txt, the file you are currently reading.

  * Copying.txt, a text file containing the GNU General Public
    License.

  * src/, a subdirectory containing the full source code, including a
    Makefile that can be used to compile Stockfish on Unix-like
    systems.  For further information about how to compile Stockfish
    yourself, read section 4 below.

  * polyglot.ini, for using Stockfish with Fabien Letouzey's PolyGlot
    adapter.


3. Opening books
----------------

This version of Stockfish has experimental support for PolyGlot opening
books. For information about how to create such books, consult the
PolyGlot documentation.  The book file can be selected by setting the
UCI parameter "Book File".



4. Compiling it yourself
------------------------

On Unix-like systems, it should usually be possible to compile
Stockfish directly from the source code with the included Makefile.
The exception is computer with big-endian CPUs, like PowerPC
Macintoshes. Some of the bitboard routines in the current version of
Stockfish are endianness-sensitive, and won't work on a big-endian CPU.
Ensuring that the line with #define USE_32BIT_ATTACKS" near the top
of bitboard.h is commented out should solve this problem.
Commenting out the line with "#define USE_32BIT_ATTACKS" near the

There is also a problem with compiling Stockfish on certain 64-bit
systems, regardless of the endianness.  If Stockfish segfaults
immediately after startup, try to comment out the line with
"#define USE_FOLDED_BITSCAN" near the beginning of bitboard.h and
recompile.

Finally, even if Stockfish does work without any changes on your
computer, it might be possible to improve the performance by changing
some of the #define directives in bitboard.h.  The default settings
are optimized for 64-bit CPUs.  On 32-bit CPUs, it is probably better
to switch on USE_32BIT_ATTACKS, and to use BITCOUNT_SWAR_32 instead of
BITCOUNT_SWAR_64.  For computers with very little memory (like
handheld devices), it is possible to conserve memory by defining
USE_COMPACT_ROOK_ATTACKS.


6. Terms of use
---------------

Stockfish is free, and distributed under the GNU General Public License
(GPL).  Essentially, this means that you are free to do almost exactly
what you want with the program, including distributing it among your
friends, making it available for download from your web site, selling
it (either by itself or as part of some bigger software package), or
using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute Stockfish in
some way, you must always include the full source code, or a pointer
to where the source code can be found.  If you make any changes to the
source code, these changes must also be made available under the GPL.

For full details, read the copy of the GPL found in the file named
Copying.txt.


7. Feedback
-----------

The author's e-mail address is mcostalba@gmail.com
