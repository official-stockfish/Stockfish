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

More information about gensfen and available options can be found in the [docs](docs/gensfen.md)

### Training a network

#### Training a Completely New Network

Whether a new network is created or not is controlled by the UCI option `SkipLoadingEval`. If set to true then a new network will be created, which allows learning from scratch. If left at its default (false) then a network will be loaded and trained further. The second scenario is described in the reinforcement learning paragraph.

A simple command chain to start with training could look like this:

```
uci
setoption name EnableTranspositionTable value false
setoption name PruneAtShallowDepth value false
setoption name SkipLoadingEval value true
setoption name Use NNUE value pure
setoption name Threads value x
isready
learn targetdir trainingdata epochs 10000 batchsize 1000000 use_draw_in_training 1 use_draw_in_validation 1 lr 1 lambda 1 eval_limit 32000 nn_batch_size 1000 newbob_decay 0.5 eval_save_interval 250000000 loss_output_interval 1000000 validation_set_file_name validationdata\val.binpack
```

This will utilize training data files in the "trainingdata" directory and validation data from file "validationdata\val.bin". Produced nets are saved in the "evalsave" folder.

More information about learn and available parameters can be found in the [docs](docs/learn.md)

#### Reinforcement Learning

If you would like to do some reinforcement learning on your original network, you must first generate training data with the setting `Use NNUE` set to `pure` and using the previous network (either name it "nn.bin" and put into alongside the binary or provide the `EvalFile` UCI option). Use the commands specified above. You should aim to generate less positions than the first run, around 1/10 of the number of positions generated in the first run. The depth should be higher as well. You should also do the same for validation data, with the depth being higher than the last run.

After you have generated the training data, you must move it into your training data folder and move the older data so that the binary does not train on the same data again. Do the same for the validation data. Make sure the "evalsave" folder is empty. Then, using the same binary, type in the training commands shown above. Do __NOT__ set `SkipLoadingEval` to true, it must be false or you will get a completely new network, instead of a network trained with reinforcement learning. You should also set `eval_save_interval` to a number that is lower than the amount of positions in your training data, perhaps also 1/10 of the original value.

After training is finished, your new net should be located in the "final" folder under the "evalsave" directory. You should test this new network against the older network to see if there are any improvements. Don't rely on the automatic rejection for network quality, sometimes even rejected nets can be better than the previous ones.

## Using Your Trained Net

If you want to use your generated net, copy the net located in the "final" folder under the "evalsave" directory and move it into a new folder named "eval" under the directory with the binaries. You can then use the halfkp_256x2 binaries pertaining to your CPU with a standard chess GUI, such as Cutechess. Refer to the [releases page](https://abrok.eu/stockfish) to find out which binary is best for your CPU.

If the engine does not load any net file, or shows "Error! *** not found or wrong format", please try to specify the net with the full file path with the `EvalFile` UCI option by typing the command `setoption name EvalFile value path` where path is the full file path. The `Use NNUE` UCI option must be set either to `true` or `pure` with the command `setoption name Use NNUE value true/pure`.

## Training data formats.

Currently there are 3 training data formats. Two of them are supported directly.

- `.bin` - the original training data format. Uses 40 bytes per entry. Is supported directly by the `gensfen` and `learn` commands.
- `.plain` - a human readable training data format. This one is not supported directly by the `gensfen` and `learn` commands. It should not be used for data exchange because it's less compact than other formats. It is mostly useful for inspection of the data.
- `.binpack` - a compact binary training data format that exploits positions chains to further reduce size. It uses on average between 2 to 3 bytes per entry when generating data with `gensfen`. It is supported directly by `gensfen` and `learn` commands. It is currently the default for the `gensfen` command. A more in depth description can be found [here](docs/binpack.md)

### Conversion between formats.

There is a builting converted that support all 3 formats described above. Any of them can be converted to any other. For more information and usage guide see [here](docs/convert.md).

## Resources

- [Training NNUE for SF](https://docs.google.com/document/d/1os5GH8GGJbV0nKAfXD-qySBclFzKKtXKHbAnA-un8tA/edit) google document with important information and coding priorities
- [Gensfen data (vondele)](https://drive.google.com/drive/folders/1mftuzYdl9o6tBaceR3d_VBQIrgKJsFpl) over 2b fens available
- [Stockfish NNUE Wiki](https://www.qhapaq.org/shogi/shogiwiki/stockfish-nnue/)
- [Training instructions](https://twitter.com/mktakizawa/status/1273042640280252416) from the creator of the Elmo shogi engine
- [Original Talkchess thread](http://talkchess.com/forum3/viewtopic.php?t=74059) discussing Stockfish NNUE
- [Guide to Stockfish NNUE](http://yaneuraou.yaneu.com/2020/06/19/stockfish-nnue-the-complete-guide/)
- [Unofficial Stockfish Discord](https://discord.gg/nv8gDtt)

A more updated list can be found in the #sf-nnue-resources channel in the Discord.
