# Learn

`learn` command allows training a network from training data.

As all commands in stockfish `learn` can be invoked either from command line (as `stockfish.exe learn ...`, but this is not recommended because it's not possible to specify UCI options before `learn` executes) or in the interactive prompt.

`learn` takes named parameters in the form of `learn param_1_name param_1_value param_2_name param_2_value ...`. Unrecognized parameters form a list of paths to training data files.

It is recommended to set the `EnableTranspositionTable` UCI option to `false` to reduce the interference between qsearches which are used to provide shallow evaluation. Using TT may cause the shallow evaluation to diverge from the real evaluation of the net, hiding imperfections.

It is recommended to set the `PruneAtShallowDepth` UCI option to `false` as it will provide more accurate shallow evaluation.

It is **required** to set the `Use NNUE` UCI option to `pure` as otherwise the function being optimized will not always match the function being probed, in which case not much can be learned.

Currently the following options are available:

`set_recommended_uci_options` - this is a modifier not a parameter, no value follows it. If specified then some UCI options are set to recommended values.

`targetdir` - path to the direction from which training data will be read. All files in this directory are read sequentially. If not specified then only the list of files from positional arguments will be used. If specified then files from the given directory will be used after the explicitly specified files.

`epochs` - the number of weight update cycles (epochs) to train the network for. One such cycle is `epoch_size` positions. If not specified then the training will loop forever.

`warmup_epochs` - the number of epochs to "pretrain" the net for with `warmup_lr` learning rate. Default: 0.

`epoch_size` - The number of positions per epoch. Should be kept lowish as the current implementation loads all into memory before processing. Default is already high enough. The epoch size is not tied to validation nor net serialization, there are more specific options for that. Default: 1000000

`basedir` - the base directory for the paths. Default: "" (current directory)

`lr` - initial learning rate. Default: 1.

`warmup_lr` - the learning rate to use during warmup epochs. Default: 0.1.

`use_draw_games_in_training` - either 0 or 1. If 1 then draws will be used in training too. Default: 1.

`use_draw_games_in_validation` - either 0 or 1. If 1 then draws will be used in validation too. Default: 1.

`skip_duplicated_positions_in_training` - either 0 or 1. If 1 then a small hashtable will be used to try to eliminate duplicated position from training. Default: 0.

`winning_probability_coefficient` - some magic value for winning probability. If you need to read this then don't touch it. Default: 1.0 / PawnValueEg / 4.0 * std::log(10.0)

`use_wdl` - either 0 or 1. If 1 then the evaluations will be converted to win/draw/loss percentages prior to learning on them. (Slightly changes the gradient because eval has a different derivative than wdl). Default: 0.

`lambda` - value in range [0..1]. 1 means that only evaluation is used for learning, 0 means that only game result is used. Values inbetween result in interpolation between the two contributions. See `lambda_limit` for when this is applied. Default: 1.0.

`lambda2` - value in range [0..1]. 1 means that only evaluation is used for learning, 0 means that only game result is used. Values inbetween result in interpolation between the two contributions. See `lambda_limit` for when this is applied. Default: 1.0.

`lambda_limit` - the maximum absolute score value for which `lambda` is used as opposed to `lambda2`. For positions with absolute evaluation higher than `lambda_limit` `lambda2` will be used. Default: 32000 (so always `lambda`).

`max_grad` - the maximum allowed loss gradient for backpropagation. Effectively a form of gradient clipping. Useful for the first iterations with a randomly generated net as with higher lr backpropagation often overshoots and kills the net. The default value is fairly conservative, values as low as 0.25 could be used with lr of 1.0 without problems. Default: 1.0.

`reduction_gameply` - the minimum ply after which positions won't be skipped. Positions at plies below this value are skipped with a probability that lessens linearly with the ply (reaching 0 at `reduction_gameply`). Default: 1.

`eval_limit` - positions with absolute evaluation higher than this will be skipped. Default: 32000 (nothing is skipped).

`save_only_once` - this is a modifier not a parameter, no value follows it. If specified then there will be only one network file generated.

`no_shuffle` - this is a modifier not a parameter, no value follows it. If specified then data within a batch won't be shuffled.

`batch_size` - the number of positions per one learning step. Default: 1000

`lr_step` - learning rate will be multiplied by this factor every time a net is rejected (so in other words it controls LR drops). Default: 0.5 (no LR drops)

`assume_quiet` - this is a flag option. When specified learn will not perform qsearch to reach a quiet position.

`smart_fen_skipping` - this is a flag option. When specified some position that are not good candidates for teaching are skipped. This includes positions where the best move is a capture or promotion, and position where a king is in check. Default: 0.

`smart_fen_skipping_for_validation` - same as `smart_fen_skipping` but applies to validation data set. Default: 0.

`max_consecutive_rejections` - determines after how many subsequent rejected nets the training process will be terminated. Default: 4.

`auto_lr_drop` - every time this many positions are processed the learning rate is multiplied by `newbob_decay`. In other words this value specifies for how many positions a single learning rate stage lasts. If 0 then doesn't have any effect. Default: 0.

`nn_options` - if you're reading this you don't use it. It passes messages directly to the network evaluation. I don't know what it can do either.

`eval_save_interval` - every `eval_save_interval` positions the network will be saved and either accepted or rejected (in which case an LR drop follows). Default: 100000000 (100M). (generally people use values in 10M-100M range)

`loss_output_interval` - every `loss_output_interval` fitness statistics are displayed. Default: 1000000 (1M)

`validation_set_file_name` - path to the file with training data to be used for validation (loss computation and move accuracy)

`validation_count` - the number of positions to use for validation. Default: 2000.

`sfen_read_size` - the number of sfens to always keep in the buffer. Default: 10000000 (10M)

`thread_buffer_size` - the number of sfens to copy at once to each thread requesting more sfens for learning. Default: 10000

`seed` - seed for the PRNG. Can be either a number or a string. If it's a string then its hash will be used. If not specified then the current time will be used.

`verbose` - this is a modifier, not a parameter. When used there will be more detailed output during training.

### Deprecated options

`bat` (deprecated) - the size of a batch in multiples of 10000. This determines how many entries are read and shuffled at once during training. Default: 100 (meaning batch size of 1000000).

`newbob_num_trials` (deprecated) - same as `max_consecutive_rejections`

`newbob_decay` (deprecated) - same as `lr_step`

`nn_batch_size` (deprecated) - same as `batch_size`

`use_hash_in_training` (deprecated) - alias for `skip_duplicated_positions_in_training`

`batchsize` (deprecated) - same as `epoch_size`

`use_draw_in_training` (deprecated) - alias for `use_draw_games_in_training`

`use_draw_in_validation` (deprecated) - alias for `use_draw_games_in_validation`

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
