<p align="center">
  <img src="https://cdn.discordapp.com/attachments/724700045525647420/729135226365804594/SFNNUE2.png">
</p>

<h1 align="center">Stockfish NNUE</h1>

## Overview
Stockfish NNUE is a port of a shogi neural network named NNUE (efficiently updateable neural network backwards) to Stockfish 11. To learn more about the Stockfish chess engine, look [here](stockfish.md) for an overview and [here](https://github.com/official-stockfish/Stockfish) for the official repository.

## Training Guide
### Generating Training Data
Use the "no-nnue.nnue-gen-sfen-from-original-eval" binary. The given example is generation in its simplest form. There are more commands. 
```
uci
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
Use the "avx2.halfkp_256x2-32-32.nnue-learn.2020-07-11" binary. Create an empty folder named "evalsave" in the same directory as the binaries.
```
uci
setoption name SkipLoadingEval value true
setoption name Threads value x
isready
learn targetdir trainingdata loop 100 batchsize 1000000 use_draw_in_training 1 use_draw_in_validation 1 eta 1 lambda 1 eval_limit 32000 nn_batch_size 1000 newbob_decay 0.5 eval_save_interval 250000000 loss_output_interval 1000000 mirror_percentage 50 validation_set_file_name validationdata\val.bin
```
Nets get saved in the "evalsave" folder. 

#### Training Parameters
- eta is the learning rate
- lambda is the amount of weight it puts to eval of learning data vs win/draw/loss results. 1 puts all weight on eval, lambda 0 puts all weight on WDL results.

### Reinforcement Learning
If you would like to do some reinforcement learning on your original network, you must first generate training data using the learn binaries. Make sure that your previously trained network is in the eval folder. Use the commands specified above. Make sure `SkipLoadingEval` is set to false so that the data generated is using the neural net's eval by typing the command `uci setoption name SkipLoadingEval value false` before typing the `isready` command. You should aim to generate less positions than the first run, around 1/10 of the number of positions generated in the first run. The depth should be higher as well. You should also do the same for validation data, with the depth being higher than the last run.

After you have generated the training data, you must move it into your training data folder and delete the older data so that the binary does not accidentally train on the same data again. Do the same for the validation data and name it to val-1.bin to make it less confusing. Make sure the evalsave folder is empty. Then, using the same binary, type in the training commands shown above. Do __NOT__ set `SkipLoadingEval` to true, it must be false or you will get a completely new network, instead of a network trained with reinforcement learning. You should also set eval_save_interval to a number that is lower than the amount of positions in your training data, perhaps also 1/10 of the original value. The validation file should be set to the new validation data, not the old data.

After training is finished, your new net should be located in the "final" folder under the "evalsave" directory. You should test this new network against the older network to see if there are any improvements.

## Using Your Trained Net
If you want to use your generated net, copy the net located in the "final" folder under the "evalsave" directory and move it into a new folder named "eval" under the directory with the binaries. You can then use the halfkp_256x2 binaries pertaining to your CPU with a standard chess GUI, such as Cutechess. Refer to the [releases page](https://github.com/nodchip/Stockfish/releases) to find out which binary is best for your CPU.

If the engine does not load any net file, or shows "Error! *** not found or wrong format", please try to sepcify the net with the full file path with the "EvalFile" option by typing the command `setoption name EvalFile value path` where path is the full file path.

## Resources
- [Stockfish NNUE Wiki](https://www.qhapaq.org/shogi/shogiwiki/stockfish-nnue/)
- [Training instructions](https://twitter.com/mktakizawa/status/1273042640280252416) from the creator of the Elmo shogi engine
- [Original Talkchess thread](http://talkchess.com/forum3/viewtopic.php?t=74059) discussing Stockfish NNUE
- [Guide to Stockfish NNUE](http://yaneuraou.yaneu.com/2020/06/19/stockfish-nnue-the-complete-guide/) 
- [Unofficial Stockfish Discord](https://discord.gg/nv8gDtt)

A more updated list can be found in the #sf-nnue-resources channel in the Discord.
