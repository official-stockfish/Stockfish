# NexusChess – Avalonia UI for Stockfish

**Greetings from Jon-Arve Constantine**

Modern, cross-platform desktop GUI for the Stockfish chess engine built with Avalonia UI and .NET 8.

## Status

**Feature-complete first version** – playable chessboard, full UCI communication, live evaluation, and analysis panel.

### Implemented Features

- Interactive 8×8 chessboard with Unicode piece rendering and coordinates
- Complete UCI protocol support (start/stop engine, `uci`, `isready`, `position`, `go`, etc.)
- Real-time engine output log, evaluation (centipawns / mate), best-move display
- FEN display and basic game-state tracking
- Clean MVVM architecture
- Fluent design look & feel

### Project Structure

```
Avalonia UI/
├── NexusChess.Core/          # Chess logic + UCI engine wrapper
├── NexusChess.ViewModels/    # MVVM ViewModels & commands
├── NexusChess.Desktop/       # Avalonia UI (MainWindow, board rendering)
└── NexusChess.sln
```

## Requirements

- .NET 8.0 SDK
- Stockfish executable (build from `../src` or download from stockfishchess.org)
- Windows recommended; Avalonia also supports macOS and Linux

## Build & Run

```bash
cd "Avalonia UI"
dotnet restore
dotnet build
dotnet run --project NexusChess.Desktop
```

1. Enter the full path to your `stockfish` (or `stockfish.exe`) binary.
2. Click **Start Engine**.
3. Use the board and analysis panel.

## Architecture

- **Model** (`NexusChess.Core`): `ChessGame`, `UciEngine`, piece/square/move types, FEN support.
- **ViewModel** (`NexusChess.ViewModels`): `MainWindowViewModel`, `RelayCommand`, data binding.
- **View** (`NexusChess.Desktop`): Avalonia XAML + custom board rendering, real-time log, controls.

Engine communication is asynchronous so the UI stays responsive.

## Future Enhancements

- Full legal-move generation & validation
- PGN import/export
- Move animation & better piece sets
- MultiPV, engine options UI, opening books, tablebases
- Native AOT publish for smaller binaries

## License

GPL-3.0 (same as Stockfish).

---

*Enjoy analyzing with Stockfish!*  
*Greetings from Jon-Arve Constantine*
