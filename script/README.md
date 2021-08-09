# `pgn_to_plain`
This script converts pgn files into text file to apply `learn convert_bin` command. You need to import [python-chess](https://pypi.org/project/python-chess/) to use this script.


    pip install python-chess
	

# Example of Qhapaq's finetune using `pgn_to_plain`

## Download data
You can download data from [here](http://rebel13.nl/index.html)

## Convert pgn files

**Important : convert text will be superheavy (approx 200 byte / position)**

    python pgn_to_plain.py --pgn "pgn/*.pgn" --start_ply 1 --output converted_pgn.txt


`--pgn` option supports wildcard. When you use pgn files with elo >= 3300, You will get 1.7 GB text file.
	
	
## Convert into training data


### Example build command

    make nnue-learn ARCH=x86-64

See `src/Makefile` for detail.


### Convert

    ./stockfish
    learn convert_bin converted_pgn.txt output_file_name pgn_bin.bin
	learn shuffle pgn_bin.bin
	
You also need to prepare validation data for training like following.
	
	python pgn_to_plain.py --pgn "pgn/ccrl-40-15-3400.pgn" --start_ply 1 --output ccrl-40-15-3400.txt
	./stockfish
    learn convert_bin ccrl-40-15-3400.txt ccrl-40-15-3400_plain.bin
	
	
### Learn

    ./stockfish
	setoption name Threads value 8
    learn shuffled_sfen.bin newbob_decay 0.5  validation_set_file_name ccrl-40-15-3400_plain.bin  nn_batch_size 50000 batchsize 1000000 eval_save_interval 8000000 eta 0.05 lambda 0.0 eval_limit 3000 mirror_percentage 0 use_draw_in_training 1


