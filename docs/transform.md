# Transform

`transform` command exposes subcommands that perform some specific transformation over data. The call syntax is `transform <subcommand>`. Currently implemented subcommands are listed and described below.

## `nudged_static`

`transform nudged_static` takes named parameters in the form of `gensfen param_1_name param_1_value param_2_name param_2_value ...` and flag parameters which don't require values.

This command goes through positions in the input files and replaces the scores with new ones - generated from static eval - but slightly adjusted based on the scores in the original input file.

Currently the following options are available:

`input_file` - path to the input file. Supports bin and binpack formats. Default: in.binpack.

`output_file` - path to the output file. Supports bin and binpack formats. Default: out.binpack.

`absolute` - states that the adjustment should be bounded by an absolute value. After this token follows the maximum absolute adjustment. Values are always adjusted towards scores in the input file. This is the default mode. Default maximum adjustement: 5.

`relative` - states that the adjustment should be bounded by a value relative in magnitude to the static eval value. After this token follows the maximum relative change - a floating point value greater than 0. For example a value of 0.1 only allows changing the static eval by at most 10% towards the score from the input file.

`interpolate` states that the output score should be a value interpolated between static eval and the score from the input file. After this token follows the interpolation constant `t`. `t` of 0 means that only static eval is used. `t` of 1 means that only score from the input file is used. `t` of 0.5 means that the static eval and input score are averaged. It accepts values outside of range `<0, 1>`, but the usefulness is questionable.
