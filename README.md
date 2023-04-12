<div align="center">

  [![Stockfish][stockfish128-logo]][website-link]

  <h3>Stockfish</h3>

  A free and powerful UCI chess engine.
  <br>
  <strong>[Explore Stockfish docs »][wiki-link]</strong>
  <br>
  <br>
  [Report bug][issue-link]
  ·
  [Open a discussion][discussions-link]
  ·
  [Discord][discord-link]
  ·
  [Blog][website-blog-link]

  [![Build][build-badge]][build-link]
  [![License][license-badge]][license-link]
  <br>
  [![Release][release-badge]][release-link]
  [![Commits][commits-badge]][commits-link]
  <br>
  [![Website][website-badge]][website-link]
  [![Fishtest][fishtest-badge]][fishtest-link]
  [![Discord][discord-badge]][discord-link]

</div>

## Overview

[Stockfish][website-link] is a **free UCI chess engine**, derived from
the Glaurung 2.1 chess engine, to analyze chess positions and to also
compute optimal chess moves with it.

Stockfish **does not include** a graphical user interface (GUI), which
are required to display a chessboard and to input chess moves. They are
developed independently from Stockfish and are available on different
websites. **Read the documentation for your GUI of choice** to get basic
information about how to use Stockfish with it.

Also, see the [Stockfish documentation][wiki-usage-link] for more
information about how to correctly use the chess engine.

## Files

This distribution of Stockfish consists of the following files:

  * [README.md][readme-link], the file you are currently reading.

  * [Copying.txt][license-link], a text file containing the GNU General
    Public License version 3.

  * [AUTHORS][authors-link], a text file with the list of all Stockfish
    project authors.

  * [src][src-link], a subdirectory containing the whole source code,
    including a `Makefile` that can be used to compile Stockfish on
    Unix-like systems.

  * A file with the `.nnue` extension, storing the neural network for
    NNUE evaluation. Binary distributions have this file embedded.

## The UCI protocol

The [Universal Chess Interface][uci-link] (UCI) is a standard text-based
protocol used to communicate with a chess engine and is the recommended
way to do so for standard graphical user interfaces (GUIs) and also for
chess tools. Stockfish implements the majority of UCI options.

Developers can see the defaults of all the available UCI options in
Stockfish by typing `./stockfish uci` in a Unix-like terminal or `uci`
in a Windows terminal, but most users should typically use a chess GUI
to interact with Stockfish.

For more information about the UCI protocol or the `debug` commands, see
our [documentation][wiki-commands-link].

## Compiling Stockfish

Stockfish has support for 32-bit and 64-bit CPUs, certain hardware
instructions, big-endian machines such as PowerPC, and other platforms.

On Unix-like systems, it should be easy to compile Stockfish directly
from the source code with the included `Makefile` in the `src` folder.
In general, it is recommended to run `make help` to see a list of `make`
targets with corresponding descriptions.

```
cd src
make -j build ARCH=x86-64-modern
```

Detailed compilation instructions for all platforms can be found in our
[documentation][wiki-compile-link].

## Contributing

### Donating hardware

Improving Stockfish requires a massive amount of testing. You can donate
your hardware resources by installing a [Fishtest worker][worker-link]
and view the current tests on [Fishtest][fishtest-link].

### Improving the code

In the [chessprogramming wiki][programming-link], many techniques used
in Stockfish are explained with a lot of background information. The
[section on Stockfish][programmingsf-link] describes many features and
techniques used. However, the information is generic rather than focused
on Stockfish's precise implementation.

Chess engine testing is done on [Fishtest][fishtest-link]. If you want
to help with improving Stockfish, read this [guideline][guideline-link]
first, as here the basics of Stockfish development are explained.

Nowadays, discussions about Stockfish take place mainly on the project's
[Discord server][discord-link]. This is the best place to ask questions
about the codebase, how to improve it, etc.

## Terms of use

Stockfish is free, distributed under the [**GNU General Public License
version 3**][license-link] ("GNU GPLv3"). Essentially, this means that
you are free to do almost exactly what you want with the software, like
distributing it among your friends, making it available for download
from your website, selling it either by itself or as part of a bigger
software package, or using it as a starting point in your software
project.

The only limitation is that whenever you distribute Stockfish in some
way, you **must** include GNU GPLv3 and the whole source code (or a
pointer to where the source code can be found) to generate the exact
binary you are distributing. If you make any changes to the source code,
these changes must also be made available under GNU GPLv3.

[authors-link]:       https://github.com/official-stockfish/Stockfish/blob/master/AUTHORS
[build-link]:         https://github.com/official-stockfish/Stockfish/actions/workflows/stockfish.yml
[commits-link]:       https://github.com/official-stockfish/Stockfish/commits/master
[discord-link]:       https://discord.gg/GWDRS3kU6R
[issue-link]:         https://github.com/official-stockfish/Stockfish/issues/new?assignees=&labels=&template=BUG-REPORT.yml
[discussions-link]:   https://github.com/official-stockfish/Stockfish/discussions/new
[fishtest-link]:      https://tests.stockfishchess.org/tests
[guideline-link]:     https://github.com/glinscott/fishtest/wiki/Creating-my-first-test
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
[wiki-usage-link]:    https://github.com/official-stockfish/Stockfish/wiki/Download-and-usage
[wiki-compile-link]:  https://github.com/official-stockfish/Stockfish/wiki/Compiling-from-source
[wiki-commands-link]: https://github.com/official-stockfish/Stockfish/wiki/Commands
[worker-link]:        https://github.com/glinscott/fishtest/wiki/Running-the-worker

[build-badge]:        https://img.shields.io/github/actions/workflow/status/official-stockfish/Stockfish/stockfish.yml?branch=master&style=for-the-badge&label=stockfish&logo=github
[commits-badge]:      https://img.shields.io/github/commits-since/official-stockfish/Stockfish/latest?style=for-the-badge
[discord-badge]:      https://img.shields.io/discord/435943710472011776?style=for-the-badge&label=discord&logo=Discord
[fishtest-badge]:     https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=Fishtest&up_color=success&up_message=Online&url=https%3A%2F%2Ftests.stockfishchess.org%2Ftests%2Ffinished
[license-badge]:      https://img.shields.io/github/license/official-stockfish/Stockfish?style=for-the-badge&label=license&color=success
[release-badge]:      https://img.shields.io/github/v/release/official-stockfish/Stockfish?style=for-the-badge&label=official%20release
[website-badge]:      https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=website&up_color=success&up_message=Online&url=https%3A%2F%2Fstockfishchess.org
