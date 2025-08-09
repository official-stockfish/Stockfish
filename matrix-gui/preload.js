const { contextBridge, ipcRenderer } = require('electron');

// Expose protected methods that allow the renderer process to use
// the ipcRenderer without exposing the entire object
contextBridge.exposeInMainWorld('electronAPI', {
  // Engine communication
  startEngine: () => ipcRenderer.invoke('start-engine'),
  sendToEngine: (command) => ipcRenderer.invoke('send-to-engine', command),
  stopEngine: () => ipcRenderer.invoke('stop-engine'),
  
  // Engine output listeners
  onEngineOutput: (callback) => {
    ipcRenderer.on('engine-output', (event, data) => callback(data));
  },
  
  onEngineError: (callback) => {
    ipcRenderer.on('engine-error', (event, error) => callback(error));
  },
  
  // Remove listeners
  removeAllListeners: (channel) => {
    ipcRenderer.removeAllListeners(channel);
  }
});