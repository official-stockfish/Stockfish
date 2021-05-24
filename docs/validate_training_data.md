# validate_training_data

`validate_training_data` allows validation of training data of types `.plain`, `.bin`, and `.binpack`.

As all commands in stockfish `validate_training_data` can be invoked either from command line (as `stockfish.exe validate_training_data ...`) or in the interactive prompt.

The syntax of this command is as follows:
```
validate_training_data in_path
```

`in_path` is the path to the file to validate. The type of the data is deduced based on its extension (one of `.plain`, `.bin`, `.binpack`).