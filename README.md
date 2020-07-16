# Stockfish NNUE

## Overview
Stockfish NNUE is a port of a shogi neural network named NNUE (efficiently updateable neural network backwards) to Stockfish 11.

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
### Generating validation data
The process is the same as the generation of training data, except for the fact that you need to set loop to 1 million, because you don't need a lot of validation data. The depth should be the same as before or a little higher than the depth of the training data. After generation rename the validation data file to val.bin and drop it in a folder named "validationdata" in the same directory to make it easier. 
### Training a completely new network
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

### Using the Trained Net
If you want to use your generated net, copy the net located in the "final" folder under the "evalsave" directory and move it into the "eval" folder. You can then use the halfkp_256x2 binaries with a standard chess GUI, such as Cutechess.
