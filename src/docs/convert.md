# Convert

`convert` allows conversion of training data between any of `.plain`, `.bin`, and `.binpack`.

As all commands in stockfish `convert` can be invoked either from command line (as `stockfish.exe convert ...`) or in the interactive prompt.

The syntax of this command is as follows:
```
convert from_path to_path [append]
```

`from_path` is the path to the file to convert from. The type of the data is deduced based on its extension (one of `.plain`, `.bin`, `.binpack`).
`to_path` is the path to an output file. The type of the data is deduced from its extension. If the file does not exist it is created.

The last argument is optional. If not specified then the output file will be truncated prior to any writes. If the last argument is `append` then the converted training data will be appended to the end of the output file.