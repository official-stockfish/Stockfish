# Using the GUIs

## Matrix GUI (Cyberpunk / Electron)

1. Install Node.js 14+.
2. `cd matrix-gui && npm install`.
3. Place a working Stockfish binary in `matrix-gui/engine/` (see the engine README).
4. Run `npm run electron-dev` or the packaged build.
5. Click **Start Engine**, then play on the board or let the engine analyze.

Features: digital-rain background, neon highlights, live evaluation, best-move hints, move history.

## NexusChess (Avalonia)

1. Install .NET 8 SDK.
2. Build and run as shown in the Avalonia UI readme.
3. Point the “Engine Path” field at your Stockfish binary and start it.

Features: clean board, real-time UCI log, evaluation display, FEN, basic game controls.

## NeuralChess PWA

1. `cd neuralchess-pwa && npm install && npm run dev`.
2. Open the local URL in a modern browser (Chrome/Edge/Firefox recommended).
3. The app can be installed as a Progressive Web App and works offline once the WASM engine is cached.

Features: glassmorphism UI, responsive layout, Web Worker isolation, security-focused design.

## Common Tips

- Always use a recent Stockfish binary that matches your CPU (AVX2 / AVX512 / ARM / etc.).
- For analysis, higher Hash and Threads values improve strength (watch RAM usage).
- All GUIs speak standard UCI; any other UCI engine can theoretically be substituted.
