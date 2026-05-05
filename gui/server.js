const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const path = require('path');
const { spawn } = require('child_process');

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

const PORT = process.env.PORT || 3000;

// Serve static files
app.use(express.static(path.join(__dirname, 'public')));

wss.on('connection', (ws) => {
    console.log('Client connected');
    
    // Path to Stockfish binary - adjust if necessary
    const enginePath = path.join(__dirname, '../src/stockfish');
    let engine;

    try {
        engine = spawn(enginePath);
        console.log('Stockfish engine spawned');
        
        engine.stdout.on('data', (data) => {
            ws.send(JSON.stringify({ type: 'engine', data: data.toString() }));
        });

        engine.stderr.on('data', (data) => {
            console.error(`Engine Error: ${data}`);
        });

        engine.on('close', (code) => {
            console.log(`Engine process exited with code ${code}`);
        });
    } catch (err) {
        console.error('Failed to start engine:', err.message);
        ws.send(JSON.stringify({ type: 'error', data: 'Stockfish engine not found. Please compile it first.' }));
    }

    ws.on('message', (message) => {
        const msg = JSON.parse(message);
        if (msg.type === 'uci' && engine) {
            engine.stdin.write(msg.data + '\n');
        }
    });

    ws.on('close', () => {
        if (engine) engine.kill();
        console.log('Client disconnected');
    });
});

server.listen(PORT, () => {
    console.log(`Server running on http://localhost:${PORT}`);
});
