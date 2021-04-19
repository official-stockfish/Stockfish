# generate_training_data_nonpv

`generate_training_data_nonpv` command allows generation of training data from self-play in a manner that suits training better than traditional games. It plays fixed nodes self play games for exploration and records [some of] the evaluated positions. Then rescores them with fixed depth search.

As all commands in stockfish `generate_training_data_nonpv` can be invoked either from command line (as `stockfish.exe generate_training_data_nonpv ...`, but this is not recommended because it's not possible to specify UCI options before `generate_training_data_nonpv` executes) or in the interactive prompt.

It is recommended to set the `PruneAtShallowDepth` UCI option to `false` as it will increase the quality of fixed depth searches.

It is recommended to keep the `EnableTranspositionTable` UCI option at the default `true` value as it will make the generation process faster without noticably harming the uniformity of the data.

`generate_training_data_nonpv` takes named parameters in the form of `generate_training_data_nonpv param_1_name param_1_value param_2_name param_2_value ...`.

Currently the following options are available:

`depth` - the search depth to use for rescoring. Default: 3.

`count` - the number of training data entries to generate. 1 entry == 1 position. Default: 1000000 (1M).

`exploration_min_nodes` - the min number of nodes to use for exploraton during selfplay. Default: 5000.

`exploration_max_nodes` - the max number of nodes to use for exploraton during selfplay. The number of nodes is chosen from a uniform distribution between min and max. Default: 15000.

`exploration_save_rate` - the ratio of positions seen during exploration self play games that are saved for later rescoring. Default: 0.01 (meaning 1 in 100 positions seen during search get saved for rescoring).

`output_file` - the name of the file to output to. If the extension is not present or doesn't match the selected training data format the right extension will be appened. Default: generated_gensfen_nonpv

`eval_limit` - evaluations with higher absolute value than this will not be written and will terminate a self-play game. Should not exceed 10000 which is VALUE_KNOWN_WIN, but is only hardcapped at mate in 2 (\~30000). Default: 4000

`exploration_eval_limit` - same as `eval_limit` but used during exploration with a value from fixed depth search.

`exploration_min_pieces` - the min number of pieces in the self play games to start the fixed depth search. Note that even if there's N pieces on the board the fixed nodes search usually reaches positions with less pieces and they are saved too. Default: 8.

`exploration_max_ply` the max ply for the exploration self play. Default: 200.

`smart_fen_skipping` - this is a flag option. When specified some position that are not good candidates for teaching are removed from the output. This includes positions where the best move is a capture or promotion, and position where a king is in check.

`book` - a path to an opening book to use for the starting positions. Currently only .epd format is supported. If not specified then the starting position is always the standard chess starting position.

`data_format` - format of the training data to use. Either `bin` or `binpack`. Default: `binpack`.

`seed` - seed for the PRNG. Can be either a number or a string. If it's a string then its hash will be used. If not specified then the current time will be used.
