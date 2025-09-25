// Import the WASM engine
importScripts('./absorb_chess_stockfish.js');

// Verify the module loaded correctly
console.log('📦 [WORKER] Script imported. AbsorbChessEngine available:', typeof AbsorbChessEngine !== 'undefined');

// console.log('🔧🔧🔧 ENGINE WORKER SCRIPT LOADED 🔧🔧🔧');

let engine = null;
let isInitialized = false;

// Initialize the engine
async function initializeEngine() {
    try {
        console.log('🚀 [WORKER] Starting chess engine initialization...');
        
        // Check if the module is available
        if (typeof AbsorbChessEngine === 'undefined') {
            console.error('❌ [WORKER] AbsorbChessEngine module not loaded');
            throw new Error('AbsorbChessEngine not available in worker');
        }
        
        console.log('✅ [WORKER] AbsorbChessEngine module found, creating instance...');
        
        // Configure WASM module with correct paths
        const wasmModule = await AbsorbChessEngine({
            locateFile: function(path) {
                console.log('🔍 [WORKER] Locating file:', path);
                if (path.endsWith('.wasm')) {
                    return './' + path;  // WASM file is in the same directory as the worker
                }
                return path;
            },
            onRuntimeInitialized: function() {
                console.log('✅ [WORKER] WASM runtime initialized');
            },
            print: function(text) {
                console.log('📝 [WORKER] WASM:', text);
            },
            printErr: function(text) {
                console.error('❌ [WORKER] WASM Error:', text);
            }
        });
        
        console.log('✅ [WORKER] WASM module loaded, creating engine instance...');
        
        // Debug: Check what's available in the WASM module
        console.log('🔍 [WORKER] WASM module keys:', Object.keys(wasmModule));
        console.log('🔍 [WORKER] WasmChessEngine available:', typeof wasmModule.WasmChessEngine);
        console.log('🔍 [WORKER] WasmChessEngine.length (constructor params):', wasmModule.WasmChessEngine.length);
        console.log('🔍 [WORKER] WasmChessEngine.prototype:', Object.getOwnPropertyNames(wasmModule.WasmChessEngine.prototype || {}));
        console.log('🔍 [WORKER] Checking for other engine classes...');
        
        // Check for alternative class names
        const possibleEngineClasses = ['WasmChessEngine', 'ChessEngine', 'Engine', 'AbsorbChessEngine'];
        for (const className of possibleEngineClasses) {
            console.log(`🔍 [WORKER] ${className}:`, typeof wasmModule[className]);
        }
        
        // Check if there are other functions we can call
        console.log('🔍 [WORKER] _uci_command available:', typeof wasmModule._uci_command);
        
        // Always use the actual constructor
        let engineInstance = null;
        try {
            console.log('⏳ [WORKER] Attempting: new WasmChessEngine()');
            engineInstance = new wasmModule.WasmChessEngine();
            console.log('✅ [WORKER] Success with new WasmChessEngine()');
        } catch (err) {
            console.error('❌ [WORKER] Error during WasmChessEngine construction:', err);
            postMessage({ type: 'initialized', success: false, error: 'Engine constructor failed: ' + err.message });
            throw err;
        }
        engine = engineInstance;
        console.log('✅ [WORKER] Engine instance created');
        // Optionally, call an initialize method if needed
        if (engine.initialize) {
            try {
                console.log('⏳ [WORKER] Awaiting engine.initialize()...');
                await engine.initialize();
                console.log('✅ [WORKER] engine.initialize() completed');
            } catch (err) {
                console.error('❌ [WORKER] Error during engine.initialize():', err);
                postMessage({ type: 'initialized', success: false, error: 'engine.initialize() failed: ' + err.message });
                throw err;
            }
        }
        isInitialized = true;
        console.log('✅ [WORKER] Chess engine initialized successfully');
        postMessage({ type: 'initialized', success: true });
    } catch (error) {
        console.error('❌ [WORKER] Engine initialization failed:', error);
        console.error('❌ [WORKER] Error stack:', error.stack);
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
            // console.log(`🔍 [WORKER] Converting piece at [${row},${col}]:`, {
            //     type: piece.type,
            //     abilities: piece.abilities,
            //     color: piece.color,
            //     hasMoved: piece.hasMoved,
            //     value: value,
            //     binary: value.toString(2).padStart(16, '0'),
            //     baseType: `PIECE_${piece.type.toUpperCase()}`,
            //     abilityFlags: piece.abilities ? piece.abilities.map(ability => `ABILITY_${ability.toUpperCase()}`) : []
            // });

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
    // console.log('🎯 [WORKER] Setting board state with turn:', isWhiteTurn);
    
    return engine.setBoardState(engineBoard, isWhiteTurn);
}

// Handle messages
self.onmessage = async function(e) {
    const { type, data, id } = e.data;
    // console.log('🎬🎬🎬 ENGINE WORKER RECEIVED MESSAGE 🎬🎬🎬:', { type, id, data });
    
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
                
                // Get current position evaluation
                let currentEval = 0;
                try {
                    currentEval = engine.getEvaluation ? engine.getEvaluation() : 0;
                } catch (e) {
                    // If getEvaluation doesn't exist, try other methods
                    try {
                        currentEval = engine.evaluate ? engine.evaluate() : 0;
                    } catch (e2) {
                        currentEval = 0;
                    }
                }
                
                console.log('⭐⭐⭐ ENGINE POSITION EVAL ⭐⭐⭐:', currentEval);
                
                const engineMove = engine.findBestMove(data.depth || 3, data.timeLimit || 5000);
                
                console.log('🎯🎯🎯 ENGINE BEST MOVE RESULT 🎯🎯🎯:', {
                    move: engineMove,
                    from: engineMove ? [engineMove.from_row, engineMove.from_col] : null,
                    to: engineMove ? [engineMove.to_row, engineMove.to_col] : null,
                    flags: engineMove ? engineMove.flags : null,
                    evaluation: engineMove ? engineMove.evaluation : null
                });
                
                let move = null;
                if (engineMove && engineMove.from_row !== undefined) {
                    const from = [engineMove.from_row, engineMove.from_col];
                    const to = [engineMove.to_row, engineMove.to_col];
                    const isInvalid = from[0] < 0 || from[1] < 0 || to[0] < 0 || to[1] < 0;
                    if (!isInvalid) {
                        move = {
                            from,
                            to,
                            flags: engineMove.flags || 0,
                            evaluation: engineMove.evaluation || 0
                        };
                    } else {
                        console.warn('[WORKER] Ignoring invalid engine move (-1,-1).');
                        move = null;
                    }
                }
                
                postMessage({ type: 'bestMove', id, data: move });
                break;
                
            case 'getLegalMoves':
                if (!isInitialized) {
                    postMessage({ type: 'error', id, error: 'Engine not initialized' });
                    return;
                }

                console.log('[DEBUG] Received board in worker:', data.board);
                console.log('[DEBUG] Received gameState in worker:', data.gameState);

                // Normalize gameState keys
                const normalizedGameState = { ...data.gameState };
                delete normalizedGameState.currentTurn;
                delete normalizedGameState.gameOver;

                console.log('[DEBUG] Normalized gameState:', normalizedGameState);

                const setBoardSuccess2 = setBoardState(data.board, normalizedGameState);
                if (!setBoardSuccess2) {
                    postMessage({ type: 'error', id, error: 'Failed to set board state' });
                    return;
                }

                const rawMoves = engine.getLegalMoves();
                console.log('[DEBUG] Raw moves from engine:', rawMoves);

                const convertedMoves = {};

                for (let i = 0; i < rawMoves.length; i++) {
                    const move = rawMoves[i];
                    const fromKey = move.from_row + ',' + move.from_col;

                    // Debug castling moves specifically
                    if (move.from_row === 7 && move.from_col === 4 && (move.to_col === 2 || move.to_col === 6)) {
                        console.log('[DEBUG] CASTLING MOVE DETECTED:', {
                            from: [move.from_row, move.from_col],
                            to: [move.to_row, move.to_col],
                            flags: move.flags,
                            flagsType: typeof move.flags,
                            expectedCastling: 49152
                        });
                    }

                    if (!convertedMoves[fromKey]) {
                        convertedMoves[fromKey] = [];
                    }

                    // Preserve move flags from engine
                    convertedMoves[fromKey].push({
                        to: [move.to_row, move.to_col],
                        flags: move.flags || 0
                    });
                }

                console.log('[DEBUG] Converted moves for frontend:', convertedMoves);

                postMessage({ type: 'legalMoves', id, data: convertedMoves });
                break;
                
            case 'getMoveWithFlags':
                if (!isInitialized) {
                    postMessage({ type: 'error', id, error: 'Engine not initialized' });
                    return;
                }
                
                const setBoardSuccess4 = setBoardState(data.board, data.gameState);
                if (!setBoardSuccess4) {
                    postMessage({ type: 'error', id, error: 'Failed to set board state' });
                    return;
                }
                
                // Get all legal moves and find the ones matching from/to
                const allMoves = engine.getLegalMoves();
                const matchingMoves = [];
                
                // Only show legal moves count without details
                console.log('📋 ENGINE LEGAL MOVES:', allMoves.length);
                
                for (let i = 0; i < allMoves.length; i++) {
                    const move = allMoves[i];
                    if (move.from_row === data.from[0] && 
                        move.from_col === data.from[1] && 
                        move.to_row === data.to[0] && 
                        move.to_col === data.to[1]) {
                        
                        const moveFlags = move.flags || 0;
                        // console.log('ENGINE MOVE MATCH:', {
                        //     from: [move.from_row, move.from_col],
                        //     to: [move.to_row, move.to_col],
                        //     flags: moveFlags
                        // });
                        
                        matchingMoves.push({
                            from: [move.from_row, move.from_col],
                            to: [move.to_row, move.to_col],
                            flags: moveFlags
                        });
                    }
                }
                
                // If multiple moves (promotion options), return all
                // If single move, return just that move
                const result = matchingMoves.length === 1 ? matchingMoves[0] : matchingMoves;
                postMessage({ type: 'moveWithFlags', id, data: result });
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