/**
 * Chess Bot Controller
 * Manages bot gameplay, UI updates, and player interactions
 */

class ChessBot {
    constructor(app) {
        this.app = app;  // Reference to main app instance
        this.engine = chessEngine; // Use global engine instance
        this.isInitialized = false;
        this.botColor = 'black';  // Default bot plays as black
        this.playerColor = 'white';
        this.difficulty = 5;  // Reduce search depth
        this.timeLimit = 5000;  // 5 second per move
        this.isThinking = false;
        this.gameActive = false;
    }

    /**
     * Initialize the bot engine
     */
    async initialize() {
        if (this.isInitialized) return true;

        try {
            console.log('🤖 Initializing Chess Bot...');
            console.log('🔍 [BOT] Engine object exists:', !!this.engine);
            await this.engine.initialize();
            this.isInitialized = true;
            console.log('✅ Chess Bot ready!');
            console.log('🔍 [BOT] Engine after init:', {
                isInitialized: this.engine.isInitialized,
                wasmModule: !!this.engine.wasmModule,
                engine: !!this.engine.engine
            });
            return true;
        } catch (error) {
            console.error('❌ Bot initialization failed:', error);
            this.showError('Failed to initialize chess engine. Please refresh the page.');
            return false;
        }
    }

    /**
     * Start a new game against the bot
     */
    async startGame(playerColor = 'white', difficulty = 4) {
        if (!await this.initialize()) return false;

        this.playerColor = playerColor;
        this.botColor = playerColor === 'white' ? 'black' : 'white';
        this.difficulty = difficulty;
        this.gameActive = true;
        this.isThinking = false;

        console.log(`🎮 Starting bot game - Player: ${this.playerColor}, Bot: ${this.botColor}, Difficulty: ${this.difficulty}`);
        console.log('🔍 [BOT] Engine status after initialization:', {
            engineExists: !!this.engine,
            isInitialized: this.engine?.isInitialized,
            wasmModule: !!this.engine?.wasmModule
        });

        // Initialize game state using app's existing game setup
        this.app.initializeBotGame(this.playerColor);
        
        // Update UI
        this.updateGameUI();
        
        // If bot plays white, make first move
        if (this.botColor === 'white') {
            setTimeout(() => this.makeBotMove(), 1000);
        }

        return true;
    }

    /**
     * Handle player move and trigger bot response
     */
    async onPlayerMove(move) {
        if (!this.gameActive || this.isThinking) return;

        console.log(`👤 Player move: [${move.from}] -> [${move.to}]`);
        console.log('🔍 [BOT] onPlayerMove - current_turn at start:', this.app.gameState.current_turn);

        // Check if game ended
        if (this.checkGameEnd()) {
            this.gameActive = false;
            return;
        }

        console.log('🔍 [BOT] onPlayerMove - current_turn after checkGameEnd:', this.app.gameState.current_turn);

        // If it's bot's turn, make bot move
        console.log('🔍 [BOT] Checking if bot should move:', {
            currentTurn: this.app.gameState.current_turn,
            current_turn: this.app.gameState.current_turn,
            botColor: this.botColor
        });
        
        if (this.app.gameState.current_turn === this.botColor) {
            console.log('✅ [BOT] Bot should move, setting timeout');
            setTimeout(() => this.makeBotMove(), 500);
        } else {
            console.log('❌ [BOT] Not bot turn, not moving');
        }
    }

    /**
     * Make bot move
     */
    async makeBotMove() {
        if (!this.gameActive || this.isThinking || this.app.gameState.current_turn !== this.botColor) {
            console.log('🚫 [BOT] Not making move:', {
                gameActive: this.gameActive,
                isThinking: this.isThinking,
                currentTurn: this.app.gameState?.current_turn,
                botColor: this.botColor
            });
            return;
        }

        this.isThinking = true;
        this.showBotThinking(true);

        try {
            console.log(`🤖 Bot (${this.botColor}) thinking in worker...`);
            console.log('🔍 [BOT] Engine worker status:', {
                engineExists: !!this.engine,
                isInitialized: this.engine?.isInitialized,
                workerExists: !!this.engine?.worker
            });
            console.log('📤 [BOT] Sending to worker:', {
                board: this.app.gameState?.board,
                gameState: this.app.gameState,
                difficulty: this.difficulty,
                timeLimit: this.timeLimit
            });

            // Get best move from engine worker (non-blocking)
            const move = await this.engine.findBestMove(
                this.app.gameState?.board,
                this.app.gameState,
                this.difficulty,
                this.timeLimit
            );

            console.log('📥 [BOT] Received from engine:', move);
            
            // Log evaluation prominently
            if (move && move.evaluation !== undefined) {
                console.log('🎯 BOT MOVE EVALUATION:', move.evaluation);
            }

            if (!move) {
                console.error('❌ Bot could not find a move');
                this.showError('Bot encountered an error. Game ended.');
                this.gameActive = false;
                return;
            }

            // Show evaluation
            this.showBotEvaluation(move.evaluation);

            // Apply the move using app's existing move logic
            const botMove = {
                from: move.from,
                to: move.to,
                promotionPiece: move.promotionPiece
            };

            console.log(`🤖 Bot plays: [${botMove.from.join(',')}] -> [${botMove.to.join(',')}]${botMove.promotionPiece ? ` (=${botMove.promotionPiece})` : ''}`);

            // Use app's move application logic
            await this.app.applyBotMove(botMove);

            // Check for game end
            if (this.checkGameEnd()) {
                this.gameActive = false;
            }

        } catch (error) {
            console.error('❌ Bot move error:', error);
            this.showError('Bot encountered an error making a move.');
            this.gameActive = false;
        } finally {
            this.isThinking = false;
            this.showBotThinking(false);
        }
    }

    /**
     * Check if game has ended
     */
    checkGameEnd() {
        // Use app's existing game end detection
        if (this.app.gameState.gameOver) {
            const winner = this.app.gameState.winner;
            let message = '';
            
            if (winner === this.playerColor) {
                message = '🎉 Congratulations! You won!';
            } else if (winner === this.botColor) {
                message = '🤖 Bot wins! Better luck next time.';
            } else {
                message = '🤝 Game ended in a draw.';
            }
            
            this.showGameResult(message);
            return true;
        }
        return false;
    }

    /**
     * Update game UI for bot game
     */
    updateGameUI() {
        // Update player indicators
        const whiteStatus = document.getElementById('white-player-status');
        const blackStatus = document.getElementById('black-player-status');
        
        if (whiteStatus && blackStatus) {
            if (this.playerColor === 'white') {
                whiteStatus.textContent = 'You';
                blackStatus.textContent = '🤖 Bot';
            } else {
                whiteStatus.textContent = '🤖 Bot';
                blackStatus.textContent = 'You';
            }
        }

        // Hide multiplayer-specific UI elements
        const resignBtn = document.getElementById('resign-btn');
        const drawBtn = document.getElementById('offer-draw-btn');
        
        if (resignBtn) resignBtn.style.display = 'none';
        if (drawBtn) drawBtn.style.display = 'none';

        // Show bot-specific controls
        this.createBotControls();
    }

    /**
     * Create bot-specific UI controls
     */
    createBotControls() {
        const gameControls = document.querySelector('.game-controls');
        if (!gameControls) return;

        // Remove existing bot controls
        const existingControls = document.getElementById('bot-controls');
        if (existingControls) {
            existingControls.remove();
        }

        // Create new bot controls
        const botControls = document.createElement('div');
        botControls.id = 'bot-controls';
        botControls.className = 'bot-controls';
        botControls.innerHTML = `
            <div class="bot-info">
                <div id="bot-status">🤖 Bot Ready</div>
                <div id="bot-evaluation">Evaluation: 0</div>
            </div>
            <div class="bot-actions">
                <button id="new-bot-game-btn" class="btn btn-primary">New Game</button>
                <button id="bot-hint-btn" class="btn btn-secondary">Hint</button>
                <select id="bot-difficulty" class="form-select">
                    <option value="2">Easy</option>
                    <option value="3">Normal</option>
                    <option value="4" selected>Hard</option>
                    <option value="5">Expert</option>
                    <option value="6">Master</option>
                </select>
            </div>
        `;

        gameControls.appendChild(botControls);

        // Add event listeners
        document.getElementById('new-bot-game-btn').addEventListener('click', () => {
            this.showNewGameDialog();
        });

        document.getElementById('bot-hint-btn').addEventListener('click', () => {
            this.showHint();
        });

        document.getElementById('bot-difficulty').addEventListener('change', (e) => {
            this.difficulty = parseInt(e.target.value);
            console.log(`🎚️ Difficulty changed to: ${this.difficulty}`);
        });
    }

    /**
     * Show bot thinking indicator
     */
    showBotThinking(thinking) {
        const botStatus = document.getElementById('bot-status');
        if (botStatus) {
            botStatus.textContent = thinking ? '🤖 Bot thinking...' : '🤖 Bot Ready';
            botStatus.className = thinking ? 'bot-thinking' : '';
        }

        // Disable hint button while bot is thinking
        const hintBtn = document.getElementById('bot-hint-btn');
        if (hintBtn) {
            hintBtn.disabled = thinking || this.app.gameState.current_turn === this.botColor;
        }
    }

    /**
     * Show bot's position evaluation
     */
    showBotEvaluation(evaluation) {
        const evalElement = document.getElementById('bot-evaluation');
        if (evalElement) {
            const evalText = evaluation > 0 ? `+${evaluation}` : `${evaluation}`;
            evalElement.textContent = `Evaluation: ${evalText}`;
            
            // Color code the evaluation
            evalElement.className = evaluation > 50 ? 'eval-positive' : 
                                   evaluation < -50 ? 'eval-negative' : 'eval-neutral';
        }
    }

    /**
     * Show hint to player
     */
    async showHint() {
        if (!this.gameActive || this.isThinking || this.app.gameState.current_turn !== this.playerColor) {
            return;
        }

        try {
            console.log('💡 Getting hint...');
            console.log('📤 [HINT] Sending to engine for hint:', {
                board: this.app.board,
                gameState: this.app.gameState,
                difficulty: Math.max(this.difficulty - 1, 1)
            });
            
            const hintMove = await this.engine.findBestMove(
                this.app.board,
                this.app.gameState,
                Math.max(2, this.difficulty - 1),  // Slightly easier than bot
                3000
            );

            console.log('📥 [HINT] Received from engine:', hintMove);

            if (hintMove) {
                this.app.showHint(hintMove.from, hintMove.to);
                this.showBotEvaluation(hintMove.evaluation);
                
                // Show hint message
                this.showMessage(`💡 Hint: Move from ${this.app.positionToAlgebraic(hintMove.from)} to ${this.app.positionToAlgebraic(hintMove.to)}`);
            }
        } catch (error) {
            console.error('Error getting hint:', error);
            this.showError('Could not generate hint.');
        }
    }

    /**
     * Show new game dialog
     */
    showNewGameDialog() {
        const modal = document.createElement('div');
        modal.className = 'modal';
        modal.style.display = 'flex';
        modal.innerHTML = `
            <div class="modal-dialog">
                <div class="modal-content">
                    <div class="modal-header">
                        <h5 class="modal-title">🎮 New Bot Game</h5>
                    </div>
                    <div class="modal-body">
                        <div class="mb-3">
                            <label class="form-label">🎨 Play as:</label>
                            <select id="color-choice" class="form-select">
                                <option value="white">⚪ White (First Move)</option>
                                <option value="black">⚫ Black (Second Move)</option>
                                <option value="random">🎲 Random</option>
                            </select>
                        </div>
                        <div class="mb-3">
                            <label class="form-label">⚡ Difficulty Level:</label>
                            <select id="difficulty-choice" class="form-select">
                                <option value="2">🟢 Easy (Beginner)</option>
                                <option value="3">🟡 Normal (Casual)</option>
                                <option value="4" selected>🟠 Hard (Challenging)</option>
                                <option value="5">🔴 Expert (Advanced)</option>
                                <option value="6">🟣 Master (Grandmaster)</option>
                            </select>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" id="cancel-new-game">Cancel</button>
                        <button type="button" class="btn btn-primary" id="start-new-game">Start Game</button>
                    </div>
                </div>
            </div>
        `;

        document.body.appendChild(modal);

        // Center the modal by ensuring proper flex display
        setTimeout(() => {
            modal.classList.add('active');
        }, 10);

        // Event listeners
        document.getElementById('cancel-new-game').addEventListener('click', () => {
            modal.classList.remove('active');
            setTimeout(() => modal.remove(), 300);
        });
        
        document.getElementById('start-new-game').addEventListener('click', () => {
            let color = document.getElementById('color-choice').value;
            if (color === 'random') {
                color = Math.random() < 0.5 ? 'white' : 'black';
            }
            const difficulty = parseInt(document.getElementById('difficulty-choice').value);
            
            modal.classList.remove('active');
            setTimeout(() => modal.remove(), 300);
            this.startGame(color, difficulty);
        });
    }

    /**
     * Show game result
     */
    showGameResult(message) {
        const modal = document.createElement('div');
        modal.className = 'modal fade show';
        modal.style.display = 'block';
        modal.innerHTML = `
            <div class="modal-dialog">
                <div class="modal-content">
                    <div class="modal-header">
                        <h5 class="modal-title">Game Over</h5>
                    </div>
                    <div class="modal-body">
                        <p>${message}</p>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" id="back-to-menu">Back to Menu</button>
                        <button type="button" class="btn btn-primary" id="play-again">Play Again</button>
                    </div>
                </div>
            </div>
        `;

        document.body.appendChild(modal);

        document.getElementById('back-to-menu').addEventListener('click', () => {
            modal.remove();
            this.app.showMainMenu();
        });

        document.getElementById('play-again').addEventListener('click', () => {
            modal.remove();
            this.startGame(this.playerColor, this.difficulty);
        });
    }

    /**
     * Show error message
     */
    showError(message) {
        this.showMessage(message, 'error');
    }

    /**
     * Show general message
     */
    showMessage(message, type = 'info') {
        // Use app's existing toast/notification system if available
        if (this.app.showToast) {
            this.app.showToast(message, type);
        } else {
            console.log(`[${type.toUpperCase()}] ${message}`);
        }
    }

    /**
     * Stop current game
     */
    stopGame() {
        this.gameActive = false;
        this.isThinking = false;
        console.log('🛑 Bot game stopped');
    }

    /**
     * Get bot status
     */
    getStatus() {
        return {
            initialized: this.isInitialized,
            gameActive: this.gameActive,
            isThinking: this.isThinking,
            playerColor: this.playerColor,
            botColor: this.botColor,
            difficulty: this.difficulty
        };
    }
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = ChessBot;
} else {
    window.ChessBot = ChessBot;
}