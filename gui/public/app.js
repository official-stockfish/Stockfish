const boardElement = document.getElementById('board');
const statusElement = document.getElementById('status');
const moveListElement = document.getElementById('move-list');
const game = new Chess();

let boardFlipped = false;
let draggedPiece = null;
let startSquare = null;
let pendingPremove = null;
let lastMove = null;

// Initialization
function initBoard() {
    boardElement.innerHTML = '';
    for (let i = 0; i < 64; i++) {
        const square = document.createElement('div');
        const row = Math.floor(i / 8);
        const col = i % 8;
        const isLight = (row + col) % 2 === 0;
        
        square.classList.add('square');
        square.classList.add(isLight ? 'light' : 'dark');
        square.dataset.index = i;
        square.dataset.pos = indexToPos(i);
        
        boardElement.appendChild(square);
    }
    renderPieces();
}

function indexToPos(index) {
    const files = 'abcdefgh';
    const ranks = '87654321';
    return files[index % 8] + ranks[Math.floor(index / 8)];
}

function posToIndex(pos) {
    const files = 'abcdefgh';
    const ranks = '87654321';
    return ranks.indexOf(pos[1]) * 8 + files.indexOf(pos[0]);
}

function renderPieces() {
    const squares = document.querySelectorAll('.square');
    const boardState = game.board();
    
    squares.forEach((square, i) => {
        square.innerHTML = '';
        square.classList.remove('last-move', 'premove', 'valid-target');
        
        // Highlight last move
        if (lastMove && (square.dataset.pos === lastMove.from || square.dataset.pos === lastMove.to)) {
            square.classList.add('last-move');
        }
        
        // Highlight premove
        if (pendingPremove && (square.dataset.pos === pendingPremove.from || square.dataset.pos === pendingPremove.to)) {
            square.classList.add('premove');
        }

        const row = Math.floor(i / 8);
        const col = i % 8;
        const piece = boardState[row][col];
        
        if (piece) {
            const pieceDiv = document.createElement('div');
            pieceDiv.classList.add('piece');
            pieceDiv.style.backgroundImage = `url(${getPieceImage(piece)})`;
            pieceDiv.dataset.type = piece.type;
            pieceDiv.dataset.color = piece.color;
            
            pieceDiv.addEventListener('mousedown', onDragStart);
            pieceDiv.addEventListener('touchstart', onDragStart, { passive: false });
            
            square.appendChild(pieceDiv);
        }
    });
}

function getPieceImage(piece) {
    const baseUrl = 'https://upload.wikimedia.org/wikipedia/commons/';
    const mapping = {
        'wp': '4/45/Chess_plt45.svg', 'wr': '7/72/Chess_rlt45.svg', 'wn': '7/70/Chess_nlt45.svg',
        'wb': 'b/b1/Chess_blt45.svg', 'wq': '1/15/Chess_qlt45.svg', 'wk': '4/42/Chess_klt45.svg',
        'bp': 'c/c7/Chess_pdt45.svg', 'br': 'f/ff/Chess_rdt45.svg', 'bn': 'e/ef/Chess_ndt45.svg',
        'bb': '9/98/Chess_bdt45.svg', 'bq': '4/47/Chess_qdt45.svg', 'bk': 'f/f0/Chess_kdt45.svg'
    };
    return `${baseUrl}${mapping[piece.color + piece.type]}`;
}

let socket;
let engineReady = false;

function connectEngine() {
    socket = new WebSocket(`ws://${window.location.host}`);
    
    socket.onopen = () => {
        console.log('Connected to engine bridge');
        sendUCI('uci');
    };
    
    socket.onmessage = (event) => {
        const msg = JSON.parse(event.data);
        if (msg.type === 'engine') {
            handleEngineOutput(msg.data);
        } else if (msg.type === 'error') {
            document.getElementById('engine-status').innerText = 'Engine: Not Found (Compile src)';
            document.getElementById('engine-status').style.color = '#ff4444';
        }
    };
}

function sendUCI(cmd) {
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ type: 'uci', data: cmd }));
    }
}

function handleEngineOutput(data) {
    console.log('Engine:', data);
    
    // Parse Evaluation
    if (data.includes('score cp')) {
        const parts = data.split(' ');
        const cpIndex = parts.indexOf('cp');
        const score = parseInt(parts[cpIndex + 1]) / 100;
        updateEvalDisplay(score);
    } else if (data.includes('score mate')) {
        const parts = data.split(' ');
        const mateIndex = parts.indexOf('mate');
        const mateIn = parts[mateIndex + 1];
        updateEvalDisplay(mateIn > 0 ? 100 : -100, true);
    }

    if (data.includes('uciok')) {
        sendUCI('isready');
    } else if (data.includes('readyok')) {
        engineReady = true;
        document.getElementById('engine-status').innerText = 'Engine: Ready';
    } else if (data.includes('bestmove')) {
        const parts = data.split(/\s+/);
        const bestMoveIndex = parts.indexOf('bestmove');
        const bestMove = parts[bestMoveIndex + 1];
        
        if (bestMove) {
            const move = game.move({
                from: bestMove.substring(0, 2),
                to: bestMove.substring(2, 4),
                promotion: 'q'
            });
            if (move) {
                lastMove = move;
                document.getElementById('engine-status').innerText = 'Engine: Idle';
                onMoveExecuted();
            }
        }
    }
}

function updateEvalDisplay(score, isMate = false) {
    const evalWhite = document.getElementById('eval-white');
    const evalScore = document.getElementById('eval-score');
    
    // Clamp score for display
    let displayScore = score;
    if (game.turn() === 'b') displayScore = -score; // Normalize to white perspective
    
    let percentage = 50 + (displayScore * 10);
    percentage = Math.max(5, Math.min(95, percentage));
    
    evalWhite.style.height = `${percentage}%`;
    evalScore.innerText = isMate ? `M${Math.abs(score)}` : displayScore.toFixed(1);
    evalScore.style.bottom = percentage > 50 ? '10px' : 'auto';
    evalScore.style.top = percentage > 50 ? 'auto' : '10px';
}

function renderCapturedPieces() {
    const blackContainer = document.getElementById('captured-black');
    const whiteContainer = document.getElementById('captured-white');
    blackContainer.innerHTML = '';
    whiteContainer.innerHTML = '';

    const initialPieces = {
        w: { p: 8, n: 2, b: 2, r: 2, q: 1 },
        b: { p: 8, n: 2, b: 2, r: 2, q: 1 }
    };

    const currentPieces = {
        w: { p: 0, n: 0, b: 0, r: 0, q: 0 },
        b: { p: 0, n: 0, b: 0, r: 0, q: 0 }
    };

    game.board().forEach(row => {
        row.forEach(piece => {
            if (piece) currentPieces[piece.color][piece.type]++;
        });
    });

    const pieceValues = { p: 1, n: 3, b: 3, r: 5, q: 9 };

    ['w', 'b'].forEach(color => {
        const container = color === 'w' ? blackContainer : whiteContainer;
        const opponentColor = color === 'w' ? 'b' : 'w';
        
        Object.keys(initialPieces[opponentColor]).forEach(type => {
            const missing = initialPieces[opponentColor][type] - currentPieces[opponentColor][type];
            for (let i = 0; i < missing; i++) {
                const img = document.createElement('div');
                img.classList.add('captured-piece');
                img.style.backgroundImage = `url(${getPieceImage({ color: opponentColor, type })})`;
                container.appendChild(img);
            }
        });
    });
}

// Updated Move Execution with Captured Pieces
function onMoveExecuted() {
    renderPieces();
    updateStatus();
    updateMoveHistory();
    renderCapturedPieces();
    
    // Check for premove
    if (pendingPremove && game.turn() === 'w') {
        const move = game.move(pendingPremove);
        if (move) {
            lastMove = move;
            pendingPremove = null;
            renderPieces();
            updateStatus();
            updateMoveHistory();
            renderCapturedPieces();
            setTimeout(makeEngineMove, 200);
        } else {
            pendingPremove = null;
            renderPieces();
        }
    }
}

function onDragStart(e) {
    e.preventDefault();
    const piece = e.target;
    const square = piece.parentElement;
    startSquare = square.dataset.pos;
    
    // Highlight valid moves
    const moves = game.moves({ square: startSquare, verbose: true });
    moves.forEach(m => {
        const targetSquare = document.querySelector(`.square[data-pos="${m.to}"]`);
        if (targetSquare) targetSquare.classList.add('valid-target');
    });

    draggedPiece = piece.cloneNode(true);
    draggedPiece.classList.add('dragging');
    document.body.appendChild(draggedPiece);
    
    piece.style.opacity = '0.4';
    
    moveAt(e.clientX || e.touches[0].clientX, e.clientY || e.touches[0].clientY);
    
    document.addEventListener('mousemove', onDragging);
    document.addEventListener('touchmove', onDragging, { passive: false });
    document.addEventListener('mouseup', onDragEnd);
    document.addEventListener('touchend', onDragEnd);
}

function moveAt(x, y) {
    draggedPiece.style.left = x - draggedPiece.offsetWidth / 2 + 'px';
    draggedPiece.style.top = y - draggedPiece.offsetHeight / 2 + 'px';
}

function onDragging(e) {
    moveAt(e.clientX || e.touches[0].clientX, e.clientY || e.touches[0].clientY);
}

function onDragEnd(e) {
    document.removeEventListener('mousemove', onDragging);
    document.removeEventListener('touchmove', onDragging);
    document.removeEventListener('mouseup', onDragEnd);
    document.removeEventListener('touchend', onDragEnd);
    
    // Remove valid target highlights
    document.querySelectorAll('.valid-target').forEach(s => s.classList.remove('valid-target'));

    const x = e.clientX || (e.changedTouches ? e.changedTouches[0].clientX : 0);
    const y = e.clientY || (e.changedTouches ? e.changedTouches[0].clientY : 0);
    
    draggedPiece.remove();
    draggedPiece = null;
    
    const targetElement = document.elementFromPoint(x, y);
    const targetSquare = targetElement?.closest('.square');
    
    if (targetSquare && targetSquare.dataset.pos !== startSquare) {
        handleMove(startSquare, targetSquare.dataset.pos);
    } else {
        renderPieces();
    }
}

function handleMove(from, to) {
    const moveData = { from, to, promotion: 'q' };
    const turn = game.turn();
    const userColor = 'w'; 

    if (turn === userColor) {
        const move = game.move(moveData);
        if (move) {
            lastMove = move;
            pendingPremove = null;
            onMoveExecuted();
            makeEngineMove();
        } else {
            renderPieces();
        }
    } else {
        // Premove logic
        pendingPremove = moveData;
        console.log('Premove queued:', pendingPremove);
        renderPieces();
    }
}

function makeEngineMove() {
    if (game.game_over()) return;
    
    document.getElementById('engine-status').innerText = 'Engine: Thinking...';
    
    const fen = game.fen();
    const depth = document.getElementById('depth-range').value;
    
    if (engineReady) {
        sendUCI(`position fen ${fen}`);
        sendUCI(`go depth ${depth}`);
    } else {
        // Fallback if engine not connected
        const moves = game.moves();
        const randomMove = moves[Math.floor(Math.random() * moves.length)];
        setTimeout(() => {
            game.move(randomMove);
            onMoveExecuted();
            document.getElementById('engine-status').innerText = 'Engine: Idle (Simulated)';
        }, 1000);
    }
}

function updateStatus() {
    let status = '';
    const turn = game.turn() === 'w' ? 'White' : 'Black';

    if (game.in_checkmate()) status = `Game Over: ${turn} is in checkmate.`;
    else if (game.in_draw()) status = 'Game Over: Draw';
    else {
        status = `${turn} to move`;
        if (game.in_check()) status += ' (Check!)';
    }
    statusElement.innerText = status;
}

function updateMoveHistory() {
    moveListElement.innerHTML = '';
    const history = game.history();
    for (let i = 0; i < history.length; i += 2) {
        const num = Math.floor(i / 2) + 1;
        moveListElement.innerHTML += `
            <div class="move-number">${num}.</div>
            <div class="move-item">${history[i]}</div>
            <div class="move-item">${history[i+1] || ''}</div>
        `;
    }
    moveListElement.scrollTop = moveListElement.scrollHeight;
}

// UI Controls
document.getElementById('new-game').addEventListener('click', () => {
    game.reset();
    lastMove = null;
    pendingPremove = null;
    initBoard();
    updateStatus();
    updateMoveHistory();
    renderCapturedPieces();
});

document.getElementById('undo-move').addEventListener('click', () => {
    game.undo();
    game.undo(); // Undo both player and engine
    renderPieces();
    updateStatus();
    updateMoveHistory();
    renderCapturedPieces();
});

// Initialize
initBoard();
updateStatus();
connectEngine();
renderCapturedPieces();
