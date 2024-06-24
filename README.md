<div align="center">

  [![Stockfish][stockfish-logo]][website-link]

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

- [About Stockfish](#about-stockfish)
  - [Key Features](#key-features)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Installation](#installation)
- [Usage](#usage)
- [Contributing](#contributing)
  - [Donating Hardware](#donating-hardware)
  - [Improving the Code](#improving-the-code)
- [Building from Source](#building-from-source)
- [License](#license)

## About Stockfish

[Stockfish][website-link] is a powerful, open-source chess engine renowned for its strength and versatility. Originally derived from Glaurung 2.1, Stockfish has evolved into one of the world's strongest chess engines, consistently ranking at the top of chess engine rating lists.

### Key Features

- Exceptionally strong chess analysis
- UCI (Universal Chess Interface) compatibility
- Cross-platform support (Windows, macOS, Linux, Android, iOS)
- Advanced NNUE (Efficiently Updatable Neural Network) evaluation
- Extensive configuration options for power users
- Active development with frequent updates

## Getting Started

### Prerequisites

Stockfish is a chess engine that requires a chess GUI (Graphical User Interface) to function as a complete chess program. Many compatible GUIs are available for various platforms.

### Installation

1. Download the latest version of Stockfish from the [official releases page][release-link].
2. Extract the files to your preferred location.
3. Configure your chosen chess GUI to use Stockfish as the engine.

For detailed installation instructions and GUI recommendations, please refer to our comprehensive [Download and Usage guide][wiki-usage-link] in the Stockfish Wiki.

## Usage

Stockfish uses the UCI protocol and is compatible with any UCI-compliant chess GUI. For command-line usage and advanced configuration options, please consult our [UCI commands documentation][wiki-uci-link].

## Contributing

We welcome contributions to Stockfish! Whether you're a developer, a chess enthusiast, or simply interested in improving the project, there are many ways to get involved.

### Donating Hardware

Stockfish development relies heavily on testing. You can contribute your hardware resources by:

1. Installing the [Fishtest Worker][worker-link]
2. Participating in current tests on [Fishtest][fishtest-link]

### Improving the Code

1. Familiarize yourself with chess programming concepts in the [Chess Programming Wiki][programming-link].
2. Study Stockfish-specific techniques in the [Stockfish section][programmingsf-link].
3. Review our [development guidelines][guideline-link].
4. Join discussions and ask questions in our [Discord server][discord-link].

For more details, see our [Contributing Guide](CONTRIBUTING.md).

## Building from Source

Stockfish can be compiled on various platforms and architectures. Here's a basic example for Unix-like systems:

```bash
cd src
make -j profile-build ARCH=x86-64-avx2
```

For comprehensive compilation instructions, including other platforms and architectures, please refer to our [compilation guide][wiki-compile-link].

## License

Stockfish is free, open-source software licensed under the [GNU General Public License v3][license-link]. You are free to use, modify, and distribute the software under the terms of this license.

---

For the latest updates and more information, visit our [official website][website-link] or join our [Discord community][discord-link].

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
[release-link]:       https://github.com/official-stockfish/Stockfish/releases/latest
[stockfish-logo]:     https://stockfishchess.org/images/logo/icon_128x128.png
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
