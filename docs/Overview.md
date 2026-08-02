# Project Overview – GizzZmo/Stockfish Fork

This repository extends the official Stockfish chess engine with three independent graphical front-ends so users can immediately play and analyze without installing a third-party GUI.

## Goals

- Keep the engine source identical (or closely tracking) official Stockfish.
- Provide modern, attractive, and usable GUIs for different platforms and tastes.
- Serve as a development playground for UI experiments around the UCI protocol.

## Components

### Core Engine (`src/`)

Classic Stockfish C++ codebase with NNUE evaluation, multi-threaded search, Syzygy tablebase support, universal binaries, etc. Build with the provided Makefile.

### Matrix GUI (`matrix-gui/`)

- Electron + React + chess.js + chessboardjsx
- Full cyberpunk / Matrix digital-rain aesthetic
- Desktop application for Windows, macOS, Linux
- Real-time analysis panel, move history, neon styling

### NexusChess (`Avalonia UI/`)

- Avalonia UI + .NET 8 (C#)
- Clean Fluent-style interface
- Strong MVVM separation
- Primary target: Windows desktop; also runs on other platforms supported by Avalonia

### NeuralChess PWA (`neuralchess-pwa/`)

- Vue 3 + Vite + WebAssembly + Service Worker
- Glassmorphism design, responsive, installable as PWA
- Offline-capable analysis in the browser

## Relationship to Official Stockfish

This is a **fork**. Upstream improvements should be merged regularly. Functional changes to the engine itself should still follow the official Fishtest process if you intend to contribute them back.

The GUIs are original additions by the fork author.

## License

GPL-3.0 for the whole repository.
