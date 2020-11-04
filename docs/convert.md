# Convert

`convert` allows conversion of training data between any of `.plain`, `.bin`, and `.binpack`.

As all commands in stockfish `convert` can be invoked either from command line (as `stockfish.exe convert ...`) or in the interactive prompt.

The syntax of this command is as follows:
```
convert from_path to_path [append] [validate]
```

`from_path` is the path to the file to convert from. The type of the data is deduced based on its extension (one of `.plain`, `.bin`, `.binpack`).
`to_path` is the path to an output file. The type of the data is deduced from its extension. If the file does not exist it is created.

`append` and `validate` can come in any order and are optional.
If `append` not specified then the output file will be truncated prior to any writes. If `append` is specified then the converted training data will be appended to the end of the output file.

If `validate` is specified then the conversion will stop on the first illegal move found and a diagnostic will be shown.