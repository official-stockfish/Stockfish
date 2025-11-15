<div align="center">

  <!-- logo -->
  <img src="https://stockfishchess.org/images/logo/icon_128x128.png" alt="Haisha Logo">

  <h3>Haisha (敗者)</h3>

  A free and strong UCI chess engine... for finding the worst possible move.
  <br>
  <strong>[Explore Haisha's descent into madness »][wiki-link]</strong>
  <br>
  <br>
  [Report a good move][issue-link]
  ·
  [Open a discussion][discussions-link]
  ·
  [Discord][discord-link]
  ·
  [Blog][website-blog-link]

</div>

## Overview

**Haisha** (敗者, Japanese for "Loser") is a **free and strong UCI chess engine** derived from
Stockfish. It analyzes chess positions to compute the most mathematically disastrous move.

Haisha is an exploration into the concept that being perfectly bad requires a near-perfect
understanding of chess. To find the worst move, the engine must evaluate all possibilities,
calculate long-term consequences, and understand complex concepts... just to violate them optimally.

Haisha **does not include a graphical user interface** (GUI). To use it, you will need
a UCI-compatible GUI.

## Files

This distribution of Haisha consists of the following files:

  * [README.md][readme-link], the file you are currently reading.

  * [Copying.txt][license-link], a text file containing the GNU General Public License version 3.

  * [AUTHORS][authors-link], a text file with the list of authors for the project.

  * [src][src-link], a subdirectory containing the full source code.

  * a file with the .nnue extension, storing the neural network used for evaluation.

the main and almost only changes made to stockfish for Haisha is in the `search.cpp` search algorithm file.
## Contributing

__See the [Contributing Guide](CONTRIBUTING.md).__

### Improving the code

Haisha is a fork of Stockfish. To understand the original architecture, the
[chessprogramming wiki][programming-link] is an invaluable resource.

Discussions about Haisha take place on the Haisha
[Discord server][discord-link]. This is the best place to ask questions
about the codebase and how to make it even worse.

## Compiling Haisha

Haisha has the same compilation requirements as Stockfish. On Unix-like systems,
it should be easy to compile directly from the source code. An example:

```
cd src
make -j profile-build
```

Detailed compilation instructions can be found in the original
[Stockfish documentation][wiki-compile-link].

## Terms of use

Haisha is free and distributed under the
[**GNU General Public License version 3**][license-link] (GPL v3). It is a
derivative work of Stockfish. This means you are free to do almost exactly what you
want with the program, including distributing it or using it as a starting point for
a project of your own.

The only real limitation is that whenever you distribute Haisha, you MUST
always include the license and the full source code (or a pointer to it). If you
make any changes to the source code, these changes must also be made available
under GPL v3.

## Acknowledgements

The original Stockfish engine uses neural networks trained on [data provided by the
Leela Chess Zero project][lc0-data-link]. Haisha benefits from this same data to
inform its (inverted) evaluation.

[authors-link]:       ./AUTHORS
[discord-link]:       https://discord.gg/[your-discord-link]
[issue-link]:         https://github.com/[your-username]/Haisha/issues/new
[discussions-link]:   https://github.com/[your-username]/Haisha/discussions/new
[license-link]:       ./Copying.txt
[programming-link]:   https://www.chessprogramming.org/Main_Page
[readme-link]:        ./README.md
[src-link]:           ./src
[website-link]:       https://[your-haisha-website.com]
[website-blog-link]:  https://[your-haisha-website.com]/blog
[wiki-link]:          https://github.com/[your-username]/Haisha/wiki
[wiki-compile-link]:  https://github.com/official-stockfish/Stockfish/wiki/Compiling-from-source
[lc0-data-link]:      https://storage.lczero.org/files/t
## Acknowledgements

Stockfish uses neural networks trained on [data provided by the Leela Chess Zero
project][lc0-data-link], which is made available under the [Open Database License][odbl-link] (ODbL).


[authors-link]:       https://github.com/official-stockfish/Stockfish/blob/master/AUTHORS
[build-link]:         https://github.com/official-stockfish/Stockfish/actions/workflows/stockfish.yml
[commits-link]:       https://github.com/official-stockfish/Stockfish/commits/master
[discord-link]:       https://discord.gg/GWDRS3kU6R
[issue-link]:         https://github.com/official-stockfish/Stockfish/issues/new?assignees=&labels=&template=BUG-REPORT.yml
[discussions-link]:   https://github.com/official-stockfish/Stockfish/discussions/new
[fishtest-link]:      https://tests.stockfishchess.org/tests
[guideline-link]:     https://github.com/official-stockfish/fishtest/wiki/Creating-my-first-test
[license-link]:       https://github.com/official-stockfish/Stockfish/blob/master/Copying.txt
[programming-link]:   https://www.chessprogramming.org/Main_Page
[programmingsf-link]: https://www.chessprogramming.org/Stockfish
[readme-link]:        https://github.com/official-stockfish/Stockfish/blob/master/README.md
[release-link]:       https://github.com/official-stockfish/Stockfish/releases/latest
[src-link]:           https://github.com/official-stockfish/Stockfish/tree/master/src
[stockfish128-logo]:  https://stockfishchess.org/images/logo/icon_128x128.png
[uci-link]:           https://backscattering.de/chess/uci/
[website-link]:       https://stockfishchess.org
[website-blog-link]:  https://stockfishchess.org/blog/
[wiki-link]:          https://github.com/official-stockfish/Stockfish/wiki
[wiki-compile-link]:  https://github.com/official-stockfish/Stockfish/wiki/Compiling-from-source
[wiki-uci-link]:      https://github.com/official-stockfish/Stockfish/wiki/UCI-&-Commands
[wiki-usage-link]:    https://github.com/official-stockfish/Stockfish/wiki/Download-and-usage
[worker-link]:        https://github.com/official-stockfish/fishtest/wiki/Running-the-worker
[lc0-data-link]:      https://storage.lczero.org/files/training_data
[odbl-link]:          https://opendatacommons.org/licenses/odbl/odbl-10.txt

[build-badge]:        https://img.shields.io/github/actions/workflow/status/official-stockfish/Stockfish/stockfish.yml?branch=master&style=for-the-badge&label=stockfish&logo=github
[commits-badge]:      https://img.shields.io/github/commits-since/official-stockfish/Stockfish/latest?style=for-the-badge
[discord-badge]:      https://img.shields.io/discord/435943710472011776?style=for-the-badge&label=discord&logo=Discord
[fishtest-badge]:     https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=Fishtest&up_color=success&up_message=Online&url=https%3A%2F%2Ftests.stockfishchess.org%2Ftests%2Ffinished
[license-badge]:      https://img.shields.io/github/license/official-stockfish/Stockfish?style=for-the-badge&label=license&color=success
[release-badge]:      https://img.shields.io/github/v/release/official-stockfish/Stockfish?style=for-the-badge&label=official%20release
[website-badge]:      https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=website&up_color=success&up_message=Online&url=https%3A%2F%2Fstockfishchess.org