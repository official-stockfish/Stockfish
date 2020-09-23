<p align="center">
  <img src="https://cdn.discordapp.com/attachments/724700045525647420/729135226365804594/SFNNUE2.png">
</p>

<h1 align="center">Stockfish NNUE</h1>

## Overview
Stockfish NNUE is a port of a shogi neural network named NNUE (efficiently updateable neural network backwards) to Stockfish 11. To learn more about the Stockfish chess engine, look [here](stockfish.md) for an overview and [here](https://github.com/official-stockfish/Stockfish) for the official repository.

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

- `blas=[yes/no]` - whether to use an external BLAS library. Default is `no`. Using an external BLAS library may have a significantly improve learning performance and by default expects openBLAS to be installed.

  * #### Ponder
    Let Stockfish ponder its next move while the opponent is thinking.

  * #### MultiPV
    Output the N best lines (principal variations, PVs) when searching.
    Leave at 1 for best performance.

  * #### Use NNUE
    Toggle between the NNUE and classical evaluation functions. If set to "true",
    the network parameters must be available to load from file (see also EvalFile),
    if they are not embedded in the binary.

  * #### EvalFile
    The name of the file of the NNUE evaluation parameters. Depending on the GUI the
    filename might have to include the full path to the folder/directory that contains the file.
    Other locations, such as the directory that contains the binary and the working directory,
    are also searched.

  * #### UCI_AnalyseMode
    An option handled by your GUI.

  * #### UCI_Chess960
    An option handled by your GUI. If true, Stockfish will play Chess960.

  * #### UCI_ShowWDL
    If enabled, show approximate WDL statistics as part of the engine output.
    These WDL numbers model expected game outcomes for a given evaluation and
    game ply for engine self-play at fishtest LTC conditions (60+0.6s per game).

  * #### UCI_LimitStrength
    Enable weaker play aiming for an Elo rating as set by UCI_Elo. This option overrides Skill Level.

  * #### UCI_Elo
    If enabled by UCI_LimitStrength, aim for an engine strength of the given Elo.
    This Elo rating has been calibrated at a time control of 60s+0.6s and anchored to CCRL 40/4.

  * #### Skill Level
    Lower the Skill Level in order to make Stockfish play weaker (see also UCI_LimitStrength).
    Internally, MultiPV is enabled, and with a certain probability depending on the Skill Level a
    weaker move will be played.

  * #### SyzygyPath
    Path to the folders/directories storing the Syzygy tablebase files. Multiple
    directories are to be separated by ";" on Windows and by ":" on Unix-based
    operating systems. Do not use spaces around the ";" or ":".

    Example: `C:\tablebases\wdl345;C:\tablebases\wdl6;D:\tablebases\dtz345;D:\tablebases\dtz6`

    It is recommended to store .rtbw files on an SSD. There is no loss in storing
    the .rtbz files on a regular HD. It is recommended to verify all md5 checksums
    of the downloaded tablebase files (`md5sum -c checksum.md5`) as corruption will
    lead to engine crashes.

  * #### SyzygyProbeDepth
    Minimum remaining search depth for which a position is probed. Set this option
    to a higher value to probe less agressively if you experience too much slowdown
    (in terms of nps) due to TB probing.

  * #### Syzygy50MoveRule
    Disable to let fifty-move rule draws detected by Syzygy tablebase probes count
    as wins or losses. This is useful for ICCF correspondence games.

  * #### SyzygyProbeLimit
    Limit Syzygy tablebase probing to positions with at most this many pieces left
    (including kings and pawns).

  * #### Contempt
    A positive value for contempt favors middle game positions and avoids draws,
    effective for the classical evaluation only.

  * #### Analysis Contempt
    By default, contempt is set to prefer the side to move. Set this option to "White"
    or "Black" to analyse with contempt for that side, or "Off" to disable contempt.

  * #### Move Overhead
    Assume a time delay of x ms due to network and GUI overheads. This is useful to
    avoid losses on time in those cases.

  * #### Slow Mover
    Lower values will make Stockfish take less time in games, higher values will
    make it think longer.

  * #### nodestime
    Tells the engine to use nodes searched instead of wall time to account for
    elapsed time. Useful for engine testing.

  * #### Clear Hash
    Clear the hash table.

  * #### Debug Log File
    Write all communication to and from the engine into a text file.

## A note on classical and NNUE evaluation

Both approaches assign a value to a position that is used in alpha-beta (PVS) search
to find the best move. The classical evaluation computes this value as a function
of various chess concepts, handcrafted by experts, tested and tuned using fishtest.
The NNUE evaluation computes this value with a neural network based on basic
inputs (e.g. piece positions only). The network is optimized and trained
on the evaluations of millions of positions at moderate search depth.

The NNUE evaluation was first introduced in shogi, and ported to Stockfish afterward.
It can be evaluated efficiently on CPUs, and exploits the fact that only parts
of the neural network need to be updated after a typical chess move.
[The nodchip repository](https://github.com/nodchip/Stockfish) provides additional
tools to train and develop the NNUE networks.

On CPUs supporting modern vector instructions (avx2 and similar), the NNUE evaluation
results in stronger playing strength, even if the nodes per second computed by the engine
is somewhat lower (roughly 60% of nps is typical).

Note that the NNUE evaluation depends on the Stockfish binary and the network parameter
file (see EvalFile). Not every parameter file is compatible with a given Stockfish binary.
The default value of the EvalFile UCI option is the name of a network that is guaranteed
to be compatible with that binary.

## What to expect from Syzygybases?

If the engine is searching a position that is not in the tablebases (e.g.
a position with 8 pieces), it will access the tablebases during the search.
If the engine reports a very large score (typically 153.xx), this means
that it has found a winning line into a tablebase position.

If the engine is given a position to search that is in the tablebases, it
will use the tablebases at the beginning of the search to preselect all
good moves, i.e. all moves that preserve the win or preserve the draw while
taking into account the 50-move rule.
It will then perform a search only on those moves. **The engine will not move
immediately**, unless there is only a single good move. **The engine likely
will not report a mate score even if the position is known to be won.**

It is therefore clear that this behaviour is not identical to what one might
be used to with Nalimov tablebases. There are technical reasons for this
difference, the main technical reason being that Nalimov tablebases use the
DTM metric (distance-to-mate), while Syzygybases use a variation of the
DTZ metric (distance-to-zero, zero meaning any move that resets the 50-move
counter). This special metric is one of the reasons that Syzygybases are
more compact than Nalimov tablebases, while still storing all information
needed for optimal play and in addition being able to take into account
the 50-move rule.

## Large Pages

Stockfish supports large pages on Linux and Windows. Large pages make
the hash access more efficient, improving the engine speed, especially
on large hash sizes. Typical increases are 5..10% in terms of nps, but
speed increases up to 30% have been measured. The support is
automatic. Stockfish attempts to use large pages when available and
will fall back to regular memory allocation when this is not the case.

### Support on Linux

Large page support on Linux is obtained by the Linux kernel
transparent huge pages functionality. Typically, transparent huge pages
are already enabled and no configuration is needed.

### Support on Windows

The use of large pages requires "Lock Pages in Memory" privilege. See
[Enable the Lock Pages in Memory Option (Windows)](https://docs.microsoft.com/en-us/sql/database-engine/configure-windows/enable-the-lock-pages-in-memory-option-windows)
on how to enable this privilege. Logout/login may be needed
afterwards. Due to memory fragmentation, it may not always be
possible to allocate large pages even when enabled. A reboot
might alleviate this problem. To determine whether large pages
are in use, see the engine log.

## Compiling Stockfish yourself from the sources

Stockfish has support for 32 or 64-bit CPUs, certain hardware
instructions, big-endian machines such as Power PC, and other platforms.

On Unix-like systems, it should be easy to compile Stockfish
directly from the source code with the included Makefile in the folder
`src`. In general it is recommended to run `make help` to see a list of make
targets with corresponding descriptions.

```
uci
setoption name Use NNUE value false
setoption name Threads value x
setoption name Hash value y
setoption name SyzygyPath value path
isready
gensfen depth a loop b use_draw_in_training_data_generation 1 eval_limit 32000
```
Specify how many threads and how much memory you would like to use with the x and y values. The option SyzygyPath is not necessary, but if you would like to use it, you must first have Syzygy endgame tablebases on your computer, which you can find [here](http://oics.olympuschess.com/tracker/index.php). You will need to have a torrent client to download these tablebases, as that is probably the fastest way to obtain them. The path is the path to the folder containing those tablebases. It does not have to be surrounded in quotes.

This will save a file named "generated_kifu.bin" in the same folder as the binary. Once generation is done, rename the file to something like "1billiondepth12.bin" to remember the depth and quantity of the positions and move it to a folder named "trainingdata" in the same directory as the binaries.
#### Generation Parameters
- Depth is the searched depth per move, or how far the engine looks forward. This value is an integer.
- Loop is the amount of positions generated. This value is also an integer
### Generating Validation Data
The process is the same as the generation of training data, except for the fact that you need to set loop to 1 million, because you don't need a lot of validation data. The depth should be the same as before or slightly higher than the depth of the training data. After generation rename the validation data file to val.bin and drop it in a folder named "validationdata" in the same directory to make it easier. 
### Training a Completely New Network
Use the "learn" binary. Create an empty folder named "evalsave" in the same directory as the binaries.
```
uci
setoption name SkipLoadingEval value true
setoption name Training value true
setoption name Use NNUE value true
setoption name Threads value x
isready
learn targetdir trainingdata loop 100 batchsize 1000000 use_draw_in_training 1 use_draw_in_validation 1 eta 1 lambda 1 eval_limit 32000 nn_batch_size 1000 newbob_decay 0.5 eval_save_interval 250000000 loss_output_interval 1000000 mirror_percentage 50 validation_set_file_name validationdata\val.bin
```
Nets get saved in the "evalsave" folder. 

#### Training Parameters
- eta is the learning rate
- lambda is the amount of weight it puts to eval of learning data vs win/draw/loss results. 1 puts all weight on eval, lambda 0 puts all weight on WDL results.

### Reinforcement Learning
If you would like to do some reinforcement learning on your original network, you must first generate training data using the learn binaries with the setting `Use NNUE` set to `pure`. Make sure that your previously trained network is in the eval folder. Use the commands specified above. Make sure `SkipLoadingEval` is set to false so that the data generated is using the neural net's eval by typing the command `setoption name SkipLoadingEval value false` before typing the `isready` command. You should aim to generate less positions than the first run, around 1/10 of the number of positions generated in the first run. The depth should be higher as well. You should also do the same for validation data, with the depth being higher than the last run.

After you have generated the training data, you must move it into your training data folder and delete the older data so that the binary does not accidentally train on the same data again. Do the same for the validation data and name it to val-1.bin to make it less confusing. Make sure the evalsave folder is empty. Then, using the same binary, type in the training commands shown above. Do __NOT__ set `SkipLoadingEval` to true, it must be false or you will get a completely new network, instead of a network trained with reinforcement learning. You should also set eval_save_interval to a number that is lower than the amount of positions in your training data, perhaps also 1/10 of the original value. The validation file should be set to the new validation data, not the old data.

After training is finished, your new net should be located in the "final" folder under the "evalsave" directory. You should test this new network against the older network to see if there are any improvements.

## Using Your Trained Net
If you want to use your generated net, copy the net located in the "final" folder under the "evalsave" directory and move it into a new folder named "eval" under the directory with the binaries. You can then use the halfkp_256x2 binaries pertaining to your CPU with a standard chess GUI, such as Cutechess. Refer to the [releases page](https://abrok.eu/stockfish) to find out which binary is best for your CPU.

If the engine does not load any net file, or shows "Error! *** not found or wrong format", please try to specify the net with the full file path with the "EvalFile" option by typing the command `setoption name EvalFile value path` where path is the full file path. The "Use NNUE" option must be set to true with the command `setoption name Use NNUE value true`.

## Resources
- [Stockfish NNUE Wiki](https://www.qhapaq.org/shogi/shogiwiki/stockfish-nnue/)
- [Training instructions](https://twitter.com/mktakizawa/status/1273042640280252416) from the creator of the Elmo shogi engine
- [Original Talkchess thread](http://talkchess.com/forum3/viewtopic.php?t=74059) discussing Stockfish NNUE
- [Guide to Stockfish NNUE](http://yaneuraou.yaneu.com/2020/06/19/stockfish-nnue-the-complete-guide/) 
- [Unofficial Stockfish Discord](https://discord.gg/nv8gDtt)

A more updated list can be found in the #sf-nnue-resources channel in the Discord.
