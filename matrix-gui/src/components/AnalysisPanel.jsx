import React, { useState, useEffect, useRef } from 'react';

const AnalysisPanel = ({ gameState, onBestMove }) => {
  const [engineStatus, setEngineStatus] = useState('offline');
  const [engineOutput, setEngineOutput] = useState('');
  const [bestMove, setBestMove] = useState('');
  const [evaluation, setEvaluation] = useState('');
  const [depth, setDepth] = useState(0);
  const [nodes, setNodes] = useState(0);
  const outputRef = useRef(null);

  useEffect(() => {
    // Set up engine communication
    if (window.electronAPI) {
      // Listen for engine output
      window.electronAPI.onEngineOutput((data) => {
        setEngineOutput(prev => prev + data);
        parseEngineOutput(data);
        scrollToBottom();
      });

      // Listen for engine errors
      window.electronAPI.onEngineError((error) => {
        setEngineOutput(prev => prev + `ERROR: ${error}\n`);
        scrollToBottom();
      });
    }

    return () => {
      if (window.electronAPI) {
        window.electronAPI.removeAllListeners('engine-output');
        window.electronAPI.removeAllListeners('engine-error');
      }
    };
  }, []);

  useEffect(() => {
    // Send position to engine when game state changes
    if (engineStatus === 'online' && gameState?.fen) {
      sendToEngine(`position fen ${gameState.fen}`);
      sendToEngine('go depth 15');
    }
  }, [gameState?.fen, engineStatus]);

  const scrollToBottom = () => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  };

  const parseEngineOutput = (output) => {
    const lines = output.split('\n');
    
    lines.forEach(line => {
      if (line.includes('bestmove')) {
        const move = line.split(' ')[1];
        setBestMove(move);
        if (onBestMove) {
          onBestMove(move);
        }
      }
      
      if (line.includes('info') && line.includes('pv')) {
        // Parse engine analysis info
        const parts = line.split(' ');
        
        // Extract depth
        const depthIndex = parts.indexOf('depth');
        if (depthIndex !== -1 && parts[depthIndex + 1]) {
          setDepth(parseInt(parts[depthIndex + 1]));
        }
        
        // Extract nodes
        const nodesIndex = parts.indexOf('nodes');
        if (nodesIndex !== -1 && parts[nodesIndex + 1]) {
          setNodes(parseInt(parts[nodesIndex + 1]));
        }
        
        // Extract evaluation
        const cpIndex = parts.indexOf('cp');
        const mateIndex = parts.indexOf('mate');
        
        if (cpIndex !== -1 && parts[cpIndex + 1]) {
          const centipawns = parseInt(parts[cpIndex + 1]);
          setEvaluation(`${(centipawns / 100).toFixed(2)}`);
        } else if (mateIndex !== -1 && parts[mateIndex + 1]) {
          const mateIn = parts[mateIndex + 1];
          setEvaluation(`M${mateIn}`);
        }
      }
    });
  };

  const startEngine = async () => {
    try {
      const result = await window.electronAPI.startEngine();
      if (result.success) {
        setEngineStatus('online');
        setEngineOutput('Engine started successfully\n');
        
        // Initialize engine
        sendToEngine('uci');
        sendToEngine('ucinewgame');
        sendToEngine('isready');
      } else {
        setEngineOutput(`Failed to start engine: ${result.error}\n`);
      }
    } catch (error) {
      setEngineOutput(`Error starting engine: ${error.message}\n`);
    }
  };

  const stopEngine = async () => {
    try {
      await window.electronAPI.stopEngine();
      setEngineStatus('offline');
      setEngineOutput(prev => prev + 'Engine stopped\n');
      setBestMove('');
      setEvaluation('');
      setDepth(0);
      setNodes(0);
    } catch (error) {
      setEngineOutput(prev => prev + `Error stopping engine: ${error.message}\n`);
    }
  };

  const sendToEngine = async (command) => {
    try {
      await window.electronAPI.sendToEngine(command);
      setEngineOutput(prev => prev + `> ${command}\n`);
    } catch (error) {
      setEngineOutput(prev => prev + `Error sending command: ${error.message}\n`);
    }
  };

  const clearOutput = () => {
    setEngineOutput('');
  };

  const analyzePosition = () => {
    if (engineStatus === 'online' && gameState?.fen) {
      sendToEngine(`position fen ${gameState.fen}`);
      sendToEngine('go depth 20');
    }
  };

  return (
    <div className="analysis-panel panel">
      <div className="panel-header glitch" data-text="Engine Analysis">
        Engine Analysis
      </div>

      <div className="engine-status">
        <div className={`status-indicator ${engineStatus === 'online' ? '' : 'offline'}`}></div>
        <span>Engine Status: {engineStatus.toUpperCase()}</span>
      </div>

      <div className="controls">
        {engineStatus === 'offline' ? (
          <button className="btn primary" onClick={startEngine}>
            Start Engine
          </button>
        ) : (
          <button className="btn danger" onClick={stopEngine}>
            Stop Engine
          </button>
        )}
        
        <button 
          className="btn" 
          onClick={analyzePosition}
          disabled={engineStatus === 'offline' || !gameState?.fen}
        >
          Analyze
        </button>
        
        <button className="btn" onClick={clearOutput}>
          Clear
        </button>
      </div>

      {engineStatus === 'online' && (
        <div className="analysis-info">
          <div className="analysis-stats">
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '10px', margin: '15px 0' }}>
              <div className="stat-item">
                <div style={{ color: '#00ffff', fontSize: '12px' }}>EVALUATION</div>
                <div style={{ fontSize: '18px', fontWeight: 'bold' }}>
                  {evaluation || '0.00'}
                </div>
              </div>
              <div className="stat-item">
                <div style={{ color: '#00ffff', fontSize: '12px' }}>DEPTH</div>
                <div style={{ fontSize: '18px', fontWeight: 'bold' }}>
                  {depth}
                </div>
              </div>
              <div className="stat-item">
                <div style={{ color: '#00ffff', fontSize: '12px' }}>BEST MOVE</div>
                <div style={{ fontSize: '16px', fontWeight: 'bold', color: '#ff0080' }}>
                  {bestMove || 'none'}
                </div>
              </div>
              <div className="stat-item">
                <div style={{ color: '#00ffff', fontSize: '12px' }}>NODES</div>
                <div style={{ fontSize: '14px' }}>
                  {nodes.toLocaleString()}
                </div>
              </div>
            </div>
          </div>
        </div>
      )}

      <div 
        ref={outputRef}
        className="engine-output"
        style={{ height: '300px' }}
      >
        {engineOutput || 'Engine output will appear here...'}
      </div>

      {gameState && (
        <div style={{ marginTop: '15px', fontSize: '12px', color: '#008000' }}>
          <div>Turn: {gameState.turn === 'w' ? 'White' : 'Black'}</div>
          <div>Status: {gameState.status}</div>
          {gameState.inCheck && <div style={{ color: '#ff0080' }}>IN CHECK</div>}
          {gameState.gameOver && <div style={{ color: '#ff0080' }}>GAME OVER</div>}
        </div>
      )}
    </div>
  );
};

export default AnalysisPanel;