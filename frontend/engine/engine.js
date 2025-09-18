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

        console.log('🤖 [ENGINE] Requesting best move from worker...');
        
        try {
            const move = await this._sendMessage('findBestMove', {
                board, gameState, depth, timeLimit
            });
            
            console.log('✅ [ENGINE] Received best move from worker:', move);
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

        console.log('📋 [ENGINE] Requesting legal moves from worker...');
        
        try {
            const moves = await this._sendMessage('getLegalMoves', {
                board, gameState
            });
            
            console.log('✅ [ENGINE] Received legal moves from worker');
            return moves;
        } catch (error) {
            console.error('❌ [ENGINE] Error getting legal moves:', error);
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
 * Apply a move to the board and handle absorption
 */
function applyMoveToBoard(board, move) {
    console.log('🎯 [MOVE] Applying move manually:', move);
    
    const { from, to } = move;
    const fromPiece = board[from[0]][from[1]];
    const toPiece = board[to[0]][to[1]];
    
    if (!fromPiece) {
        console.error('❌ [MOVE] No piece at source position');
        return { success: false, capturedPiece: null };
    }
    
    // Create a deep copy of the piece to avoid modifying the original
    const movingPiece = JSON.parse(JSON.stringify(fromPiece));
    movingPiece.hasMoved = true;
    
    // Handle absorption if there's a capture
    let capturedPiece = null;
    if (toPiece && toPiece.color !== movingPiece.color) {
        capturedPiece = toPiece;
        console.log('💥 [ABSORPTION] Capturing piece:', capturedPiece.type, 'with abilities:', capturedPiece.abilities);
        
        // ONLY add captured piece's BASE TYPE to abilities (not its inherited abilities)
        if (!movingPiece.abilities.includes(capturedPiece.type)) {
            movingPiece.abilities.push(capturedPiece.type);
            console.log('✨ [ABSORPTION] Gained ability:', capturedPiece.type, 'New abilities:', movingPiece.abilities);
        } else {
            console.log('ℹ️ [ABSORPTION] Already has ability:', capturedPiece.type);
        }
        
        // NO LONGER inheriting all abilities from captured piece
        // This was causing the bug where capturing a multi-ability piece gave too many abilities
    }
    
    // Apply the move
    board[to[0]][to[1]] = movingPiece;
    board[from[0]][from[1]] = null;
    
    console.log('✅ [MOVE] Move applied successfully');
    return { success: true, capturedPiece };
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
    module.exports = ChessEngine;
} else {
    window.ChessEngine = ChessEngine;
}

// Create global engine instance
const chessEngine = new ChessEngine();