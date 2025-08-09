# Stockfish Matrix GUI

A cyberpunk-themed Electron + React GUI for the Stockfish chess engine, featuring a digital rain matrix effect and neon styling.

50,341 Files, 7,796 Folders

## Features

- **Cyberpunk/Matrix Theme**: Complete with digital rain background effect and neon styling
- **Chess Interface**: Interactive chessboard using chessboardjsx and chess.js
- **Engine Integration**: Real-time communication with Stockfish engine via UCI protocol
- **Analysis Panel**: Live display of engine output, evaluation, and best moves
- **Move History**: Track and review game progress
- **Responsive Design**: Works on different screen sizes

## Installation

### Prerequisites

- Node.js (14+ recommended)
- npm (included with Node.js)
- Stockfish chess engine binary

### Setup

1. **Install Dependencies**
   ```bash
   cd matrix-gui
   npm install
   ```

2. **Install Stockfish Engine**
   - Follow the instructions in `engine/README.md`
   - Download Stockfish binary from https://stockfishchess.org/download/
   - Place the binary in the `engine/` directory

### Running the Application

#### Development Mode (React only)
```bash
npm start
```
This starts the React development server at http://localhost:3000

#### Production Build
```bash
npm run build
```
Creates an optimized build in the `build/` directory

#### Electron App (Full GUI)
```bash
# Build and run with Electron
npm run electron-pack

# Or for development with hot reload
npm run electron-dev
```

## Usage

### Basic Operation

1. **Start the Application**: Run the Electron app or React development server
2. **Start Engine**: Click "Start Engine" in the Analysis Panel
3. **Play Chess**: Click and drag pieces on the board to make moves
4. **Analyze**: The engine will automatically analyze positions and show evaluations

### Engine Features

- **Real-time Analysis**: Engine continuously analyzes the current position
- **Best Move Hints**: Engine suggests the best move (highlighted in cyan)
- **Evaluation Display**: Shows position evaluation in centipawns or mate scores
- **Depth Control**: See how deep the engine is searching
- **Node Count**: Monitor engine performance

### Interface Controls

- **New Game**: Reset the board to starting position
- **Undo Move**: Take back the last move
- **Play Engine Move**: Execute the engine's suggested move
- **Start/Stop Engine**: Control engine operation
- **Clear Output**: Clear the engine analysis window

## Technology Stack

- **Frontend**: React 18, CSS3 with custom cyberpunk styling
- **Chess Logic**: chess.js for game rules and validation
- **Chess UI**: chessboardjsx for interactive board
- **Desktop App**: Electron for native application
- **Engine Communication**: Node.js child_process for UCI protocol

## File Structure

```
matrix-gui/
├── public/
│   └── index.html          # HTML template
├── src/
│   ├── components/
│   │   ├── Chessboard.jsx  # Chess board component
│   │   ├── AnalysisPanel.jsx # Engine interface
│   │   └── DigitalRain.jsx # Matrix background effect
│   ├── styles/
│   │   └── theme.css       # Cyberpunk styling
│   ├── App.jsx             # Main application component
│   └── index.js            # React entry point
├── main.js                 # Electron main process
├── preload.js              # Electron preload script
├── package.json            # Dependencies and scripts
└── engine/
    ├── README.md           # Engine installation guide
    └── stockfish*          # Stockfish binary (user-provided)
```

## Customization

### Styling
Edit `src/styles/theme.css` to customize the cyberpunk theme:
- Colors: Modify CSS variables in `:root`
- Animations: Adjust keyframe animations
- Layout: Change grid and flexbox properties

### Engine Settings
Modify engine parameters in `src/components/AnalysisPanel.jsx`:
- Search depth
- Time controls
- UCI options

## Troubleshooting

### Engine Not Starting
1. Verify Stockfish binary is in `engine/` directory
2. Check file permissions (executable on Linux/macOS)
3. Ensure correct binary for your operating system

### Build Issues
1. Clear node_modules: `rm -rf node_modules && npm install`
2. Update dependencies: `npm update`
3. Check Node.js version compatibility

### Performance
- Lower engine depth for faster analysis
- Adjust digital rain animation if CPU usage is high
- Use production build for better performance

## Development

### Adding Features
1. Components go in `src/components/`
2. Styling in `src/styles/theme.css`
3. Engine communication via Electron IPC

### Testing
```bash
npm test                    # Run React tests
npm run build              # Test production build
npm run electron-dev       # Test Electron integration
```

## License

This GUI is provided under the same license as Stockfish (GPL v3). The Stockfish engine itself is developed by the Stockfish team and is also licensed under GPL v3.

## Credits

- **Stockfish Engine**: https://stockfishchess.org/
- **chess.js**: Chess game logic library
- **chessboardjsx**: React chess board component
- **Matrix Theme**: Inspired by The Matrix movie series
