/**
 * Absorb Chess Stockfish Engine Integration
 * 
 * This module provides a high-level interface to the absorb chess Stockfish engine
 */

class AbsorbChessStockfish {
    constructor() {
        this.engine = null;
        this.isReady = false;
    }
    
    /**
     * Initialize the engine
     * @param {string} wasmPath - Path to the WASM files
     */
    async init(wasmPath = './') {
        try {
            // Load the WASM module
            const AbsorbChessEngineModule = await import(wasmPath + 'absorb_chess_stockfish.js');
            const engine = await AbsorbChessEngineModule.default();
            
            this.engine = new engine.AbsorbChessEngine();
            this.isReady = true;
            
            console.log('Absorb Chess Stockfish engine initialized');
            return true;
        } catch (error) {
            console.error('Failed to initialize engine:', error);
            return false;
        }
    }
    
    /**
     * Set position from FEN string
     */
    setPosition(fen) {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.setPosition(fen);
    }
    
    /**
     * Get current position as FEN
     */
    getFEN() {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.getFEN();
    }
    
    /**
     * Make a move
     */
    makeMove(move) {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.makeMove(move);
    }
    
    /**
     * Get all legal moves
     */
    getLegalMoves() {
        if (!this.isReady) throw new Error('Engine not initialized');
        const moves = this.engine.getLegalMoves();
        const result = [];
        for (let i = 0; i < moves.size(); i++) {
            result.push(moves.get(i));
        }
        return result;
    }
    
    /**
     * Get best move
     */
    getBestMove(depth = 5) {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.getBestMove(depth);
    }
    
    /**
     * Evaluate current position
     */
    evaluate() {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.evaluate();
    }
    
    /**
     * Check game state
     */
    isInCheck() {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.isInCheck();
    }
    
    isCheckmate() {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.isCheckmate();
    }
    
    isStalemate() {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.isStalemate();
    }
    
    /**
     * Get piece at square
     */
    getPieceAt(square) {
        if (!this.isReady) throw new Error('Engine not initialized');
        return this.engine.getPieceAt(square);
    }
    
    /**
     * Get abilities of piece at square (absorb chess specific)
     */
    getAbilitiesAt(square) {
        if (!this.isReady) throw new Error('Engine not initialized');
        const abilities = this.engine.getAbilitiesAt(square);
        const result = [];
        for (let i = 0; i < abilities.size(); i++) {
            result.push(abilities.get(i));
        }
        return result;
    }
    
    /**
     * Get complete board state
     */
    getBoardState() {
        if (!this.isReady) throw new Error('Engine not initialized');
        return JSON.parse(this.engine.getBoardState());
    }
    
    /**
     * Convert board to absorb chess format for frontend
     */
    getAbsorbChessBoard() {
        if (!this.isReady) throw new Error('Engine not initialized');
        
        const board = [];
        for (let row = 0; row < 8; row++) {
            board[row] = [];
            for (let col = 0; col < 8; col++) {
                const square = String.fromCharCode(97 + col) + (row + 1);
                const piece = this.getPieceAt(square);
                const abilities = this.getAbilitiesAt(square);
                
                if (piece) {
                    board[row][col] = {
                        piece: piece,
                        abilities: abilities,
                        square: square
                    };
                } else {
                    board[row][col] = null;
                }
            }
        }
        
        return {
            board: board,
            turn: this.getBoardState().turn,
            inCheck: this.isInCheck(),
            isCheckmate: this.isCheckmate(),
            isStalemate: this.isStalemate(),
            evaluation: this.evaluate()
        };
    }
}

// Export for use in web applications
if (typeof module !== 'undefined' && module.exports) {
    module.exports = AbsorbChessStockfish;
} else if (typeof window !== 'undefined') {
    window.AbsorbChessStockfish = AbsorbChessStockfish;
}

// Example usage:
/*
const engine = new AbsorbChessStockfish();
await engine.init('./wasm/');

// Set starting position
engine.setPosition('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1');

// Get legal moves
const moves = engine.getLegalMoves();
console.log('Legal moves:', moves);

// Make a move
engine.makeMove('e2e4');

// Get best response
const bestMove = engine.getBestMove(5);
console.log('Best move:', bestMove);

// Get board state with abilities
const boardState = engine.getAbsorbChessBoard();
console.log('Board state:', boardState);
*/