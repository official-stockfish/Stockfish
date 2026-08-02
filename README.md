<div align="center">

  [![Stockfish](https://stockfishchess.org/images/logo/icon_128x128.png)](https://stockfishchess.org)

  <h3>Stockfish + Modern GUIs (GizzZmo Fork)</h3>

  A free and strong UCI chess engine **plus** three complete graphical front-ends.

  [Official Stockfish](https://stockfishchess.org) · [This Fork](https://github.com/GizzZmo/Stockfish) · [Issues](https://github.com/GizzZmo/Stockfish/issues)

</div>

## Overview

**Greetings from Jon-Arve Constantine**

This repository is a public fork of the official [Stockfish](https://github.com/official-stockfish/Stockfish) project.  
It contains:

- The complete, up-to-date Stockfish chess engine source (`src/`)
- **Three ready-to-use graphical user interfaces** designed for different platforms and aesthetics

Stockfish itself remains a pure UCI engine (no built-in GUI). The GUIs in this repo make it immediately usable.

### Included GUIs

| GUI | Technology | Theme / Style | Platform | Location |
|-----|------------|---------------|----------|----------|
| **Matrix GUI** | Electron + React | Cyberpunk / Matrix digital rain | Desktop (Win/macOS/Linux) | `matrix-gui/` |
| **NexusChess** | Avalonia + .NET 8 | Modern Fluent / clean | Desktop (primary Windows, cross-platform) | `Avalonia UI/` |
| **NeuralChess PWA** | Vue 3 + Vite + WebAssembly | Glassmorphism | Browser / installable PWA | `neuralchess-pwa/` |

## Quick Start

### 1. Build the Stockfish engine

```bash
cd src
make -j profile-build          # recommended for most x86-64 CPUs
# or: make help                # see all targets
```

The resulting binary is `src/stockfish` (or `stockfish.exe` on Windows).

### 2. Choose a GUI

#### Matrix GUI (cyberpunk desktop)
```bash
cd matrix-gui
npm install
# Place stockfish binary in matrix-gui/engine/ (see matrix-gui/engine/README.md)
npm run electron-dev           # development
# or npm run electron-pack     # production package
```

#### NexusChess (Avalonia)
```bash
cd "Avalonia UI"
dotnet restore
dotnet build
dotnet run --project NexusChess.Desktop
```
Then set the path to your Stockfish binary in the UI and click **Start Engine**.

#### NeuralChess PWA
```bash
cd neuralchess-pwa
npm install
npm run dev                    # http://localhost:5173
# Production: npm run build && npm run preview
```

Detailed instructions for each GUI live in their own `README.md`.

## Repository Structure

```
.
├── src/                     # Official Stockfish C++ engine (Makefile, NNUE, etc.)
├── matrix-gui/              # Electron + React cyberpunk GUI
├── Avalonia UI/             # NexusChess – Avalonia/.NET desktop GUI
├── neuralchess-pwa/         # Vue 3 Progressive Web App
├── scripts/                 # Build / universal binary helpers
├── tests/                   # Engine tests
├── docs/                    # Project documentation (this fork)
├── AUTHORS, Copying.txt     # Original Stockfish credits & GPL-3.0
├── CONTRIBUTING.md
└── README.md                # You are here
```

## Documentation

- [Project Overview & Architecture](docs/Overview.md)
- [Building the Engine](docs/Building.md)
- [Using the GUIs](docs/GUIs.md)
- [Contributing](CONTRIBUTING.md)
- Original Stockfish docs: https://github.com/official-stockfish/Stockfish/wiki

Wiki pages for this fork can be created from the `docs/` content (wiki is enabled).

## License

Everything inherits the **GNU General Public License v3.0** (see `Copying.txt`).  
You must distribute source code when you distribute binaries that include Stockfish.

## Acknowledgements

- The Stockfish team and the entire community behind the official engine and Fishtest.
- Leela Chess Zero for training data used by NNUE networks.
- Authors of chess.js, chessboardjsx, Avalonia, Vue, Electron, and all other open-source libraries used in the GUIs.

---

*A gift to chess enthusiasts everywhere.*  
*Greetings from Jon-Arve Constantine*
