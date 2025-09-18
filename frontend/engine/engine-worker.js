// Import the WASM engine
importScripts('./chess_engine.js');

let engine = null;
let isInitialized = false;

// Initialize the engine
async function initializeEngine() {
    try {
        console.log('🔧 [WORKER] Initializing chess engine...');
        
        if (typeof ChessEngineModule === 'undefined') {
            throw new Error('ChessEngineModule not available in worker');
        }
        
        // Configure WASM module with correct paths
        const wasmModule = await ChessEngineModule({
            locateFile: function(path) {
                if (path.endsWith('.wasm')) {
                    return './' + path;  // WASM file is in the same directory as the worker
                }
                return path;
            }
        });
        engine = new wasmModule.WasmChessEngine();
        
        isInitialized = true;
        console.log('✅ [WORKER] Chess engine initialized successfully');
        postMessage({ type: 'initialized', success: true });
    } catch (error) {
        console.error('❌ [WORKER] Engine initialization failed:', error);
        postMessage({ type: 'initialized', success: false, error: error.message });
    }
}

// Convert board for engine
function convertBoardToEngine(board) {
    const engineBoard = [];
    for (let row = 0; row < 8; row++) {
        const engineRow = [];
        for (let col = 0; col < 8; col++) {
            const piece = board[row][col];
            if (!piece) {
                engineRow.push(0);
                continue;
            }

            let value = 0;
            // Base piece type flags (bits 0-5)
            const PIECE_PAWN = 1, PIECE_KNIGHT = 2, PIECE_BISHOP = 4;
            const PIECE_ROOK = 8, PIECE_QUEEN = 16, PIECE_KING = 32;
            
            // Ability flags (bits 6-11) - matching C++ engine expectations
            const ABILITY_PAWN = 64, ABILITY_KNIGHT = 128, ABILITY_BISHOP = 256;
            const ABILITY_ROOK = 512, ABILITY_QUEEN = 1024, ABILITY_KING = 2048;

            // Set base piece type
            switch (piece.type) {
                case 'pawn': value |= PIECE_PAWN; break;
                case 'knight': value |= PIECE_KNIGHT; break;
                case 'bishop': value |= PIECE_BISHOP; break;
                case 'rook': value |= PIECE_ROOK; break;
                case 'queen': value |= PIECE_QUEEN; break;
                case 'king': value |= PIECE_KING; break;
            }

            // Add the base piece type to abilities (so the piece keeps its original movement)
            if (!piece.abilities) piece.abilities = [];
            if (!piece.abilities.includes(piece.type)) {
                piece.abilities.push(piece.type);
            }

            // Set abilities (including the base type)
            if (piece.abilities) {
                if (piece.abilities.includes('pawn')) value |= ABILITY_PAWN;
                if (piece.abilities.includes('knight')) value |= ABILITY_KNIGHT;
                if (piece.abilities.includes('bishop')) value |= ABILITY_BISHOP;
                if (piece.abilities.includes('rook')) value |= ABILITY_ROOK;
                if (piece.abilities.includes('queen')) value |= ABILITY_QUEEN;
                if (piece.abilities.includes('king')) value |= ABILITY_KING;
            }

            // Color and movement flags (bits 12-13)
            if (piece.color === 'white') value |= 8192; // IS_WHITE = 8192
            if (piece.hasMoved) value |= 4096; // HAS_MOVED = 4096

            // Debug log for all pieces to understand conversion
            console.log(`🔍 [WORKER] Converting piece at [${row},${col}]:`, {
                type: piece.type,
                abilities: piece.abilities,
                color: piece.color,
                hasMoved: piece.hasMoved,
                value: value,
                binary: value.toString(2).padStart(16, '0'),
                baseType: `PIECE_${piece.type.toUpperCase()}`,
                abilityFlags: piece.abilities ? piece.abilities.map(ability => `ABILITY_${ability.toUpperCase()}`) : []
            });

            engineRow.push(value);
        }
        engineBoard.push(engineRow);
    }
    return engineBoard;
}

function setBoardState(board, gameState) {
    const engineBoard = convertBoardToEngine(board);
    const isWhiteTurn = gameState.current_turn === 'white';
    
    // Debug log the conversion
    console.log('🎯 [WORKER] Setting board state with turn:', isWhiteTurn);
    
    return engine.setBoardState(engineBoard, isWhiteTurn);
}

// Handle messages
self.onmessage = async function(e) {
    const { type, data, id } = e.data;
    
    try {
        switch (type) {
            case 'initialize':
                await initializeEngine();
                break;
                
            case 'findBestMove':
                if (!isInitialized) {
                    postMessage({ type: 'error', id, error: 'Engine not initialized' });
                    return;
                }
                
                const setBoardSuccess = setBoardState(data.board, data.gameState);
                if (!setBoardSuccess) {
                    postMessage({ type: 'error', id, error: 'Failed to set board state' });
                    return;
                }
                
                const engineMove = engine.findBestMove(data.depth || 3, data.timeLimit || 5000);
                
                let move = null;
                if (engineMove && engineMove.from_row !== undefined) {
                    move = {
                        from: [engineMove.from_row, engineMove.from_col],
                        to: [engineMove.to_row, engineMove.to_col],
                        flags: engineMove.flags || 0,
                        evaluation: engineMove.evaluation || 0
                    };
                }
                
                postMessage({ type: 'bestMove', id, data: move });
                break;
                
            case 'getLegalMoves':
                if (!isInitialized) {
                    postMessage({ type: 'error', id, error: 'Engine not initialized' });
                    return;
                }
                
                const setBoardSuccess2 = setBoardState(data.board, data.gameState);
                if (!setBoardSuccess2) {
                    postMessage({ type: 'error', id, error: 'Failed to set board state' });
                    return;
                }
                
                const rawMoves = engine.getLegalMoves();
                const convertedMoves = {};
                
                for (let i = 0; i < rawMoves.length; i++) {
                    const move = rawMoves[i];
                    const fromKey = move.from_row + ',' + move.from_col;
                    
                    if (!convertedMoves[fromKey]) {
                        convertedMoves[fromKey] = [];
                    }
                    
                    convertedMoves[fromKey].push([move.to_row, move.to_col]);
                }
                
                postMessage({ type: 'legalMoves', id, data: convertedMoves });
                break;
                
            case 'isInCheck':
                if (!isInitialized) {
                    postMessage({ type: 'error', id, error: 'Engine not initialized' });
                    return;
                }
                
                const setBoardSuccess3 = setBoardState(data.board, data.gameState);
                if (!setBoardSuccess3) {
                    postMessage({ type: 'error', id, error: 'Failed to set board state' });
                    return;
                }
                
                const inCheck = engine.isInCheck();
                postMessage({ type: 'checkResult', id, data: inCheck });
                break;
                
            default:
                postMessage({ type: 'error', id, error: 'Unknown message type: ' + type });
        }
    } catch (error) {
        postMessage({ type: 'error', id, error: error.message });
    }
};