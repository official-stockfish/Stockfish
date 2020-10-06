# Learn

`learn` command allows training a network from training data.

As all commands in stockfish `learn` can be invoked either from command line (as `stockfish.exe learn ...`, but this is not recommended because it's not possible to specify UCI options before `learn` executes) or in the interactive prompt.

`learn` takes named parameters in the form of `learn param_1_name param_1_value param_2_name param_2_value ...`. Unrecognized parameters form a list of paths to training data files.

It is recommended to set the `EnableTranspositionTable` UCI option to `false` to reduce the interference between qsearches which are used to provide shallow evaluation. Using TT may cause the shallow evaluation to diverge from the real evaluation of the net, hiding imperfections.

It is recommended to set the `PruneAtShallowDepth` UCI option to `false` as it will provide more accurate shallow evaluation.

It is **required** to set the `Use NNUE` UCI option to `pure` as otherwise the function being optimized will not always match the function being probed, in which case not much can be learned.

Currently the following options are available:

`bat` - the size of a batch in multiples of 10000. This determines how many entries are read and shuffled at once during training. Default: 1000 (meaning batch size of 1000000).

`targetdir` - path to the direction from which training data will be read. All files in this directory are read sequentially. If not specified then only the list of files from positional arguments will be used. If specified then files from the given directory will be used after the explicitly specified files.

`loop` - the number of times to loop over all training data.

`basedir` - the base directory for the paths. Default: "" (current directory)

`batchsize` - same as `bat` but doesn't scale by 10000

`lr` - initial learning rate. Default: 1.

`use_draw_games_in_training` - either 0 or 1. If 1 then draws will be used in training too. Default: 0.

`use_draw_in_training` - deprecated, alias for `use_draw_games_in_training`

`use_draw_games_in_validation` - either 0 or 1. If 1 then draws will be used in validation too. Default: 0.

`use_draw_in_validation` - deprecated, alias for `use_draw_games_in_validation`

`skip_duplicated_positions_in_training` - either 0 or 1. If 1 then a small hashtable will be used to try to eliminate duplicated position from training. Default: 0.

`use_hash_in_training` - deprecated, alias for `skip_duplicated_positions_in_training`

`winning_probability_coefficient` - some magic value for winning probability. If you need to read this then don't touch it. Default: 1.0 / PawnValueEg / 4.0 * std::log(10.0)

`use_wdl` - either 0 or 1. If 1 then the evaluations will be converted to win/draw/loss percentages prior to learning on them. (Slightly changes the gradient because eval has a different derivative than wdl). Default: 0.

`lambda` - value in range [0..1]. 1 means that only evaluation is used for learning, 0 means that only game result is used. Values inbetween result in interpolation between the two contributions. See `lambda_limit` for when this is applied. Default: 0.33.

`lambda2` - value in range [0..1]. 1 means that only evaluation is used for learning, 0 means that only game result is used. Values inbetween result in interpolation between the two contributions. See `lambda_limit` for when this is applied. Default: 0.33.

`lambda_limit` - the maximum absolute score value for which `lambda` is used as opposed to `lambda2`. For positions with absolute evaluation higher than `lambda_limit` `lambda2` will be used. Default: 32000 (so always `lambda`).

`reduction_gameply` - the minimum ply after which positions won't be skipped. Positions at plies below this value are skipped with a probability that lessens linearly with the ply (reaching 0 at `reduction_gameply`). Default: 1.

`eval_limit` - positions with absolute evaluation higher than this will be skipped. Default: 32000 (nothing is skipped).

`save_only_once` - this is a modifier not a parameter, no value follows it. If specified then there will be only one network file generated.

`no_shuffle` - this is a modifier not a parameter, no value follows it. If specified then data within a batch won't be shuffled.

`nn_batch_size` - minibatch size used for learning. Should be smaller than batch size. Default: 1000.

`newbob_decay` - learning rate will be multiplied by this factor every time a net is rejected (so in other words it controls LR drops). Default: 1.0 (no LR drops)

`newbob_num_trials` - determines after how many subsequent rejected nets the training process will be terminated. Default: 2.

`nn_options` - if you're reading this you don't use it. It passes messages directly to the network evaluation. I don't know what it can do either.

`eval_save_interval` - every `eval_save_interval` positions the network will be saved and either accepted or rejected (in which case an LR drop follows). Default: 1000000000 (1B). (generally people use values in 10M-100M range)

`loss_output_interval` - every `loss_output_interval` fitness statistics are displayed. Default: `batchsize`

`validation_set_file_name` - path to the file with training data to be used for validation (loss computation and move accuracy)

`seed` - seed for the PRNG. Can be either a number or a string. If it's a string then its hash will be used. If not specified then the current time will be used.

## Legacy subcommands and parameters

### Convert

`convert_plain`
`convert_bin`
`interpolate_eval`
`check_invalid_fen`
`check_illegal_move`
`convert_bin_from_pgn-extract`
`pgn_eval_side_to_move`
`convert_no_eval_fens_as_score_zero`
`src_score_min_value`
`src_score_max_value`
`dest_score_min_value`
`dest_score_max_value`

### Shuffle

`shuffle`
`buffer_size`
`shuffleq`
`shufflem`
`output_file_name`