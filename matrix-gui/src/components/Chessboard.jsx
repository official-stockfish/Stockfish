import React, { useState, useEffect } from 'react';
import Chessboard from 'chessboardjsx';
import { Chess } from 'chess.js';

const ChessboardComponent = ({ onMove, onGameUpdate, engineBestMove }) => {
  const [game, setGame] = useState(new Chess());
  const [fen, setFen] = useState(game.fen());
  const [gameStatus, setGameStatus] = useState('');
  const [moveHistory, setMoveHistory] = useState([]);

  useEffect(() => {
    updateGameStatus();
  }, [game]);

  useEffect(() => {
    if (onGameUpdate) {
      onGameUpdate({
        fen: fen,
        turn: game.turn(),
        gameOver: game.isGameOver(),
        inCheck: game.isCheck(),
        status: gameStatus,
        history: moveHistory
      });
    }
  }, [fen, gameStatus, moveHistory, onGameUpdate]);

  const updateGameStatus = () => {
    let status = '';
    
    if (game.isGameOver()) {
      if (game.isCheckmate()) {
        status = `Game Over - ${game.turn() === 'w' ? 'Black' : 'White'} wins by checkmate`;
      } else if (game.isDraw()) {
        if (game.isStalemate()) {
          status = 'Game Over - Draw by stalemate';
        } else if (game.isThreefoldRepetition()) {
          status = 'Game Over - Draw by threefold repetition';
        } else if (game.isInsufficientMaterial()) {
          status = 'Game Over - Draw by insufficient material';
        } else {
          status = 'Game Over - Draw by 50-move rule';
        }
      }
    } else {
      if (game.isCheck()) {
        status = `${game.turn() === 'w' ? 'White' : 'Black'} is in check`;
      } else {
        status = `${game.turn() === 'w' ? 'White' : 'Black'} to move`;
      }
    }
    
    setGameStatus(status);
  };

  const onDrop = ({ sourceSquare, targetSquare }) => {
    try {
      // Attempt the move
      const move = game.move({
        from: sourceSquare,
        to: targetSquare,
        promotion: 'q' // Always promote to queen for simplicity
      });

      if (move === null) return false; // Invalid move

      const newGame = new Chess(game.fen());
      setGame(newGame);
      setFen(newGame.fen());
      setMoveHistory([...moveHistory, move]);

      // Notify parent component
      if (onMove) {
        onMove(move, newGame.fen());
      }

      return true;
    } catch (error) {
      console.error('Move error:', error);
      return false;
    }
  };

  const resetGame = () => {
    const newGame = new Chess();
    setGame(newGame);
    setFen(newGame.fen());
    setMoveHistory([]);
  };

  const undoMove = () => {
    if (moveHistory.length > 0) {
      const newGame = new Chess();
      const newHistory = moveHistory.slice(0, -1);
      
      // Replay all moves except the last one
      newHistory.forEach(move => {
        newGame.move({
          from: move.from,
          to: move.to,
          promotion: move.promotion
        });
      });
      
      setGame(newGame);
      setFen(newGame.fen());
      setMoveHistory(newHistory);
    }
  };

  const makeEngineMove = (moveString) => {
    try {
      const move = game.move(moveString);
      if (move) {
        const newGame = new Chess(game.fen());
        setGame(newGame);
        setFen(newGame.fen());
        setMoveHistory([...moveHistory, move]);
        
        if (onMove) {
          onMove(move, newGame.fen());
        }
      }
    } catch (error) {
      console.error('Engine move error:', error);
    }
  };

  // Custom board styling for cyberpunk theme
  const boardStyle = {
    borderRadius: '8px',
    boxShadow: '0 0 20px rgba(0, 255, 0, 0.3)'
  };

  const squareStyles = {};
  
  // Highlight the last move
  if (moveHistory.length > 0) {
    const lastMove = moveHistory[moveHistory.length - 1];
    squareStyles[lastMove.from] = { backgroundColor: 'rgba(0, 255, 0, 0.3)' };
    squareStyles[lastMove.to] = { backgroundColor: 'rgba(0, 255, 0, 0.5)' };
  }

  // Highlight engine's suggested move
  if (engineBestMove) {
    const from = engineBestMove.substring(0, 2);
    const to = engineBestMove.substring(2, 4);
    squareStyles[from] = { backgroundColor: 'rgba(0, 255, 255, 0.4)' };
    squareStyles[to] = { backgroundColor: 'rgba(0, 255, 255, 0.6)' };
  }

  return (
    <div className="chessboard-container">
      <div className="panel-header glitch" data-text="Chess Interface">
        Chess Interface
      </div>
      
      <div style={{ display: 'flex', justifyContent: 'center', marginBottom: '20px' }}>
        <Chessboard
          position={fen}
          onDrop={onDrop}
          width={500}
          boardStyle={boardStyle}
          squareStyles={squareStyles}
          lightSquareStyle={{ backgroundColor: '#f0d9b5' }}
          darkSquareStyle={{ backgroundColor: '#b58863' }}
        />
      </div>

      <div className="game-info">
        <div className="game-status">{gameStatus}</div>
        
        <div className="controls">
          <button className="btn primary" onClick={resetGame}>
            New Game
          </button>
          <button 
            className="btn" 
            onClick={undoMove}
            disabled={moveHistory.length === 0}
          >
            Undo Move
          </button>
          {engineBestMove && (
            <button 
              className="btn primary" 
              onClick={() => makeEngineMove(engineBestMove)}
            >
              Play Engine Move
            </button>
          )}
        </div>

        {moveHistory.length > 0 && (
          <div className="move-display">
            <div style={{ marginBottom: '10px', fontWeight: 'bold' }}>Move History:</div>
            <div style={{ maxHeight: '100px', overflowY: 'auto' }}>
              {moveHistory.map((move, index) => (
                <div key={index} style={{ margin: '2px 0' }}>
                  {Math.floor(index / 2) + 1}. {index % 2 === 0 ? move.san : `... ${move.san}`}
                </div>
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  );
};

export default ChessboardComponent;