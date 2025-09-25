/**
 * Chess Engine Interface
 * Handles communication with WebAssembly chess engine through Web Worker
 */

class ChessEngine {
    constructor() {
        this.worker = null;
        this.isInitialized = false;
        this.pendingRequests = new Map();
        this.requestId = 0;
    }

    /**
     * Initialize the chess engine worker
     */
    async initialize() {
        if (this.isInitialized) return;

        try {
            console.log('🔧 [ENGINE] Initializing chess engine worker...');
            
            // Create worker from separate file instead of Blob URL
            this.worker = new Worker('engine/engine-worker.js');
            
            // Handle worker messages
            this.worker.onmessage = (event) => {
                this._handleWorkerMessage(event.data);
            };
            
            this.worker.onerror = (error) => {
                console.error('❌ [ENGINE] Worker error:', error);
            };
            
            // Initialize the worker
            await this._sendMessage('initialize');
            
            this.isInitialized = true;
            console.log('✅ [ENGINE] Chess engine worker initialized successfully');
            
        } catch (error) {
            console.error('❌ [ENGINE] Failed to initialize worker:', error);
            throw error;
        }
    }

    /**
     * Handle messages from the worker
     */
    _handleWorkerMessage(message) {
        // console.log('ENGINE WORKER -> MAIN:', message);
        const { type, id, data, error } = message;
        
        if (type === 'initialized') {
            const request = this.pendingRequests.get('init');
            if (request) {
                if (message.success) {
                    request.resolve(message);
                } else {
                    request.reject(new Error(message.error));
                }
                this.pendingRequests.delete('init');
            }
            return;
        }
        
        const request = this.pendingRequests.get(id);
        if (request) {
            if (type === 'error') {
                request.reject(new Error(error));
            } else {
                request.resolve(data);
            }
            this.pendingRequests.delete(id);
        }
    }

    /**
     * Send a message to the worker
     */
    _sendMessage(type, data = null) {
        // console.log('ENGINE MAIN -> WORKER:', { type, data });
        return new Promise((resolve, reject) => {
            const id = type === 'initialize' ? 'init' : ++this.requestId;
            
            this.pendingRequests.set(id, { resolve, reject });
            
            this.worker.postMessage({ type, id, data });
            
            // Timeout to prevent hanging
            setTimeout(() => {
                if (this.pendingRequests.has(id)) {
                    this.pendingRequests.delete(id);
                    reject(new Error(`Request timeout for ${type}`));
                }
            }, 30000);
        });
    }

    /**
     * Find the best move (now runs in worker)
     */
    async findBestMove(board, gameState, depth = 3, timeLimit = 5000) {
        if (!this.isInitialized) {
            await this.initialize();
        }

        // console.log('🤖 [ENGINE] Requesting best move from worker...');
        
        try {
            const move = await this._sendMessage('findBestMove', {
                board, gameState, depth, timeLimit
            });
            
            console.log('🎯 ENGINE BEST MOVE & EVAL:', {
                move: move ? `${move.from} -> ${move.to}` : null,
                evaluation: move ? move.evaluation : null,
                flags: move ? move.flags : null
            });
            return move;
        } catch (error) {
            console.error('❌ [ENGINE] Error finding best move:', error);
            return null;
        }
    }

    /**
     * Get legal moves (now runs in worker)
     */
    async getLegalMoves(board, gameState) {
        if (!this.isInitialized) {
            await this.initialize();
        }

        console.log('[DEBUG] Sending board to worker:', board);
        console.log('[DEBUG] Sending gameState to worker:', gameState);

        try {
            const moves = await this._sendMessage('getLegalMoves', {
                board, gameState
            });

            console.log('[DEBUG] Received legal moves from worker:', moves);
            return moves;
        } catch (error) {
            console.error('\u274c [ENGINE] Error getting legal moves:', error);
            return {};
        }
    }

    /**
     * Check if position is in check (now runs in worker)
     */
    async isInCheck(board, gameState) {
        if (!this.isInitialized) {
            await this.initialize();
        }

        console.log('👑 [ENGINE] Checking for check in worker...');
        
        try {
            const inCheck = await this._sendMessage('isInCheck', {
                board, gameState
            });
            
            console.log('✅ [ENGINE] Check result from worker:', inCheck);
            return inCheck;
        } catch (error) {
            console.error('❌ [ENGINE] Error checking for check:', error);
            return false;
        }
    }

    /**
     * Get complete move with flags for a specific from/to position
     */
    async getMoveWithFlags(board, gameState, from, to) {
        if (!this.isInitialized) {
            await this.initialize();
        }

        console.log('🎯 [ENGINE] Getting move with flags from worker...');
        
        try {
            const move = await this._sendMessage('getMoveWithFlags', {
                board, gameState, from, to
            });
            
            console.log('✅ [ENGINE] Received move with flags:', move);
            return move;
        } catch (error) {
            console.error('❌ [ENGINE] Error getting move with flags:', error);
            return null;
        }
    }

    /**
     * Terminate the worker
     */
    terminate() {
        if (this.worker) {
            this.worker.terminate();
            this.worker = null;
            this.isInitialized = false;
            this.pendingRequests.clear();
            console.log('🛑 [ENGINE] Worker terminated');
        }
    }
}

/**
 * MANUAL MOVE APPLICATION FUNCTIONS
 * Frontend applies moves directly to board state
 */

/**
 * Apply a move to the board and handle absorption - Unified for player and bot moves
 */
function applyMoveToBoard(board, move) {
    console.log('🎯 [MOVE] Applying move:', move);
    
    // Support both 'flag' and 'flags' for compatibility
    const { from, to } = move;
    const flags = (typeof move.flags !== 'undefined') ? move.flags : move.flag;
    const fromPiece = board[from[0]][from[1]];
    const toPiece = board[to[0]][to[1]];
    
    if (!fromPiece) {
        console.error('❌ [MOVE] No piece at source position:', from);
        return { success: false, capturedPiece: null };
    }
    
    // Create a deep copy of the piece to avoid modifying the original
    const movingPiece = JSON.parse(JSON.stringify(fromPiece));
    movingPiece.has_moved = true;
    
    // Define MoveType constants (matching C++ engine)
    const MoveType = {
        NORMAL: 0,
        PROMOTION: 16384,   // 1 << 14
        ENPASSANT: 32768,   // 2 << 14  
        CASTLING: 49152     // 3 << 14
    };

    let capturedPiece = null;
    
    console.log('🎯 [MOVE] Processing move - from:', from, 'to:', to, 'flags:', flags);
    
    // Handle special moves based on flags
    if (flags === MoveType.CASTLING) {
        // CRITICAL FIX: Handle Stockfish-style castling moves
        // Stockfish encodes castling as "king captures rook", but we need to convert
        // this to proper king and rook positions
        
        console.log('🎯 [CASTLING] Processing castling move - piece type:', fromPiece.type);
        
        if (fromPiece.type === 'king') {
            // The 'to' position is actually the rook's position in Stockfish encoding
            const rookCol = to[1]; // Where the rook currently is
            const kingRow = from[0];
            
            console.log('🎯 [CASTLING] King at:', from, 'attempting to castle with rook at col:', rookCol);
            
            // Determine castling type and final positions
            let kingFinalCol, rookFinalCol;
            
            if (rookCol === 7) {
                // Kingside castling - king goes to g-file (col 6), rook goes to f-file (col 5)
                kingFinalCol = 6;
                rookFinalCol = 5;
                console.log('🎯 [CASTLING] Kingside castling detected');
            } else if (rookCol === 0) {
                // Queenside castling - king goes to c-file (col 2), rook goes to d-file (col 3)
                kingFinalCol = 2;
                rookFinalCol = 3;
                console.log('🎯 [CASTLING] Queenside castling detected');
            } else {
                console.error('❌ [CASTLING] Invalid rook position for castling:', rookCol);
                return { success: false, capturedPiece: null };
            }
            
            // Get the rook
            const rook = board[kingRow][rookCol];
            if (!rook || rook.type !== 'rook' || rook.color !== fromPiece.color) {
                console.error('❌ [CASTLING] No valid rook found at position:', kingRow, rookCol);
                return { success: false, capturedPiece: null };
            }
            
            // Move the king to its final position
            board[kingRow][kingFinalCol] = movingPiece;
            board[from[0]][from[1]] = null;
            
            // Move the rook to its final position
            const rookCopy = JSON.parse(JSON.stringify(rook));
            rookCopy.has_moved = true;
            board[kingRow][rookFinalCol] = rookCopy;
            board[kingRow][rookCol] = null;
            
            console.log('✅ [CASTLING] Completed - King moved to:', [kingRow, kingFinalCol], 'Rook moved to:', [kingRow, rookFinalCol]);
            
            return { success: true, capturedPiece: null };
        } else {
            console.error('❌ [CASTLING] Castling flag on non-king piece');
            return { success: false, capturedPiece: null };
        }
    }
    else if (flags === MoveType.ENPASSANT) {
        // En passant capture
        console.log('🎯 [SPECIAL] En passant capture');
        const capturedRow = from[0]; // Same row as attacking pawn
        const capturedCol = to[1];   // Same column as destination
        capturedPiece = board[capturedRow][capturedCol];
        board[capturedRow][capturedCol] = null; // Remove the captured pawn
        console.log('💥 [EN PASSANT] Captured pawn at:', capturedRow, capturedCol);
    }
    else if (flags === MoveType.PROMOTION || flags >= 4 && flags <= 7) {
        // Promotion handling
        console.log('🎯 [SPECIAL] Promotion detected, flags:', flags);
        
        let promotionType;
        if (flags === MoveType.PROMOTION) {
            // Use global promotionPiece or default to queen
            promotionType = window.promotionPiece || 'queen';
        } else {
            // Legacy format
            const promotionPieces = { 4: 'queen', 5: 'rook', 6: 'bishop', 7: 'knight' };
            promotionType = promotionPieces[flags];
        }
        
        movingPiece.type = promotionType;
        movingPiece.abilities = [promotionType]; // Reset abilities to just the promoted piece type
        console.log('🎯 [PROMOTION] Pawn promoted to:', promotionType);
        
        // Handle capture if any
        if (toPiece && toPiece.color !== movingPiece.color) {
            capturedPiece = toPiece;
        }
    }
    else if (flags === 2 || flags === 3) {
        // Legacy castling format
        console.log('🎯 [SPECIAL] Legacy castling, flags:', flags);
        
        if (flags === 2) { // Kingside
            const rookFromCol = 7, rookToCol = 5;
            const rook = board[from[0]][rookFromCol];
            if (rook) {
                board[from[0]][rookFromCol] = null;
                const rookCopy = JSON.parse(JSON.stringify(rook));
                rookCopy.has_moved = true;
                board[from[0]][rookToCol] = rookCopy;
            }
        } else { // Queenside
            const rookFromCol = 0, rookToCol = 3;
            const rook = board[from[0]][rookFromCol];
            if (rook) {
                board[from[0]][rookFromCol] = null;
                const rookCopy = JSON.parse(JSON.stringify(rook));
                rookCopy.has_moved = true;
                board[from[0]][rookToCol] = rookCopy;
            }
        }
    }
    else {
        // Normal move - handle capture if any
        if (toPiece && toPiece.color !== movingPiece.color) {
            capturedPiece = toPiece;
        }
    }
    
    // For non-castling moves, apply the normal move
    if (flags !== MoveType.CASTLING) {
        // Handle absorption if there's a capture
        if (capturedPiece && capturedPiece.color !== movingPiece.color) {
            console.log('💥 [ABSORPTION] Capturing piece:', capturedPiece.type);
            
            // Add captured piece's base type to abilities
            if (!movingPiece.abilities) {
                movingPiece.abilities = [movingPiece.type];
            }
            if (!movingPiece.abilities.includes(capturedPiece.type)) {
                movingPiece.abilities.push(capturedPiece.type);
                console.log('✨ [ABSORPTION] Gained ability:', capturedPiece.type);
            }
        }
        
        // Apply the move
        board[to[0]][to[1]] = movingPiece;
        board[from[0]][from[1]] = null;
    }
    
    console.log('✅ [MOVE] Move applied successfully');
    return { success: true, capturedPiece };
}

/**
 * Apply a bot move (handles engine format directly)
 */
function applyBotMove(board, engineMove) {
    console.log('🤖 [BOT] Applying bot move:', engineMove);
    
    // Bot moves come in engine format, convert to standard format
    const move = {
        from: [engineMove.from_row, engineMove.from_col],
        to: [engineMove.to_row, engineMove.to_col],
        flags: engineMove.flags || 0
    };
    
    return applyMoveToBoard(board, move);
}

/**
 * Apply a player move (handles UI format)
 */
function applyPlayerMove(board, from, to, flags = 0) {
    console.log('👤 [PLAYER] Applying player move:', from, to, 'flags:', flags);
    
    const move = {
        from: Array.isArray(from) ? from : [from.row, from.col],
        to: Array.isArray(to) ? to : [to.row, to.col],
        flags: flags
    };
    
    return applyMoveToBoard(board, move);
}

/**
 * Check if a move is legal (basic validation)
 */
function isValidMove(board, gameState, move) {
    const { from, to } = move;
    
    // Basic bounds checking
    if (from[0] < 0 || from[0] > 7 || from[1] < 0 || from[1] > 7) return false;
    if (to[0] < 0 || to[0] > 7 || to[1] < 0 || to[1] > 7) return false;
    
    const piece = board[from[0]][from[1]];
    if (!piece) return false;
    
    // Check if it's the correct player's turn
    if (piece.color !== gameState.current_turn) return false;
    
    // Check if destination has own piece
    const destPiece = board[to[0]][to[1]];
    if (destPiece && destPiece.color === piece.color) return false;
    
    // For now, allow all other moves (engine will validate complex rules)
    return true;
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { ChessEngine, applyMoveToBoard, applyBotMove, applyPlayerMove, isValidMove };
} else {
    window.ChessEngine = ChessEngine;
    window.applyMoveToBoard = applyMoveToBoard;
    window.applyBotMove = applyBotMove;
    window.applyPlayerMove = applyPlayerMove;
    window.isValidMove = isValidMove;
}

// Create global engine instance
const chessEngine = new ChessEngine();