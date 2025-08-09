# Stockfish Engine Binary

This directory should contain the Stockfish chess engine binary for the Matrix GUI to function properly.

## Installation Instructions

### Option 1: Download Pre-compiled Binary

1. Go to the official Stockfish releases: https://stockfishchess.org/download/
2. Download the appropriate binary for your operating system:
   - **Windows**: Download `stockfish_15_win_x64_avx2.zip` or similar
   - **macOS**: Download `stockfish_15_mac.zip` or similar  
   - **Linux**: Download `stockfish_15_linux_x64_avx2.zip` or similar

3. Extract the downloaded archive
4. Copy the `stockfish` executable to this directory (`matrix-gui/engine/`)
5. Make sure the file is named exactly `stockfish` (without any extension on Linux/macOS)

### Option 2: Compile from Source

If you want to compile Stockfish from source:

1. Navigate to the main Stockfish source directory (parent of this matrix-gui folder)
2. Go to the `src` directory: `cd src`
3. Compile Stockfish: `make -j profile-build`
4. Copy the resulting `stockfish` binary to this directory

### Platform-Specific Notes

#### Windows
- The binary should be named `stockfish.exe`
- Make sure you have the required Visual C++ redistributables installed

#### macOS
- The binary should be named `stockfish` 
- You may need to allow the binary in Security & Privacy settings
- On Apple Silicon Macs, use the ARM64 version if available

#### Linux
- The binary should be named `stockfish`
- Make sure the binary has execute permissions: `chmod +x stockfish`
- Install any required dependencies (usually just standard C++ libraries)

### Verification

To verify the engine is working:

1. Open a terminal/command prompt in this directory
2. Run the engine: `./stockfish` (Linux/macOS) or `stockfish.exe` (Windows)
3. Type `uci` and press Enter
4. You should see output starting with `id name Stockfish ...`
5. Type `quit` to exit

### Troubleshooting

**Engine not found errors:**
- Make sure the binary is in this exact directory
- Check the filename matches your operating system
- Verify file permissions (executable on Linux/macOS)

**Permission denied errors:**
- On Linux/macOS: `chmod +x stockfish`
- On macOS: Check Security & Privacy settings

**Engine crashes:**
- Try a different build variant (AVX2, AVX, SSE, or basic)
- Check if your CPU supports the instruction set used in the binary

### Current Status

```
[ ] Stockfish binary installed
[ ] Binary permissions set correctly  
[ ] Engine tested and working
```

Place your Stockfish binary in this directory and check off the items above once complete.

---

**Note**: This GUI is designed to work with Stockfish 14+ and uses the UCI (Universal Chess Interface) protocol for communication.