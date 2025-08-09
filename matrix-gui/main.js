const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');
const { spawn } = require('child_process');

const isDev = process.env.NODE_ENV === 'development';

let mainWindow;
let stockfishProcess = null;

function createWindow() {
  // Create the browser window
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    minWidth: 1200,
    minHeight: 800,
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      preload: path.join(__dirname, 'preload.js')
    },
    icon: path.join(__dirname, 'assets/icon.png'), // Optional: add app icon
    titleBarStyle: 'default',
    backgroundColor: '#000000',
    show: false
  });

  // Load the app
  const startUrl = isDev 
    ? 'http://localhost:3000' 
    : `file://${path.join(__dirname, '../build/index.html')}`;
  
  mainWindow.loadURL(startUrl);

  // Show window when ready
  mainWindow.once('ready-to-show', () => {
    mainWindow.show();
  });

  // Open DevTools in development
  if (isDev) {
    mainWindow.webContents.openDevTools();
  }

  mainWindow.on('closed', () => {
    mainWindow = null;
    if (stockfishProcess) {
      stockfishProcess.kill();
    }
  });
}

// Stockfish engine communication
ipcMain.handle('start-engine', async () => {
  try {
    const enginePath = path.join(__dirname, 'engine', 'stockfish');
    stockfishProcess = spawn(enginePath);
    
    stockfishProcess.on('error', (error) => {
      console.error('Failed to start Stockfish:', error);
      mainWindow.webContents.send('engine-error', error.message);
    });

    return { success: true };
  } catch (error) {
    console.error('Engine start error:', error);
    return { success: false, error: error.message };
  }
});

ipcMain.handle('send-to-engine', async (event, command) => {
  if (stockfishProcess && !stockfishProcess.killed) {
    stockfishProcess.stdin.write(command + '\n');
    return { success: true };
  }
  return { success: false, error: 'Engine not running' };
});

ipcMain.handle('stop-engine', async () => {
  if (stockfishProcess && !stockfishProcess.killed) {
    stockfishProcess.kill();
    stockfishProcess = null;
    return { success: true };
  }
  return { success: false, error: 'Engine not running' };
});

// Set up engine output listener
function setupEngineOutput() {
  if (stockfishProcess) {
    stockfishProcess.stdout.on('data', (data) => {
      const output = data.toString();
      mainWindow.webContents.send('engine-output', output);
    });

    stockfishProcess.stderr.on('data', (data) => {
      const error = data.toString();
      console.error('Engine error:', error);
      mainWindow.webContents.send('engine-error', error);
    });
  }
}

// App event handlers
app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});

app.on('before-quit', () => {
  if (stockfishProcess && !stockfishProcess.killed) {
    stockfishProcess.kill();
  }
});