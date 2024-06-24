<div align="center">

  [![Stockfish][stockfish128-logo]][website-link]

  # Stockfish

  A free, powerful, and open-source UCI chess engine.
  
  **[Explore Stockfish Documentation »][wiki-link]**
  
  [Report Bug][issue-link] · [Open Discussion][discussions-link] · [Join Discord][discord-link] · [Read Blog][website-blog-link]

  [![Build][build-badge]][build-link]
  [![License][license-badge]][license-link]
  [![Release][release-badge]][release-link]
  [![Commits][commits-badge]][commits-link]
  [![Website][website-badge]][website-link]
  [![Fishtest][fishtest-badge]][fishtest-link]
  [![Discord][discord-badge]][discord-link]

</div>

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Installation](#installation)
- [Usage](#usage)
- [Contributing](#contributing)
  - [Donating Hardware](#donating-hardware)
  - [Improving the Code](#improving-the-code)
- [Compiling Stockfish](#compiling-stockfish)
- [License](#license)

## Overview

[Stockfish][website-link] is a powerful, open-source chess engine renowned for its strength and versatility. Derived from Glaurung 2.1, Stockfish has evolved into one of the world's strongest chess engines, consistently ranking at the top of chess engine rating lists.

## Key Features

- Extremely strong chess analysis
- UCI (Universal Chess Interface) compatible
- Cross-platform support (Windows, macOS, Linux, Android, iOS)
- NNUE (Efficiently Updatable Neural Network) evaluation
- Extensive configuration options for advanced users
- Active development and frequent updates

## Getting Started

### Prerequisites

Stockfish is a chess engine that requires a chess GUI (Graphical User Interface) to function as a complete chess program. Popular GUIs include:

- [Arena Chess GUI](http://www.playwitharena.de/) (Windows)
- [Cute Chess](https://github.com/cutechess/cutechess) (Cross-platform)
- [Scid vs. PC](http://scidvspc.sourceforge.net/) (Cross-platform)

### Installation

1. Download the latest version of Stockfish from the [official releases page][release-link].
2. Extract the files to a location on your computer.
3. Set up your chosen chess GUI to use Stockfish as the engine.

For detailed installation instructions, please refer to our [wiki][wiki-usage-link].

## Usage

Stockfish uses the UCI protocol and can be used with any UCI-compatible chess GUI. For command-line usage and advanced configuration options, please consult our [UCI commands documentation][wiki-uci-link].

## Contributing

We welcome contributions to Stockfish! Whether you're a developer, a chess enthusiast, or simply interested in improving the project, there are many ways to contribute.

### Donating Hardware

Stockfish development relies heavily on testing. You can contribute your hardware resources by:

1. Installing the [Fishtest Worker][worker-link]
2. Viewing and participating in current tests on [Fishtest][fishtest-link]

### Improving the Code

1. Familiarize yourself with chess programming concepts in the [Chess Programming Wiki][programming-link].
2. Read about Stockfish-specific techniques in the [Stockfish section][programmingsf-link].
3. Review our [development guidelines][guideline-link].
4. Join discussions and ask questions in our [Discord server][discord-link].

For more details, see our [Contributing Guide](CONTRIBUTING.md).

## Compiling Stockfish

Stockfish can be compiled on various platforms and architectures. Here's a basic example for Unix-like systems:

```bash
cd src
make -j profile-build ARCH=x86-64-avx2
```

For comprehensive compilation instructions, including other platforms and architectures, please refer to our [compilation guide][wiki-compile-link].

## License

Stockfish is free software licensed under the [GNU General Public License v3][license-link]. You are free to use, modify, and distribute the software under the terms of this license.

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

[build-badge]:        https://img.shields.io/github/actions/workflow/status/official-stockfish/Stockfish/stockfish.yml?branch=master&style=for-the-badge&label=stockfish&logo=github
[commits-badge]:      https://img.shields.io/github/commits-since/official-stockfish/Stockfish/latest?style=for-the-badge
[discord-badge]:      https://img.shields.io/discord/435943710472011776?style=for-the-badge&label=discord&logo=Discord
[fishtest-badge]:     https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=Fishtest&up_color=success&up_message=Online&url=https%3A%2F%2Ftests.stockfishchess.org%2Ftests%2Ffinished
[license-badge]:      https://img.shields.io/github/license/official-stockfish/Stockfish?style=for-the-badge&label=license&color=success
[release-badge]:      https://img.shields.io/github/v/release/official-stockfish/Stockfish?style=for-the-badge&label=official%20release
[website-badge]:      https://img.shields.io/website?style=for-the-badge&down_color=red&down_message=Offline&label=website&up_color=success&up_message=Online&url=https%3A%2F%2Fstockfishchess.org
