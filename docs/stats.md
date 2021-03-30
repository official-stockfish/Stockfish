# Stats

`gather_statistics` command allows gathering various statistics from a .bin or a .binpack file. The syntax is `gather_statistics (GROUP)* input_file FILENAME`. There can be many groups specified. Any statistic gatherer that belongs to at least one of the specified groups will be used.

Simplest usage: `stockfish.exe gather_statistics all input_file a.binpack`

Any name that doesn't designate an argument name or is not an argument will be interpreted as a group name.

## Parameters

`input_file` - the path to the .bin or .binpack input file to read

`max_count` - the maximum number of positions to process. Default: no limit.

## Groups

`all`

 - A special group designating all statistics gatherers available.

`position_count`

 - `struct PositionCounter` - the total number of positions in the file.
