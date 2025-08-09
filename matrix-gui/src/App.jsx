import React, { useState } from 'react';
import ChessboardComponent from './components/Chessboard';
import AnalysisPanel from './components/AnalysisPanel';
import DigitalRain from './components/DigitalRain';
import './styles/theme.css';

function App() {
  const [gameState, setGameState] = useState(null);
  const [engineBestMove, setEngineBestMove] = useState('');

  const handleMove = (move, fen) => {
    console.log('Move made:', move);
    // Game state will be updated via handleGameUpdate
  };

  const handleGameUpdate = (newGameState) => {
    setGameState(newGameState);
  };

  const handleBestMove = (move) => {
    setEngineBestMove(move);
  };

  return (
    <div className="app">
      <DigitalRain />
      
      <div className="main-content">
        <div className="game-area">
          <div style={{ textAlign: 'center', marginBottom: '20px' }}>
            <h1 
              className="panel-header glitch" 
              data-text="STOCKFISH MATRIX"
              style={{ 
                fontSize: '2.5rem', 
                margin: '0', 
                color: '#00ff00',
                textShadow: '0 0 20px #00ff00'
              }}
            >
              STOCKFISH MATRIX
            </h1>
            <div style={{ 
              color: '#00ffff', 
              fontSize: '1rem', 
              marginTop: '10px',
              textShadow: '0 0 10px #00ffff' 
            }}>
              Cyberpunk Chess Interface
            </div>
          </div>

          <ChessboardComponent
            onMove={handleMove}
            onGameUpdate={handleGameUpdate}
            engineBestMove={engineBestMove}
          />
        </div>

        <div className="analysis-area">
          <AnalysisPanel
            gameState={gameState}
            onBestMove={handleBestMove}
          />
        </div>
      </div>
    </div>
  );
}

export default App;