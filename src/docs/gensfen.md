# Gensfen

`gensfen` command allows generation of training data from self-play in a manner that suits training better than traditional games. It introduces random moves to diversify openings, and fixed depth evaluation.

As all commands in stockfish `gensfen` can be invoked either from command line (as `stockfish.exe gensfen ...`, but this is not recommended because it's not possible to specify UCI options before `gensfen` executes) or in the interactive prompt.

It is recommended to set the `PruneAtShallowDepth` UCI option to `false` as it will increase the quality of fixed depth searches.

It is recommended to keep the `EnableTranspositionTable` UCI option at the default `true` value as it will make the generation process faster without noticably harming the uniformity of the data.

`gensfen` takes named parameters in the form of `gensfen param_1_name param_1_value param_2_name param_2_value ...`.

Currently the following options are available:

`depth` - minimum depth of evaluation of each position. Default: 3.

`depth2` - maximum depth of evaluation of each position. If not specified then the same as `depth`.

`nodes` - the number of nodes to use for evaluation of each position. This number is multiplied by the number of PVs of the current search. This does NOT override the `depth` and `depth2` options. If specified then whichever of depth or nodes limit is reached first applies.

`loop` - the number of training data entries to generate. 1 entry == 1 position.

`output_file_name` - the name of the file to output to. If the extension is not present or doesn't match the selected training data format the right extension will be appened.

`eval_limit` - evaluations with higher absolute value than this will not be written and will terminate a self-play game. Should not exceed 10000 which is VALUE_KNOWN_WIN, but is only hardcapped at mate in 2 (\~30000).

`random_move_minply` - the minimal ply at which a random move may be executed instead of a move chosen by search

`random_move_maxply` - the maximal ply at which a random move may be executed instead of a move chosen by search

`random_move_count` - maximum number of random moves in a single self-play game

`random_move_like_apery` - either 0 or 1. If 1 then random king moves will be followed by a random king move from the opponent whenever possible with 50% probability.

`random_multi_pv` - the number of PVs used for determining the random move. If not specified then a truly random move will be chosen. If specified then a multiPV search will be performed the random move will be one of the moves chosen by the search.

`random_multi_pv_diff` - Makes the multiPV random move selection consider only moves that are at most `random_multi_pv_diff` worse than the next best move. Default: 30000 (all multiPV moves).

`random_multi_pv_depth` - the depth to use for multiPV search for random move. Defaults to `depth2`.

`write_minply` - minimum ply for which the training data entry will be emitted.

`write_maxply` - maximum ply for which the training data entry will be emitted.

`save_every` - the number of training data entries per file. If not specified then there will be always one file. If specified there may be more than one file generated (each having at most `save_every` training data entries) and each file will have a unique number attached.

`random_file_name` - if specified then the output filename will be chosen randomly. Overrides `output_file_name`.

`write_out_draw_game_in_training_data_generation` - either 0 or 1. If 1 then training data from drawn games will be emitted too. Default: 0.

`use_draw_in_training_data_generation` - deprecated, alias for `write_out_draw_game_in_training_data_generation`

`detect_draw_by_consecutive_low_score` - either 0 or 1. If 1 then drawn games will be adjudicated when the score remains 0 for at least 8 plies after ply 80. Default: 0.

`use_game_draw_adjudication` - deprecated, alias for `detect_draw_by_consecutive_low_score`

`detect_draw_by_insufficient_mating_material` - either 0 or 1. If 1 then position with insufficient material will be adjudicated as draws. Default: 0.

`sfen_format` - format of the training data to use. Either `bin` or `binpack`. Default: `bin`.

`seed` - seed for the PRNG. Can be either a number or a string. If it's a string then its hash will be used. If not specified then the current time will be used.
