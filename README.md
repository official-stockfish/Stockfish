# Stockfish

Stockfish is a powerful UCI chess engine derived from Glaurung 2.1. It analyzes chess positions and computes optimal moves.

- **Website:** [Stockfish Official](https://stockfishchess.org/)
- **Documentation:** [Stockfish Docs](https://stockfishchess.org/documentation/)
- **Discord:** [Stockfish Discord](https://discord.gg/j9emv3s)

## Overview

Stockfish is distributed under the GNU General Public License version 3 (GPL v3), making it free for various uses. It doesn't include a graphical user interface (GUI), so you'll need to use it with compatible GUIs available online.

## Files

- **README.md:** This file.
- **Copying.txt:** GNU General Public License version 3.
- **AUTHORS:** List of project authors.
- **src:** Source code and Makefile for compiling on Unix-like systems.
- **.nnue file:** Neural network for NNUE evaluation (binary distributions include this file).

## Contributing

See [Contributing Guide](CONTRIBUTING.md).

## Donating Hardware

Help improve Stockfish by donating hardware resources through [Fishtest](https://tests.stockfishchess.org/).

## Improving Code

Learn about Stockfish development basics in the [Chessprogramming Wiki](https://www.chessprogramming.org/Stockfish). Discussions mainly happen on the [Stockfish Discord server](https://discord.gg/j9emv3s).

## Compiling Stockfish

Compile directly from the source code with the included Makefile. Example for Intel and AMD chips:

```bash
cd src
make -j profile-build ARCH=x86-64-avx2
