# Building Stockfish and the GUIs

## Engine

```bash
cd src
make help                    # list all targets
make -j profile-build        # recommended for most modern x86-64 CPUs
```

Other useful targets (see official docs for details):

- `make build ARCH=x86-64-avx2`
- Universal / multi-arch builds via scripts in `scripts/` and `.github/workflows`

On Windows you can also use the Visual Studio solution or the GitHub Actions artifacts.

Place the resulting binary where the GUIs expect it (or configure the path inside each GUI).

## Matrix GUI

```bash
cd matrix-gui
npm install
# put stockfish binary into matrix-gui/engine/
npm run electron-dev
```

See `matrix-gui/README.md` and `matrix-gui/engine/README.md`.

## NexusChess (Avalonia)

```bash
cd "Avalonia UI"
dotnet restore
dotnet build
dotnet run --project NexusChess.Desktop
```

Requires .NET 8 SDK.

## NeuralChess PWA

```bash
cd neuralchess-pwa
npm install
npm run dev
# production: npm run build
```

WebAssembly Stockfish can be built with the scripts provided in that folder (or use a pre-built .wasm).

## Continuous Integration

This fork contains many GitHub Actions workflows under `.github/workflows/` (compilation matrices, sanitizers, performance, WASM, etc.). They are largely inherited/adapted from the official project plus additional GUI-related jobs.
