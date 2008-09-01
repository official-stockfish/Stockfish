1. Introduction
---------------

Glaurung is a free UCI chess engine.  It is not a complete chess
program, but requires some UCI compatible GUI (like XBoard with
PolyGlot, eboard, José, Arena, Sigma Chess, Shredder, Chess Partner,
or Fritz) in order to be used comfortably.  Read the documentation for
your GUI of choice for information about how to use Glaurung with your
GUI.

Glaurung 2 is a completely rewritten version of Glaurung.  Apart from
the parallel search code, almost no code is shared with Glaurung
1.2.1, the previous stable version.  The new program is clearly
stronger than the old, but has a less attractive style of play,
because there are still a few major holes in its evaluation function
(most notably space and development).

This version of Glaurung supports up to 8 CPUs, but has not been
tested thoroughly with more than 2.  The program tries to detect the
number of CPUs on your computer and set the number of search threads
accordingly, but please be aware that the detection is not always
correct.  It is therefore recommended to inspect the value of the
"Threads" UCI parameter, and to make sure it equals the number of CPU
cores on your computer.


2. Files
--------

This distribution of Glaurung consists of the following files:

  * Readme.txt, the file you are currently reading.

  * Copying.txt, a text file containing the GNU General Public
    License.

  * src/, a subdirectory containing the full source code, including a
    Makefile that can be used to compile Glaurung on Unix-like
    systems.  For further information about how to compile Glaurung
    yourself, read section 4 below.

  * MacOSX/, a subdirectory containing excutables for Apple Macintosh
    computers running Mac OS X 10.4 (Tiger) and newer.  There are two
    executables, one for OS X 10.4, and one for OS X 10.5.  The
    executable for OS X 10.4 will work in 10.5 as well, but the one
    for 10.5 is faster.

  * LinuxX86/, a subdirectory containing 32-bit and 64-bit x86 GNU/Linux
    executables.

  * Windows/, a subdirectory containing 32-bit and 64-bit Windows
    executables.

  * polyglot.ini, for using Glaurung with Fabien Letouzey's PolyGlot
    adapter.


3. Opening books
----------------

This version of Glaurung has experimental support for PolyGlot opening
books.  For information about how to create such books, consult the
PolyGlot documentation.  The book file can be selected by setting the
UCI parameter "Book File".

A book file contributed by Salvo Spitaleri can be found on the
Glaurung web page.


4. Compiling it yourself
------------------------

On Unix-like systems, it should usually be possible to compile
Glaurung directly from the source code with the included Makefile.
The exception is computer with big-endian CPUs, like PowerPC
Macintoshes.  Some of the bitboard routines in the current version of
Glaurung are endianness-sensitive, and won't work on a big-endian CPU.
Ensuring that the line with #define USE_32BIT_ATTACKS" near the top
of bitboard.h is commented out should solve this problem.
Commenting out the line with "#define USE_32BIT_ATTACKS" near the

There is also a problem with compiling Glaurung on certain 64-bit 
systems, regardless of the endianness.  If Glaurung segfaults 
immediately after startup, try to comment out the line with 
"#define USE_FOLDED_BITSCAN" near the beginning of bitboard.h and
recompile.

Finally, even if Glaurung does work without any changes on your
computer, it might be possible to improve the performance by changing
some of the #define directives in bitboard.h.  The default settings
are optimized for 64-bit CPUs.  On 32-bit CPUs, it is probably better
to switch on USE_32BIT_ATTACKS, and to use BITCOUNT_SWAR_32 instead of
BITCOUNT_SWAR_64.  For computers with very little memory (like
handheld devices), it is possible to conserve memory by defining
USE_COMPACT_ROOK_ATTACKS. 



5. History
----------

2007-05-06: Glaurung 2 - epsilon
--------------------------------

The first public release, and the first version of my new program
which is able to match the old Glaurung 1.2.1 on a single CPU.  Lots
of features and chess knowledge is still missing.

2007-05-10: Glaurung 2 - epsilon/2
----------------------------------

This version is very close to 2 - epsilon.  The major changes are:

  * A number of compatibility problems which appeared when trying to
    compile Glaurung 2 - epsilon on various operating systems and CPUs
    have been solved.

  * Fixed a major bug in the detection of rooks trapped inside a
    friendly king.

  * Added knowledge about several types of drawn endgames.

  * Fixed a few FRC related bugs.  FRC now works, but because of
    serious holes in the evaluation function the program plays very
    badly.

  * A slightly more sophisticated king safety evaluation.

2007-06-07: Glaurung 2 - epsilon/3
----------------------------------

The first public version with support for multiple CPUs.  Unless you
have a dual-core (or better) computer, use Glaurung with a PolyGlot
book, or runs games with ponder on, you may want to skip this version,
which is almost certainly no stronger than 2 - epsilon/2 when running
on a single CPU.  The main changes compared to the previous version
are:

  * Parallel search, with support for 1-4 CPUs.  The program currently
    always allocates a separate pawn hash table and material hash
    table for four threads, which is a pure waste of RAM if your
    computer has just a single CPU.  This will be fixed in a future
    version.

  * Fixed a bug in book randomization.  When using Polyglot books, the
    previous version would always select exactly the same move in the
    same position after a restart of the program.  Thanks to Pavel
    Háse for pointing this out.

  * Fixed a UCI pondering bug: Glaurung no longer instantly prints its
    best move when the maximum depth is reached during a ponder
    search, as the previous version did.  According to the UCI
    protocol, it is not allowed to print the best move before the
    engine has received the "stop" or "quit" command.

  * Additional search information:  The new version displays hash
    saturation and the current line(s) of search.

  * Several minor bug fixes and optimizations in the search and
    evaluation. 

2007-06-08: Glaurung 2 - epsilon/4
----------------------------------

A bugfix release, with only a single important change:

   * Fixed a very serious pondering bug.  As pointed out by Marc
     Lacrosse, the previous version would lose on time in almost every
     single game with pondering enabled.  The new version handles
     pondering correctly (or so I hope).  When playing with ponder
     off, the new version is identical to version 2 - epsilon/3.

2007-06-25: Glaurung 2 - epsilon/5
----------------------------------

Another minor update, including the following improvements and bug
fixes:

   * As Werner Schüle discovered, the previous version would sometimes
     stop thinking and lose on time right before delivering checkmate
     (which is of course a very unfortunate moment to lose on time).
     I haven't been able to reproduce Werner's problem on my computer
     (probably because I run a different OS), but I have fixed the bug
     which I suspect caused the time losses.  I hope the time losses
     will no longer occur with 2 - epsilon/5.

   * The program is now slightly less resource-hungry on computers
     with less than 4 CPU cores: The previous version would always
     allocated separate pawn and material hash tables for four
     threads, even when running on a single-core CPU.  The new version
     only allocates pawn and material hash tables for the threads
     which are actually used.

   * A minor reorganization of the memory layout has made the parallel
     search about 10% more efficient (at least on my computer, but the
     results are likely to vary considerably on different systems).

   * The Intel Mac OS X binary is much faster than before, thanks to
     the Intel C++ compiler (previous versions were compiled with
     GCC).

   * A few other very minor bug fixes and enhancements.

2007-11-21: Glaurung 2.0
------------------------

The first stable (or so I hope) and feature-complete version of
Glaurung 2.  The following are the main changes compared to the
previous version:

   * The license has been changed from GPL version 2 to GPL version 3.

   * MultiPV mode.

   * Support for the "searchmoves" option in the UCI "go" command.
     This means that it is possible to ask Glaurung to exclude some
     moves from its analysis, or to restrict its analysis to just a
     handful of moves selected by the user.  This feature must also be
     supported by the GUI under which Glaurung is run.  Glaurung's own
     GUI does currently not support this feature.

   * Chess960 support now works.  The program still plays this game 
     very badly, because of lack of opening knowledge.

   * Much more aggressive pruning in the last few plies of the main
     search.

   * Somewhat better scaling on multi-CPU systems, and support for up
     to 8 CPUs.

   * Lots of new UCI parameters.

   * Improved time managment, especially in games with pondering on 
     (i.e. when the engine is allowed to think when it's the
     opponent's turn to move).

   * Some evaluation improvements, and some new basic endgame
     patterns.

   * The program should no longer crash if the game lasts longer than
     1000 plies.
 
   * Many minor bug fixes and other tiny improvements throughout the
     code.  
 
   * More generously commented code, and numerous cosmetic changes in
     coding style.

2007-11-22: Glaurung 2.0.1
--------------------------

   * Fixed (or so I hope) a bug which would occasionally cause one of
     the search threads to get stuck forever in its idle loop.

2008-05-14: Glaurung 2.1
------------------------

This version contains far too many changes to list them all, but most
of them are minor and cosmetic.  The most important and noticable
changes are a lot of new UCI parameters, and many improvements in the
evaluation function.  The highlights are:

   * Extensive changes in the evaluation function.  The addition of
     king safety is the most important improvement, but there are also
     numerous little improvements elsewhere in the evaluation.  There
     is still much work left to do in the evaluation function, though.
     Space and development are still missing, and the tuning is likely
     to be very poor.  Currently, the program is optimized for an
     entertaining style rather than maximum strength.

   * More accurate forward pruning.  The previous version used the
     null move refutation move to improve the pruning accuracy by
     means of a very simple trick: It did not allow pruning of any
     moves with the piece captured by the null move refutation move.
     In Glaurung 2.1, this has been enhanced: It does not allow
     pruning of moves which defend the destination square of the null
     move refutation move, nor of moves which block the ray of the
     piece in the case that the moving piece in the null move
     refutation move is a slider.

   * More conservative use of LMR at PV nodes.  The previous version
     searched the first 6 moves with full depth, 2.1 by default
     searches the first 14 moves with full depth (but there is a new
     UCI parameter for configuring this).  I am not at all sure
     whether this is an improvement.  More thorough testing is
     required. 

   * Feedback from the evaluation to the search.  The search passes an
     object of type 'EvalInfo' to the eval, and the eval fills this
     struct with various potentially useful information (like the sets
     of squares attacked by each piece type, the middle game and
     endgame components of the eval, etc.).  At the moment, almost
     none of this information is actually used by the search.  The
     only exception is that the evaluation function is now used to
     adjust the futility pruning margin in the quiescence search.

   * Less extensions.  This hurts the programs performance a lot in most
     test suites, but I hope it improves the branching factor in deep
     searches.

   * A very long list of new UCI parameters, especially for tuning the
     evaluation. 


6. Terms of use
---------------

Glaurung is free, and distributed under the GNU General Public License
(GPL).  Essentially, this means that you are free to do almost exactly
what you want with the program, including distributing it among your
friends, making it available for download from your web site, selling
it (either by itself or as part of some bigger software package), or
using it as the starting point for a software project of your own.

The only real limitation is that whenever you distribute Glaurung in
some way, you must always include the full source code, or a pointer
to where the source code can be found.  If you make any changes to the
source code, these changes must also be made available under the GPL.

For full details, read the copy of the GPL found in the file named
Copying.txt.


7. Feedback
-----------

The author's e-mail address is tord@glaurungchess.com

