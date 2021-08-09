# Stats

`gather_statistics` command allows gathering various statistics from a .bin or a .binpack file. The syntax is `gather_statistics (GROUP)* input_file FILENAME`. There can be many groups specified. Any statistic gatherer that belongs to at least one of the specified groups will be used.

Simplest usage: `stockfish.exe gather_statistics all input_file a.binpack`

Any name that doesn't designate an argument name or is not an argument will be interpreted as a group name.

## Parameters

`input_file` - the path to the .bin or .binpack input file to read

`output_file` - optional path to the output file to write the results too. Results are always written on the console, so if this is specified the results will be written in both places.

`max_count` - the maximum number of positions to process. Default: no limit.

## Groups

`all` - a special group designating all statistics gatherers available.

`position_count` - the total number of positions in the file.

`king`, `king_square_count` - the number of times a king was on each square. Output is layed out as a chessboard, with the 8th rank being the topmost. Separate values for white and black kings.

`move`, `move_from_count` - same as `king_square_count` but for from_sq(move)

`move`, `move_to_count` - same as `king_square_count` but for to_sq(move)

`move`, `move_type` - the number of moves with each type. Includes normal, captures, castling, promotions, enpassant. The groups are not disjoint.

`move`, `moved_piece_type` - the number of times a piece of each type was moved

`piece_count` - the histogram of the number of pieces on the board

`ply_discontinuities` - the number of times the ply jumped by a value different than 1 between two consecutive positions. Usually the number of games.

`material_imbalance` - the histogram of imbalances, with values computed using "simple eval", that is pawn=1, bishop=knight=3, rook=5, queen=9

`results` - the distribution of game results

`endgames_6man` - distribution of endgame configurations for <=6 pieces (including kings)
