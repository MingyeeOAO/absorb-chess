const UNDER_DEVELOPMENT = true;

class ChessApp {
    constructor() {
        this.websocket = null;
        this.currentScreen = 'main-menu';
        this.playerId = null;
        this.playerColor = null;
        this.isOwner = false;
        this.currentLobby = null;
        this.lobbyData = null;
        this.gameState = null;
        this.selectedSquare = null;
        this.possibleMoves = [];
        this.lastMoveHighlight = null; // Track last move for highlighting
        this.enableAnimations = false; // Animation toggle (false to disable)
        this.currentTextureSet = 'classic';
        this.pieceImages = null;
        this.disconnectionTimer = null;
        this.disconnectionWarning = null;
        this.lastConnectionCheck = Date.now();
        this.missedChecks = 0;
        this.disconnectionTimer = null;
        this.disconnectionCountdown = null;
        
        this.initializeEventListeners();
        this.loadTextureSet('classic').then(() => {
            this.showScreen('main-menu');
        });
    }
    
    initializeEventListeners() {
        // Main menu buttons
        document.getElementById('create-lobby-btn').addEventListener('click', () => {
            this.showScreen('create-lobby');
        });
        
        document.getElementById('join-lobby-btn').addEventListener('click', () => {
            this.showScreen('join-lobby');
        });

        document.getElementById('search-game-btn').addEventListener('click', () => {
            this.showScreen('search-game');
        });
        
        // Back buttons
        document.getElementById('back-to-menu').addEventListener('click', () => {
            this.showScreen('main-menu');
        });
        
        document.getElementById('back-to-menu-join').addEventListener('click', () => {
            this.showScreen('main-menu');
        });
        
        // Forms
        document.getElementById('create-lobby-form').addEventListener('submit', (e) => {
            e.preventDefault();
            this.createLobby();
        });
        
        document.getElementById('join-lobby-form').addEventListener('submit', (e) => {
            e.preventDefault();
            this.joinLobby();
        });
        
        // Lobby controls
        document.getElementById('start-game-btn').addEventListener('click', () => {
            this.startGame();
        });
        
        document.getElementById('leave-lobby-btn').addEventListener('click', () => {
            this.leaveLobby();
        });
        
        document.getElementById('leave-game-btn').addEventListener('click', () => {
            this.confirmLeaveOrResign('leave');
        });
        
        document.getElementById('resign-btn').addEventListener('click', () => {
            this.confirmLeaveOrResign('resign');
        });
        
        document.getElementById('offer-draw-btn').addEventListener('click', () => {
            this.offerDraw();
        });

        // Search game
        document.getElementById('search-game-form').addEventListener('submit', (e) => {
            e.preventDefault();
            this.searchGame();
        });

        document.getElementById('cancel-search').addEventListener('click', () => {
            // This handles cancel before search starts - just go back to main menu
            this.showScreen('main-menu');
        });
        
        // Error modal
        document.getElementById('close-error').addEventListener('click', () => {
            this.hideError();
        });
    }
    
    showScreen(screenName) {
        // Hide all screens
        document.querySelectorAll('.screen').forEach(screen => {
            screen.classList.remove('active');
        });
        
        // Reset search UI when showing search screen
        if (screenName === 'search-game') {
            this.isSearchingGame = false;
            document.getElementById('search-game-form').style.display = 'block';
            document.getElementById('searching-status').style.display = 'none';
        }
        
        // Show target screen
        document.getElementById(screenName).classList.add('active');
        this.currentScreen = screenName;
    }
    
    async connectWebSocket() {
        var servers = [
            'ws://localhost:8765',
            'wss://chess.harc.qzz.io/ws/'
        ];
        if(UNDER_DEVELOPMENT == false){
            servers.remove('ws://localhost:8765');
        }

        for (var serverUrl of servers) {
            try {
                var isValid = await this.tryValidateServer(serverUrl);
                if (isValid) {
                    // Found a working server, establish the real connection
                    return await this.establishConnection(serverUrl);
                }
            } catch (error) {
                console.log(`Failed to connect to ${serverUrl}:`, error);
                continue; // Try next server
            }
        }

        // All servers failed
        this.showError('Failed to connect to any server. Please check if the server is running.');
        throw new Error('No available servers');
    }

    async tryValidateServer(url) {
        return new Promise((resolve) => {
            console.log(`Validating server at ${url}`);
            const ws = new WebSocket(url);
            
            let timeoutId = setTimeout(() => {
                ws.close();
                resolve(false);
            }, 3000);

            ws.onopen = () => {
                try {
                    ws.send(JSON.stringify({ type: 'validate_server' }));
                } catch (error) {
                    clearTimeout(timeoutId);
                    ws.close();
                    resolve(false);
                }
            };

            ws.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === 'validate_server_response') {
                        clearTimeout(timeoutId);
                        ws.close();
                        resolve(data.isChessServer === true);
                    }
                } catch (error) {
                    clearTimeout(timeoutId);
                    ws.close();
                    resolve(false);
                }
            };

            ws.onerror = () => {
                clearTimeout(timeoutId);
                ws.close();
                resolve(false);
            };

            ws.onclose = () => {
                clearTimeout(timeoutId);
                resolve(false);
            };
        });
    }

    async establishConnection(url) {
        return new Promise((resolve, reject) => {
            console.log(`Establishing main connection to ${url}`);
            this.websocket = new WebSocket(url);
            this.lastPingTime = Date.now();
            this.reconnectAttempts = 0;
            
            this.websocket.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    this.handleMessage(data);
                } catch (error) {
                    console.error('Failed to parse message:', error);
                }
            };

            this.websocket.onopen = () => {
                console.log(`Connected to ${url}`);
                this.updateConnectionStatus(true);
                
                // Set up connection check interval
                if (this.connectionCheckInterval) {
                    clearInterval(this.connectionCheckInterval);
                }
                this.connectionCheckInterval = setInterval(() => {
                    if (this.gameState && !this.gameState.game_over) {
                        this.sendMessage({
                            type: 'connection_check',
                            timestamp: Date.now()
                        });
                    }
                }, 10000);

                // Set up heartbeat interval
                if (this.heartbeatInterval) {
                    clearInterval(this.heartbeatInterval);
                }
                this.heartbeatInterval = setInterval(() => {
                    this.sendMessage({type: 'Heartbeat'});
                }, 60000);

                resolve();
            };

            this.websocket.onclose = () => {
                console.log('Disconnected from server');
                this.updateConnectionStatus(false);
                if (this.connectionCheckInterval) {
                    clearInterval(this.connectionCheckInterval);
                }
                if (this.heartbeatInterval) {
                    clearInterval(this.heartbeatInterval);
                }
            };

            this.websocket.onerror = (error) => {
                console.error('WebSocket error:', error);
                reject(error);
            };
   
            this.websocket.onmessage = (event) => {
                this.handleMessage(JSON.parse(event.data));
            };
                
        });
    }
    
    async searchGame() {
        try {
            await this.connectWebSocket();
            const playerName = document.getElementById('player-name-search').value;
            
            // Show searching status
            document.getElementById('search-game-form').style.display = 'none';
            document.getElementById('searching-status').style.display = 'block';
            this.isSearchingGame = true;
            
            this.sendMessage({
                type: 'search_game',
                player_name: playerName
            });
        } catch (error) {
            this.showError('Failed to start game search. Please try again.' + error);
        }
    }

    cancelSearch() {
        // Reset search state regardless of current state
        this.isSearchingGame = false;
        
        // Send cancel message if we were actually searching
        if (this.websocket && this.websocket.readyState === WebSocket.OPEN) {
            this.sendMessage({
                type: 'cancel_search'
            });
        }
        
        // Reset UI to initial state
        document.getElementById('search-game-form').style.display = 'block';
        document.getElementById('searching-status').style.display = 'none';
        
        // Go back to main menu
        this.showScreen('main-menu');
    }
    
    async createLobby() {
        try {
            await this.connectWebSocket();
            
            const playerName = document.getElementById('player-name-create').value;
            const timeMinutes = parseInt(document.getElementById('time-minutes').value || '0', 10);
            const timeIncrement = parseInt(document.getElementById('time-increment').value || '0', 10);
            
            this.sendMessage({
                type: 'create_lobby',
                player_name: playerName,
                settings: {
                    time_minutes: timeMinutes,
                    time_increment_seconds: timeIncrement,
                }
            });
        } catch (error) {
            console.error('Failed to create lobby:', error);
        }
    }
    
    async joinLobby() {
        try {
            await this.connectWebSocket();
            
            const lobbyCode = document.getElementById('lobby-code').value.toUpperCase();
            const playerName = document.getElementById('player-name-join').value;
            
            this.sendMessage({
                type: 'join_lobby',
                lobby_code: lobbyCode,
                player_name: playerName
            });
        } catch (error) {
            console.error('Failed to join lobby:', error);
        }
    }
    
    leaveLobby() {
        if (this.websocket) {
            this.sendMessage({ type: 'leave_lobby' });
        }
        this.showScreen('main-menu');
        this.resetLobbyState();
    }
    
    leaveGame() {
        this.leaveLobby();
    }
    
    startGame() {
        if (this.isOwner) {
            this.sendMessage({ type: 'start_game' });
        }
    }
    
    sendMessage(message) {
        if (this.websocket && this.websocket.readyState === WebSocket.OPEN) {
            this.websocket.send(JSON.stringify(message));
        } else {
            this.showError('Not connected to server');
        }
    }

    confirmLeaveOrResign(action) {
        const modal = document.createElement('div');
        modal.className = 'modal active';
        const title = action === 'resign' ? 'Confirm Resign' : 'Confirm Leave';
        const body = action === 'resign' ? 'Are you sure you want to resign?' : 'Leave game? This counts as resign.';
        modal.innerHTML = `
            <div class="modal-content">
                <h3>${title}</h3>
                <p>${body}</p>
                <div class="modal-actions">
                    <button id="confirm-action" class="btn btn-primary">Yes</button>
                    <button id="cancel-action" class="btn btn-secondary">No</button>
                </div>
            </div>
        `;
        document.body.appendChild(modal);
        document.getElementById('cancel-action').addEventListener('click', () => modal.remove());
        document.getElementById('confirm-action').addEventListener('click', () => {
            modal.remove();
            if (action === 'resign') {
                this.sendMessage({ type: 'resign' });
            } else {
                this.sendMessage({ type: 'resign' });
                this.leaveGame();
            }
        });
    }

    offerDraw() {
        this.sendMessage({ type: 'offer_draw' });
    }
    
    handleMessage(data) {
        console.log('Received message:', data);
        
        switch (data.type) {
            case 'connection_check':
                // Respond immediately to connection check
                this.sendMessage({
                    type: 'connection_check_response',
                    timestamp: data.timestamp
                });
                break;
            
            case 'player_disconnected':
                this.handlePlayerDisconnection(data);
                break;
            
            case 'search_game_started':
                this.handleSearchStarted(data);
                break;
            case 'search_game_found':
                this.handleGameFound(data);
                break;
            case 'search_game_cancelled':
                this.handleSearchCancelled();
                break;
            case 'lobby_created':
                this.handleLobbyCreated(data);
                break;
            case 'lobby_joined':
                this.handleLobbyJoined(data);
                break;
            case 'lobby_update':
                this.handleLobbyUpdate(data);
                break;
            case 'game_started':
                this.handleGameStarted(data);
                break;
            case 'move_made':
                this.handleMoveMade(data);
                break;
            case 'game_over':
                this.gameState = data.game_state;
                this.updateValidMovesFromGameState();
                this.handleGameOver(data.reason);
                break;
            case 'connection_check_response':
                // Reset reconnect attempts when we get a response
                this.reconnectAttempts = 0;
                break;
            case 'draw_offered':
                this.handleDrawOffered(data);
                break;
            case 'draw_offer_ack':
                this.toast('Draw offer sent!');
                break;
            case 'draw_offer_rate_limited':
                this.toast(`Too many draw offers. Try again in ${data.retry_after}s`);
                break;
            case 'draw_declined':
                this.toast('Your draw offer was declined.');
                break;
            case 'promotion_pending':
                this.handlePromotionPending(data);
                break;
            case 'promotion_applied':
                this.handlePromotionApplied(data);
                break;
            case 'promotion_canceled':
                this.handlePromotionCanceled(data);
                break;
            case 'invalid_move':
                this.handleInvalidMove(data);
                break;
            case 'error':
                this.showError(data.message);
                break;
        }
    }
    
    handleSearchStarted(data) {
        // Search has been acknowledged by server
        document.getElementById('searching-status').innerHTML = '<p>Searching for opponent...</p><div class="loading-spinner"></div>';
    }

    handleGameFound(data) {
        // Only handle if we're actually searching
        if (!this.isSearchingGame) {
            console.log('Received game found but not searching - ignoring stale message');
            return;
        }
        
        this.isSearchingGame = false;
        
        // Store lobby data from search result
        this.currentLobby = data.lobby_code;
        this.playerId = data.player_id;
        
        // Get the assigned color from server
        if (data.player_color) {
            this.playerColor = data.player_color;
            // Update UI to show we found a game and our assigned color
            document.getElementById('searching-status').innerHTML = '<p>Opponent found! You are playing as ' + this.playerColor + '</p>';
        } else {
            document.getElementById('searching-status').innerHTML = '<p>Opponent found! Waiting for game to start...</p>';
        }
    }

    handleSearchCancelled() {
        this.isSearchingGame = false;
        document.getElementById('search-game-form').style.display = 'block';
        document.getElementById('searching-status').style.display = 'none';
        this.showScreen('main-menu');
    }
    
    handleLobbyCreated(data) {
        this.playerId = data.player_id;
        this.isOwner = data.is_owner;
        this.currentLobby = data.lobby_code;
        this.lobbyData = data.lobby_data;
        // Set player color from lobby data
        if (this.lobbyData && this.lobbyData.players) {
            const player = this.lobbyData.players.find(p => p.id === this.playerId);
            if (player) {
                this.playerColor = player.color;
            }
        }
        this.showScreen('lobby-waiting');
        this.updateLobbyDisplay();
    }
    
    handleLobbyJoined(data) {
        this.playerId = data.player_id;
        this.isOwner = data.is_owner;
        this.currentLobby = data.lobby_code;
        this.lobbyData = data.lobby_data;
        // Set player color from lobby data
        if (this.lobbyData && this.lobbyData.players) {
            const player = this.lobbyData.players.find(p => p.id === this.playerId);
            if (player) {
                this.playerColor = player.color;
            }
        }
        this.showScreen('lobby-waiting');
        this.updateLobbyDisplay();
    }
    
    handleLobbyUpdate(data) {
        this.lobbyData = data;
        this.isOwner = data.is_owner;
        // Update player color from lobby data
        if (this.lobbyData && this.lobbyData.players) {
            const player = this.lobbyData.players.find(p => p.id === this.playerId);
            if (player) {
                this.playerColor = player.color;
            }
        }
        this.updateLobbyDisplay();
    }
    
    handleGameStarted(data) {
        // Only proceed if we expect a game to start (either searching, in lobby, or have current lobby)
        if (!this.isSearchingGame && !this.currentLobby && !this.lobbyData) {
            console.log('Received game started but not in expected state - ignoring stale message');
            return;
        }
        
        this.gameState = data.game_state;
        
        // Set player color from server data if provided
        if (data.player_color) {
            this.playerColor = data.player_color;
            console.log('Player color set to:', this.playerColor); // Debug log
        }
        
        // Store lobby data for player names
        if (data.lobby_data) {
            this.lobbyData = data.lobby_data;
        }
        
        // Extract and cache valid moves from game state
        this.updateValidMovesFromGameState();
        
        // Set player names from lobby data or game state
        if (this.lobbyData && this.lobbyData.players) {
            const whitePlayer = this.lobbyData.players.find(p => p.color === 'white');
            const blackPlayer = this.lobbyData.players.find(p => p.color === 'black');
            
            if (whitePlayer) {
                document.getElementById('white-player-name').textContent = whitePlayer.name;
            }
            if (blackPlayer) {
                document.getElementById('black-player-name').textContent = blackPlayer.name;
            }
        } else if (this.gameState && this.gameState.players) {
            // Fallback to game state player names
            const whitePlayer = this.gameState.players.find(p => p.color === 'white');
            const blackPlayer = this.gameState.players.find(p => p.color === 'black');
            
            if (whitePlayer) {
                document.getElementById('white-player-name').textContent = whitePlayer.name;
            }
            if (blackPlayer) {
                document.getElementById('black-player-name').textContent = blackPlayer.name;
            }
        }
        
        this.showScreen('game-screen');
        this.renderChessBoard();
        this.initializeGameControls();
        this.updateClocks();
        // Start local ticking interval
        if (this.clockInterval) clearInterval(this.clockInterval);
        this.clockInterval = setInterval(() => this.updateClocks(), 250);

        // Debug log
        console.log('Current turn:', this.gameState.current_turn);
        console.log('My color:', this.playerColor);
    }
    
    handleMoveMade(data) {
        // Store move info for animation and highlighting
        if (data && data.last_move && data.last_move.from && data.last_move.to) {
            this.lastMoveHighlight = { from: data.last_move.from, to: data.last_move.to };
            if (this.enableAnimations) {
                this.animateMove(data.last_move.from, data.last_move.to);
                // Update game state after animation
                setTimeout(() => {
                    this.gameState = data.game_state;
                    this.updateValidMovesFromGameState();
                    this.renderChessBoard();
                    this.updateMoveHistory();
                    this.updateCurrentTurn();
                    this.updateClocks();
                    this.clearSelection();
                    if (this.gameState.game_over) {
                        this.handleGameOver();
                    }
                }, 300);
            } else {
                // Update immediately without animation
                this.gameState = data.game_state;
                this.updateValidMovesFromGameState();
                this.renderChessBoard();
                this.updateMoveHistory();
                this.updateCurrentTurn();
                this.updateClocks();
                this.clearSelection();
                if (this.gameState.game_over) {
                    this.handleGameOver();
                }
            }
        }
    }

    handlePromotionPending(data) {
        this.gameState = data.game_state;
        this.updateValidMovesFromGameState();
        this.renderChessBoard();
        // Always show modal if promotion_pending exists in gameState
        if (this.gameState && this.gameState.promotion_pending) {
            this.showPromotionModal();
        }
    }

    handlePromotionApplied(data) {
        // Update lastMoveHighlight with the promotion move
        if (data && data.from && data.to) {
            this.lastMoveHighlight = { from: data.from, to: data.to };
            // If animations are enabled, animate the promotion move
            if (this.enableAnimations) {
                this.animateMove(data.from, data.to);
            }
        }

        // Update game state and check for game over
        this.gameState = data.game_state;
        this.updateValidMovesFromGameState();
        if (this.gameState.game_over) {
            this.handleGameOver();
        }

        // Update the board and UI
        this.renderChessBoard();
        this.updateMoveHistory();
        this.updateCurrentTurn();
        this.updateClocks();
        this.clearPromotionModal();
    }

    handlePromotionCanceled(data) {
        this.gameState = data.game_state;
        this.updateValidMovesFromGameState();
        this.renderChessBoard();
        this.clearPromotionModal();
    }

    handleInvalidMove(data) {
        console.error('Invalid move details:', data);
        
        // Build detailed error message
        let errorMessage = `Invalid Move: ${data.reason}\n\n`;
        errorMessage += `From: (${data.from[0]},${data.from[1]}) To: (${data.to[0]},${data.to[1]})\n`;
        errorMessage += `Current Turn: ${data.current_turn}\n\n`;
        
        if (data.details && data.details.length > 0) {
            errorMessage += 'Details:\n';
            data.details.forEach((detail, index) => {
                errorMessage += `${index + 1}. ${detail}\n`;
            });
        }
        
        // Show detailed error in console for debugging
        console.log('=== INVALID MOVE DEBUG INFO ===');
        console.log('From:', data.from);
        console.log('To:', data.to);
        console.log('Current Turn:', data.current_turn);
        console.log('Details:', data.details);
        console.log('===============================');
        
        // Show simplified message to user
        this.toast(`Invalid move: ${data.reason}`);
        
        // Clear any pending selection since the move failed
        this.clearSelection();
    }

    showPromotionModal() {
        this.clearPromotionModal();
        const modal = document.createElement('div');
        modal.id = 'promotion-modal';
        modal.className = 'modal active';
        const allowCancel = !!(this.gameState && this.gameState.promotion_cancel_allowed);
        const headerRight = allowCancel ? '<button id="promotion-close" class="modal-close" aria-label="Close">×</button>' : '';
        const cancelBtn = allowCancel ? '<button class="btn btn-secondary" data-choice="cancel">Cancel</button>' : '';
        modal.innerHTML = `
            <div class="modal-content">
                <div class="modal-header">
                    <h3>Choose Promotion</h3>
                    ${headerRight}
                </div>
                <div class="promotion-choices">
                    <button class="btn btn-primary" data-choice="queen">Queen</button>
                    <button class="btn btn-primary" data-choice="rook">Rook</button>
                    <button class="btn btn-primary" data-choice="bishop">Bishop</button>
                    <button class="btn btn-primary" data-choice="knight">Knight</button>
                    ${cancelBtn}
                </div>
            </div>
        `;
        document.body.appendChild(modal);
        modal.querySelectorAll('button[data-choice]').forEach(btn => {
            btn.addEventListener('click', () => {
                const choice = btn.getAttribute('data-choice');
                this.sendMessage({ type: 'promotion_choice', choice });
            });
        });
        if (allowCancel) {
            const closeBtn = document.getElementById('promotion-close');
            if (closeBtn) {
                closeBtn.addEventListener('click', () => {
                    this.sendMessage({ type: 'promotion_choice', choice: 'cancel' });
                });
            }
        }
        // Also handle cancel during searching
        const cancelSearchingBtn = document.getElementById('cancel-searching');
        if (cancelSearchingBtn) {
            cancelSearchingBtn.addEventListener('click', () => {
                this.cancelSearch();
            });
        }
    }

    clearPromotionModal() {
        const modal = document.getElementById('promotion-modal');
        if (modal) modal.remove();
    }
    
    handleGameOver(reason) {
        const winner = this.gameState.winner;
        let winnerText;
        if (reason === 'draw' || winner == null) {
            winnerText = 'Game drawn!';
        } else {
            winnerText = winner === this.playerColor ? 'You won!' : 'You lost!';
        }
        
        // Show game over modal instead of error modal
        this.showGameOverModal(winnerText);
        
        // Disable all interactions
        document.querySelectorAll('.chess-square').forEach(square => {
            square.style.pointerEvents = 'none';
        });
    }

    handleValidMoves(data) {
        // Store all valid moves for each piece
        this.allValidMoves = new Map();
        
        // Convert server moves to our format for each piece
        for (const [position, moves] of Object.entries(data.moves)) {
            const [row, col] = position.split(',').map(Number);
            this.allValidMoves.set(`${row},${col}`, moves.map(move => ({
                row: move[0],
                col: move[1]
            })));
        }
        
        // If a piece is selected, show its moves
        if (this.selectedSquare) {
            const key = `${this.selectedSquare.row},${this.selectedSquare.col}`;
            this.possibleMoves = this.allValidMoves.get(key) || [];
            this.showPossibleMoves(this.selectedSquare.row, this.selectedSquare.col);
        }
    }

    updateValidMovesFromGameState() {
        // Extract and cache valid moves from game state if available
        if (this.gameState && this.gameState.valid_moves) {
            this.allValidMoves = new Map();
            // Convert server moves to our format for each piece
            for (const [position, moves] of Object.entries(this.gameState.valid_moves)) {
                const [row, col] = position.split(',').map(Number);
                this.allValidMoves.set(`${row},${col}`, moves.map(move => ({
                    row: move[0],
                    col: move[1]
                })));
            }
            // Debug print: log all valid moves
            console.log('Frontend valid_moves:', this.gameState.valid_moves);
            console.log('Frontend allValidMoves:', this.allValidMoves);
        }
    }

    handleDrawOffered(data) {
        // Remove any existing draw offer modal to avoid stale listeners
        const existing = document.getElementById('draw-offer-modal');
        if (existing) existing.remove();

        const modal = document.createElement('div');
        modal.id = 'draw-offer-modal';
        modal.className = 'modal active';
        modal.innerHTML = `
            <div class="modal-content">
                <h3>Draw Offer</h3>
                <p>Your opponent offered a draw. Accept?</p>
                <div class="modal-actions">
                    <button id="accept-draw" class="btn btn-primary">Accept</button>
                    <button id="decline-draw" class="btn btn-secondary">Decline</button>
                </div>
            </div>
        `;
        document.body.appendChild(modal);
        const acceptBtn = document.getElementById('accept-draw');
        const declineBtn = document.getElementById('decline-draw');

        acceptBtn.addEventListener('click', () => {
            modal.remove();
            this.sendMessage({ type: 'accept_draw' });
        });
        declineBtn.addEventListener('click', () => {
            modal.remove();
            this.sendMessage({ type: 'decline_draw', to: data.from });
        });
    }

    toast(msg) {
        const n = document.createElement('div');
        n.className = 'toast';
        n.textContent = msg;
        document.body.appendChild(n);
        setTimeout(() => n.classList.add('show'), 10);
        setTimeout(() => {
            n.classList.remove('show');
            setTimeout(() => n.remove(), 300);
        }, 3000);
    }
    
    showGameOverModal(winnerText) {
        const modal = document.createElement('div');
        modal.className = 'modal active';
        modal.innerHTML = `
            <div class="modal-content">
                <h2>Game Over!</h2>
                <p>${winnerText}</p>
                <div class="modal-actions">
                    <button id="back-to-menu-btn" class="btn btn-primary">Back to Menu</button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        // Add event listener for back to menu button
        document.getElementById('back-to-menu-btn').addEventListener('click', () => {
            this.backToMenu();
        });
    }
    
    backToMenu() {
        // Remove game over modal
        const modal = document.querySelector('.modal');
        if (modal) {
            modal.remove();
        }
        
        // Reset game state
        this.resetGameState();
        
        // Reset search state to prevent stale "game found" messages
        this.isSearchingGame = false;
        this.currentLobby = null;
        this.lobbyData = null;
        this.playerId = null;
        this.playerColor = null;
        this.isOwner = false;
        
        // Reset search UI
        document.getElementById('search-game-form').style.display = 'block';
        document.getElementById('searching-status').style.display = 'none';
        
        // Go back to main menu
        this.showScreen('main-menu');
    }
    
    resetGameState() {
        this.gameState = null;
        this.selectedSquare = null;
        this.possibleMoves = [];
        this.clearSelection();
    }
    
    updateCurrentTurn() {
        if (this.gameState) {
            const currentTurnElement = document.getElementById('current-turn');
            let turnText = this.gameState.current_turn === 'white' ? 'White' : 'Black';
            // Add check status
            if (this.gameState.white_king_in_check && this.gameState.current_turn === 'white') {
                turnText += ' (IN CHECK!)';
            } else if (this.gameState.black_king_in_check && this.gameState.current_turn === 'black') {
                turnText += ' (IN CHECK!)';
            }
            currentTurnElement.textContent = turnText;
            // Update player names below clocks
            if (this.gameState && this.gameState.players) {
                const whiteName = this.gameState.players.find(p => p.color === 'white')?.name || 'White';
                const blackName = this.gameState.players.find(p => p.color === 'black')?.name || 'Black';
                const whiteNameEl = document.getElementById('white-player-name');
                const blackNameEl = document.getElementById('black-player-name');
                if (whiteNameEl) whiteNameEl.textContent = whiteName;
                if (blackNameEl) blackNameEl.textContent = blackName;
            }
        }
    }

    updateClocks() {
        if (!this.gameState || !this.gameState.clock) return;
        const { white_ms, black_ms, last_turn_start } = this.gameState.clock;

        // Apply live ticking only for current player locally
        let liveWhite = white_ms;
        let liveBlack = black_ms;
        if (last_turn_start) {
            try {
                const started = new Date(last_turn_start);
                const now = new Date();
                const elapsed = Math.max(0, now.getTime() - started.getTime());
                if (this.gameState.current_turn === 'white') {
                    liveWhite = Math.max(0, (white_ms || 0) - elapsed);
                } else {
                    liveBlack = Math.max(0, (black_ms || 0) - elapsed);
                }
            } catch (e) {}
        }
        const whiteEl = document.getElementById('clock-white');
        const blackEl = document.getElementById('clock-black');
        if (whiteEl) whiteEl.textContent = this.formatMs(liveWhite);
        if (blackEl) blackEl.textContent = this.formatMs(liveBlack);

        // If active player's time hits zero locally, notify server once
        if (!this.gameState.game_over) {
            if (this.gameState.current_turn === 'white' && liveWhite <= 0) {
                this.sendMessage({ type: 'timeout' });
            } else if (this.gameState.current_turn === 'black' && liveBlack <= 0) {
                this.sendMessage({ type: 'timeout' });
            }
        }
    }

    formatMs(ms) {
        ms = Math.max(0, parseInt(ms || 0, 10));
        const totalSeconds = Math.floor(ms / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        return `${String(minutes).padStart(2,'0')}:${String(seconds).padStart(2,'0')}`;
    }
    
    updateLobbyDisplay() {
        if (!this.lobbyData) return;
        
        // Update lobby code
        document.getElementById('lobby-code-display').textContent = this.lobbyData.lobby_code;
        
        // Update players list
        const playersList = document.getElementById('players-list');
        playersList.innerHTML = '';
        
        this.lobbyData.players.forEach(player => {
            const playerElement = document.createElement('div');
            playerElement.className = 'player-item';
            playerElement.innerHTML = `
                <span>${player.name}</span>
                <span class="player-color ${player.color}">${player.color.toUpperCase()}</span>
            `;
            playersList.appendChild(playerElement);
        });
        
        // Update settings display
        const settingsDisplay = document.getElementById('lobby-settings');
        const minutes = this.lobbyData.settings?.time_minutes ?? null;
        const increment = this.lobbyData.settings?.time_increment_seconds ?? null;
        const timeText = (minutes !== null && minutes !== undefined)
            ? `${minutes}m + ${increment || 0}s`
            : 'No Time Limit';
        settingsDisplay.innerHTML = `
            <div><strong>Time Control:</strong> ${timeText}</div>
            <div><strong>Ability System:</strong> ${this.lobbyData.settings?.ability_system || 'Enabled'}</div>
        `;
        
        // Show owner controls if this player is the owner
        const ownerControls = document.getElementById('owner-controls');
        if (this.isOwner) {
            ownerControls.style.display = 'block';
            // Enable start button when 2 players are present
            const startBtn = document.getElementById('start-game-btn');
            startBtn.disabled = this.lobbyData.players.length < 2;
        } else {
            ownerControls.style.display = 'none';
        }
        
        // Update waiting message
        const waitingText = document.getElementById('waiting-text');
        if (this.lobbyData.players.length < 2) {
            waitingText.textContent = 'Waiting for players...';
        } else if (this.isOwner) {
            waitingText.textContent = 'Ready to start! Click "Start Game" to begin.';
        } else {
            waitingText.textContent = 'Waiting for owner to start the game...';
        }
    }
    
    renderChessBoard() {
        const board = document.getElementById('chess-board');
        board.innerHTML = '';
        
        if (!this.gameState || !this.gameState.board) {
            return;
        }
        
        // Rotate board for black player
        const isBlackPlayer = this.playerColor === 'black';
        const boardClass = isBlackPlayer ? 'chess-board rotated' : 'chess-board';
        board.className = boardClass;
        
        for (let row = 0; row < 8; row++) {
            for (let col = 0; col < 8; col++) {
                const square = document.createElement('div');
                let squareClass = `chess-square ${(row + col) % 2 === 0 ? 'light' : 'dark'}`;
                
                // Check if this square has a king in check
                const piece = this.gameState.board[row][col];
                if (piece && piece.type === 'king') {
                    if ((piece.color === 'white' && this.gameState.white_king_in_check) ||
                        (piece.color === 'black' && this.gameState.black_king_in_check)) {
                        squareClass += ' king-in-check';
                    }
                }
                
                square.className = squareClass;
                square.dataset.row = row;
                square.dataset.col = col;
                
                if (piece) {
                    const pieceElement = this.getPieceElement(piece.type, piece.color, piece.abilities);
                    square.appendChild(pieceElement);
                }
                
                // Add hover event for ability display
                square.addEventListener('mouseenter', (e) => this.handleSquareHover(e, row, col));
                square.addEventListener('mouseleave', () => this.handleSquareLeave());
                square.addEventListener('click', () => this.handleSquareClick(row, col));
                board.appendChild(square);
            }
        }
        
        // Restore last move highlighting if it exists
        if (this.lastMoveHighlight) {
            this.restoreLastMoveHighlighting();
        }
    }
    
    restoreLastMoveHighlighting() {
        if (!this.lastMoveHighlight) return;
        
        const { from, to } = this.lastMoveHighlight;
        const fromSquare = document.querySelector(`[data-row="${from[0]}"][data-col="${from[1]}"]`);
        const toSquare = document.querySelector(`[data-row="${to[0]}"][data-col="${to[1]}"]`);
        
        if (fromSquare && toSquare) {
            fromSquare.classList.add('last-move');
            toSquare.classList.add('last-move');
        }
    }
    
    async loadTextureSet(textureSet = 'classic') {
        this.currentTextureSet = textureSet;
        this.pieceImages = {
            'pawn': { 'white': null, 'black': null },
            'rook': { 'white': null, 'black': null },
            'knight': { 'white': null, 'black': null },
            'bishop': { 'white': null, 'black': null },
            'queen': { 'white': null, 'black': null },
            'king': { 'white': null, 'black': null }
        };

        const loadImage = (src) => {
            return new Promise((resolve, reject) => {
                const img = new Image();
                img.onload = () => resolve(img);
                img.onerror = () => reject(new Error(`Failed to load image: ${src}`));
                img.src = src;
            });
        };

        try {
            for (const type in this.pieceImages) {
                for (const color in this.pieceImages[type]) {
                    const path = `resources/pieces/${textureSet}/${type}_${color}.png`;
                    this.pieceImages[type][color] = await loadImage(path);
                }
            }
            console.log(`Loaded texture set: ${textureSet}`);
            return true;
        } catch (error) {
            console.error('Failed to load texture set:', error);
            return false;
        }
    }

    getPieceElement(type, color, abilities = []) {
        const pieceContainer = document.createElement('div');
        pieceContainer.className = 'piece-container';
        
        const pieceElement = document.createElement('div');
        pieceElement.className = 'chess-piece';
        
        if (this.pieceImages?.[type]?.[color]) {
            // Create an img element if we have the image loaded
            const img = document.createElement('img');
            img.src = this.pieceImages[type][color].src;
            img.alt = `${color} ${type}`;
            
            // Make piece size relative to square size
            img.style.width = '80%';  // Use percentage of square size
            img.style.height = '80%';
            img.style.display = 'block';
            img.style.objectFit = 'contain';
            pieceElement.appendChild(img);
        } else {
            // Fallback to unicode symbols if images aren't loaded
            const symbols = {
                'pawn': { 'white': '♙', 'black': '♟' },
                'rook': { 'white': '♖', 'black': '♜' },
                'knight': { 'white': '♘', 'black': '♞' },
                'bishop': { 'white': '♗', 'black': '♝' },
                'queen': { 'white': '♕', 'black': '♛' },
                'king': { 'white': '♔', 'black': '♚' }
            };
            pieceElement.textContent = symbols[type]?.[color] || '?';
        }

        // Add abilities container
        if (abilities && abilities.length > 0) {
            const abilitiesContainer = document.createElement('div');
            abilitiesContainer.className = 'abilities-container';
            
            // Handle rotation for black player's view
            if (this.playerColor === 'black') {
                abilitiesContainer.style.transform = 'rotate(180deg)';
                abilitiesContainer.style.bottom = 'auto';
                abilitiesContainer.style.top = '0';
            }

            // Filter out the piece's own type from abilities
            const extraAbilities = abilities.filter(ability => ability.toLowerCase() !== type.toLowerCase());

            // Add ability icons
            extraAbilities.forEach(ability => {
                if (this.pieceImages?.[ability.toLowerCase()]?.[color]) {
                    const abilityIcon = document.createElement('img');
                    abilityIcon.src = this.pieceImages[ability.toLowerCase()][color].src;
                    abilityIcon.alt = ability;
                    abilityIcon.style.height = '100%';
                    abilityIcon.style.width = '20%'; // Space for 5 icons with gaps
                    abilityIcon.style.opacity = '0.9';
                    abilityIcon.style.objectFit = 'contain';
                    abilitiesContainer.appendChild(abilityIcon);
                }
            });

            if (extraAbilities.length > 0) {
                pieceContainer.appendChild(pieceElement);
                pieceContainer.appendChild(abilitiesContainer);
            } else {
                pieceContainer.appendChild(pieceElement);
            }
        } else {
            pieceContainer.appendChild(pieceElement);
        }
        
        return pieceContainer;
    }
    
    handleSquareHover(e, row, col) {
        const piece = this.gameState.board[row][col];
        if (piece) {
            this.showPieceAbilities(piece.abilities);
        }
    }
    
    handleSquareLeave() {
        // Clear abilities display when not hovering over a piece
        const abilitiesDisplay = document.getElementById('abilities-display');
        abilitiesDisplay.innerHTML = '<div class="ability-hint">Hover over a piece to see its abilities</div>';
    }
    
    handleSquareClick(row, col) {
        // Always check if it's the current player's turn first
        if (!this.isCurrentPlayerTurn()) {
            console.log("Not your turn!");
            return;
        }

        if (this.selectedSquare) {
            // Check if clicking on a possible move
            const isPossibleMove = this.possibleMoves.some(move => 
                move.row === row && move.col === col
            );
            
            if (isPossibleMove) {
                // Valid move - attempt it
                this.attemptMove(this.selectedSquare, { row, col });
            } else {
                // Invalid move - cancel selection
                this.clearSelection();
                // If clicking on another piece, select it instead
                const piece = this.gameState.board[row][col];
                if (piece && this.isCurrentPlayerPiece(piece)) {
                    this.selectSquare(row, col);
                }
            }
        } else {
            // Select piece if it belongs to current player
            const piece = this.gameState.board[row][col];
            if (piece && this.isCurrentPlayerPiece(piece)) {
                this.selectSquare(row, col);
            }
        }
    }
    
    isCurrentPlayerTurn() {
        if (!this.gameState) return false;
        // Check if current turn matches the player's color
        // This assumes we know which player we are - we'll need to track this
        return this.gameState.current_turn === this.playerColor;
    }
    
    isCurrentPlayerPiece(piece) {
        if (!this.gameState || !piece) return false;
        return piece.color === this.gameState.current_turn;
    }
    
    async selectSquare(row, col) {
        const piece = this.gameState.board[row][col];
        if (piece && this.isCurrentPlayerPiece(piece)) {
            this.selectedSquare = { row, col };
            this.highlightSelectedSquare();
            this.showPieceAbilities(piece.abilities);
            // Clear previous moves
            this.clearPossibleMoves();
            // Always use cached valid moves from server
            const key = `${row},${col}`;
            this.possibleMoves = this.allValidMoves?.get(key) || [];
            this.showPossibleMoves(row, col);
        }
    }
    
    attemptMove(from, to) {
        this.sendMessage({
            type: 'move_piece',
            from: [from.row, from.col],
            to: [to.row, to.col]
        });
        // Clear selection and cached moves since they will change after the move
        this.clearSelection();
        this.allValidMoves = null;
    }
    
    highlightSelectedSquare() {
        // Remove previous selection
        document.querySelectorAll('.chess-square').forEach(square => {
            square.classList.remove('selected');
        });
        
        // Highlight selected square
        if (this.selectedSquare) {
            const square = document.querySelector(`[data-row="${this.selectedSquare.row}"][data-col="${this.selectedSquare.col}"]`);
            if (square) {
                square.classList.add('selected');
            }
        }
    }
    
    showPossibleMoves(row, col) {
        this.clearPossibleMoves();
        // Debug print: show possible moves and selected square
        console.log('showPossibleMoves called for:', row, col);
        console.log('possibleMoves:', this.possibleMoves);
        if (!this.possibleMoves || this.possibleMoves.length === 0) {
            // If no valid moves, show a toast message
            //this.toast('No valid moves available for this piece');
            return;
        }
        
        // Highlight all possible move squares the same way
        this.possibleMoves.forEach(move => {
            const square = document.querySelector(`[data-row="${move.row}"][data-col="${move.col}"]`);
            if (square) {
                square.classList.add('possible-move');
            }
        });
    }
    
    calculatePossibleMoves(piece, fromRow, fromCol) {
        const moves = [];
        
        // Calculate moves for each ability the piece has
        for (const ability of piece.abilities) {
            const abilityMoves = this.calculateMovesForAbility(piece, fromRow, fromCol, ability);
            moves.push(...abilityMoves);
        }
        
        // Remove duplicates
        const uniqueMoves = moves.filter((move, index, self) => 
            index === self.findIndex(m => m.row === move.row && m.col === move.col)
        );
        
        return uniqueMoves;
    }
    
    calculateMovesForAbility(piece, fromRow, fromCol, ability) {
        const moves = [];
        
        if (ability === 'pawn') {
            moves.push(...this.calculatePawnMoves(piece, fromRow, fromCol));
        } else if (ability === 'rook') {
            moves.push(...this.calculateRookMoves(piece, fromRow, fromCol));
        } else if (ability === 'knight') {
            moves.push(...this.calculateKnightMoves(piece, fromRow, fromCol));
        } else if (ability === 'bishop') {
            moves.push(...this.calculateBishopMoves(piece, fromRow, fromCol));
        } else if (ability === 'queen') {
            moves.push(...this.calculateQueenMoves(piece, fromRow, fromCol));
        } else if (ability === 'king') {
            moves.push(...this.calculateKingMoves(piece, fromRow, fromCol));
        }
        
        return moves;
    }
    
    calculatePawnMoves(piece, fromRow, fromCol) {
        const moves = [];
        // FIXED: Match server direction logic
        const direction = piece.color === 'white' ? -1 : 1;
        const startRow = piece.color === 'white' ? 6 : 1;
        
        // Forward move (one square)
        const forwardRow = fromRow + direction;
        if (forwardRow >= 0 && forwardRow < 8) {
            const forwardPiece = this.gameState.board[forwardRow][fromCol];
            if (!forwardPiece) {
                moves.push({ row: forwardRow, col: fromCol });
            }
        }
        
        // Double forward move from start position
        if (fromRow === startRow) {
            const doubleForwardRow = fromRow + 2 * direction;
            if (doubleForwardRow >= 0 && doubleForwardRow < 8) {
                const forwardPiece = this.gameState.board[forwardRow][fromCol];
                const doubleForwardPiece = this.gameState.board[doubleForwardRow][fromCol];
                if (!forwardPiece && !doubleForwardPiece) {
                    moves.push({ row: doubleForwardRow, col: fromCol });
                }
            }
        }
        
        // Diagonal captures
        for (const colOffset of [-1, 1]) {
            const toRow = fromRow + direction;
            const toCol = fromCol + colOffset;
            if (toRow >= 0 && toRow < 8 && toCol >= 0 && toCol < 8) {
                const targetPiece = this.gameState.board[toRow][toCol];
                if (targetPiece && targetPiece.color !== piece.color) {
                    moves.push({ row: toRow, col: toCol });
                }
                // Check for en passant
                else if (this.gameState.en_passant_target && 
                        this.gameState.en_passant_target[0] === toRow && 
                        this.gameState.en_passant_target[1] === toCol) {
                    moves.push({ row: toRow, col: toCol });
                }
            }
        }
        
        return moves;
    }
    
    calculateRookMoves(piece, fromRow, fromCol) {
        const moves = [];
        const directions = [[-1, 0], [1, 0], [0, -1], [0, 1]];
        
        for (const [rowDir, colDir] of directions) {
            for (let i = 1; i < 8; i++) {
                const toRow = fromRow + i * rowDir;
                const toCol = fromCol + i * colDir;
                
                if (toRow < 0 || toRow >= 8 || toCol < 0 || toCol >= 8) break;
                
                const targetPiece = this.gameState.board[toRow][toCol];
                if (!targetPiece) {
                    moves.push({ row: toRow, col: toCol });
                } else {
                    if (targetPiece.color !== piece.color) {
                        moves.push({ row: toRow, col: toCol });
                    }
                    break;
                }
            }
        }
        
        return moves;
    }
    
    calculateKnightMoves(piece, fromRow, fromCol) {
        const moves = [];
        const knightMoves = [
            [-2, -1], [-2, 1], [-1, -2], [-1, 2],
            [1, -2], [1, 2], [2, -1], [2, 1]
        ];
        
        for (const [rowOffset, colOffset] of knightMoves) {
            const toRow = fromRow + rowOffset;
            const toCol = fromCol + colOffset;
            
            if (toRow >= 0 && toRow < 8 && toCol >= 0 && toCol < 8) {
                const targetPiece = this.gameState.board[toRow][toCol];
                if (!targetPiece || targetPiece.color !== piece.color) {
                    moves.push({ row: toRow, col: toCol });
                }
            }
        }
        
        return moves;
    }
    
    calculateBishopMoves(piece, fromRow, fromCol) {
        const moves = [];
        const directions = [[-1, -1], [-1, 1], [1, -1], [1, 1]];
        
        for (const [rowDir, colDir] of directions) {
            for (let i = 1; i < 8; i++) {
                const toRow = fromRow + i * rowDir;
                const toCol = fromCol + i * colDir;
                
                if (toRow < 0 || toRow >= 8 || toCol < 0 || toCol >= 8) break;
                
                const targetPiece = this.gameState.board[toRow][toCol];
                if (!targetPiece) {
                    moves.push({ row: toRow, col: toCol });
                } else {
                    if (targetPiece.color !== piece.color) {
                        moves.push({ row: toRow, col: toCol });
                    }
                    break;
                }
            }
        }
        
        return moves;
    }
    
    calculateQueenMoves(piece, fromRow, fromCol) {
        // Queen moves like rook + bishop
        return [
            ...this.calculateRookMoves(piece, fromRow, fromCol),
            ...this.calculateBishopMoves(piece, fromRow, fromCol)
        ];
    }
    
    calculateKingMoves(piece, fromRow, fromCol) {
        const moves = [];
        const directions = [
            [-1, -1], [-1, 0], [-1, 1],
            [0, -1], [0, 1],
            [1, -1], [1, 0], [1, 1]
        ];
        
        for (const [rowOffset, colOffset] of directions) {
            const toRow = fromRow + rowOffset;
            const toCol = fromCol + colOffset;
            
            if (toRow >= 0 && toRow < 8 && toCol >= 0 && toCol < 8) {
                const targetPiece = this.gameState.board[toRow][toCol];
                if (!targetPiece || targetPiece.color !== piece.color) {
                    moves.push({ row: toRow, col: toCol });
                }
            }
        }
        // Castling hints (client-side approximation; server validates strictly)
        const kingHasMoved = !!this.getPieceField(fromRow, fromCol, 'has_moved');
        if (!kingHasMoved) {
            // Short castle (king side): rook at col 7
            if (this.canCastle(fromRow, fromCol, 1)) {
                moves.push({ row: fromRow, col: fromCol + 2 });
            }
            // Long castle (queen side): rook at col 0
            if (this.canCastle(fromRow, fromCol, -1)) {
                moves.push({ row: fromRow, col: fromCol - 2 });
            }
        }
        return moves;
    }

    getPieceField(row, col, field) {
        const piece = this.gameState?.board?.[row]?.[col];
        return piece ? piece[field] : undefined;
    }

    canCastle(row, kingCol, direction) {
        // direction: +1 for king-side, -1 for queen-side
        const rookCol = direction === 1 ? 7 : 0;
        const rook = this.gameState.board[row][rookCol];
        if (!rook || rook.type !== 'rook' || rook.color !== this.gameState.board[row][kingCol].color) return false;
        if (rook.has_moved) return false;
        // squares between king and rook must be empty
        let c = kingCol + direction;
        while (c !== rookCol) {
            if (this.gameState.board[row][c]) return false;
            c += direction;
        }
        // Do not attempt if king currently in check per server hint; we only have king-in-check flags for current king
        // Note: server will still validate passing-through check, so we only provide a hint
        return true;
    }
    
    clearPossibleMoves() {
        document.querySelectorAll('.chess-square').forEach(square => {
            square.classList.remove('possible-move');
        });
    }
    
    clearSelection() {
        this.selectedSquare = null;
        this.possibleMoves = [];
        this.clearPossibleMoves();
        document.querySelectorAll('.chess-square').forEach(square => {
            square.classList.remove('selected');
        });
    }
    
    highlightLastMove(from, to) {
        this.clearLastMoveHighlighting();
        const fromSquare = document.querySelector(`[data-row="${from[0]}"][data-col="${from[1]}"]`);
        const toSquare = document.querySelector(`[data-row="${to[0]}"][data-col="${to[1]}"]`);
        if (fromSquare) fromSquare.classList.add('last-move');
        if (toSquare) toSquare.classList.add('last-move');
    }

    animateMove(from, to) {
        const fromSquare = document.querySelector(`[data-row="${from[0]}"][data-col="${from[1]}"]`);
        const toSquare = document.querySelector(`[data-row="${to[0]}"][data-col="${to[1]}"]`);
        const board = document.getElementById('chess-board');
        const isBoardRotated = board.classList.contains('rotated');

        if (fromSquare && toSquare) {
            const piece = fromSquare.querySelector('.chess-piece');
            if (piece) {
                // Create clone for animation
                const movingPiece = piece.cloneNode(true);
                board.appendChild(movingPiece); // Add to board for absolute positioning
                
                // Get exact positions
                const fromRect = fromSquare.getBoundingClientRect();
                const toRect = toSquare.getBoundingClientRect();
                const boardRect = board.getBoundingClientRect();

                // Position the piece absolutely within the board
                movingPiece.style.position = 'absolute';
                movingPiece.style.zIndex = '1000';
                movingPiece.style.margin = '0';
                movingPiece.style.padding = '0';
                movingPiece.style.display = 'flex';
                movingPiece.style.alignItems = 'center';
                movingPiece.style.justifyContent = 'center';
                
                // Set initial position relative to board
                const startX = fromRect.left - boardRect.left;
                const startY = fromRect.top - boardRect.top;
                movingPiece.style.left = startX + 'px';
                movingPiece.style.top = startY + 'px';
                movingPiece.style.width = fromRect.width + 'px';
                movingPiece.style.height = fromRect.height + 'px';

                // Handle piece rotation when board is rotated
                if (isBoardRotated) {
                    movingPiece.style.transform = 'rotate(180deg)';
                }

                // Hide original piece
                piece.style.opacity = '0';

                // Calculate final position
                const endX = toRect.left - boardRect.left;
                const endY = toRect.top - boardRect.top;

                // Animate
                requestAnimationFrame(() => {
                    movingPiece.style.transition = 'all 0.3s ease';
                    if (isBoardRotated) {
                        movingPiece.style.transform = 'rotate(180deg)';
                    }
                    movingPiece.style.left = endX + 'px';
                    movingPiece.style.top = endY + 'px';
                });

                // Cleanup after animation
                setTimeout(() => {
                    movingPiece.remove();
                    piece.style.opacity = '1';
                    this.highlightLastMove(from, to);
                }, 300);
            }
        }
    }

    clearLastMoveHighlighting() {
        document.querySelectorAll('.chess-square').forEach(square => {
            square.classList.remove('last-move');
        });
        // Do NOT clear this.lastMoveHighlight here, so highlight persists after board re-render
    }
    
    showPieceAbilities(abilities) {
        const abilitiesDisplay = document.getElementById('abilities-display');
        if (!abilitiesDisplay) return; // Skip if element doesn't exist
        
        abilitiesDisplay.innerHTML = '';
        if (!abilities) return; // Skip if no abilities provided
        
        abilities.forEach(ability => {
            const abilityElement = document.createElement('div');
            abilityElement.className = 'ability-item';
            abilityElement.textContent = ability.charAt(0).toUpperCase() + ability.slice(1);
            abilitiesDisplay.appendChild(abilityElement);
        });
    }
    
    updateMoveHistory() {
        const moveList = document.getElementById('move-list');
        moveList.innerHTML = '';
        
        if (this.gameState && this.gameState.move_history) {
            this.gameState.move_history.forEach((move, index) => {
                const moveElement = document.createElement('div');
                moveElement.className = 'move-item';
                moveElement.innerHTML = `
                    <span>${index + 1}. ${move.piece} ${this.positionToString(move.from)} → ${this.positionToString(move.to)}</span>
                    ${move.captured ? `<span>Captured: ${move.captured}</span>` : ''}
                `;
                moveList.appendChild(moveElement);
            });
        }
    }
    
    positionToString(pos) {
        const col = String.fromCharCode(97 + pos[1]); // a-h
        const row = 8 - pos[0]; // 1-8
        return `${col}${row}`;
    }
    
    updateConnectionStatus(connected) {
        const statusElement = document.getElementById('connection-status');
        if (connected) {
            statusElement.textContent = 'Connected';
            statusElement.className = 'connection-status connected';
        } else {
            statusElement.textContent = 'Disconnected';
            statusElement.className = 'connection-status disconnected';
        }
    }
    
    showError(message) {
        document.getElementById('error-message').textContent = message;
        document.getElementById('error-modal').classList.add('active');
    }
    
    hideError() {
        document.getElementById('error-modal').classList.remove('active');
    }

    handlePlayerDisconnection(data) {
        // Get disconnect time and auto-abort time
        const { playerId, disconnect_time, abort_time } = data;
        
        // Update opponent's connection status
        if (this.currentScreen === 'game-screen') {
            const opponentColor = this.playerColor === 'white' ? 'black' : 'white';
            const playerClockDiv = document.querySelector(`.${opponentColor}-clock`);
            
            // Update connection status
            const statusElement = document.getElementById(`${opponentColor}-connection-status`);
            if (statusElement) {
                statusElement.textContent = 'Disconnected';
                statusElement.classList.remove('connected');
                statusElement.classList.add('disconnected');
            }

            // Create or update disconnection status under timer
            let disconnectionStatus = playerClockDiv.querySelector('.disconnection-status');
            if (!disconnectionStatus) {
                disconnectionStatus = document.createElement('div');
                disconnectionStatus.className = 'disconnection-status';
                playerClockDiv.appendChild(disconnectionStatus);
            }

            // Clear any existing countdown
            if (this.disconnectionTimer) {
                clearInterval(this.disconnectionTimer);
            }

            const updateCountdown = () => {
                const now = Math.floor(Date.now() / 1000);
                const remainingTime = Math.max(0, Math.floor(abort_time - now));
                
                if (remainingTime > 0) {
                    disconnectionStatus.textContent = `Auto-abort in ${remainingTime} seconds`;
                } else {
                    clearInterval(this.disconnectionTimer);
                    if (disconnectionStatus) {
                        disconnectionStatus.remove();
                    }
                }
            };

            updateCountdown();
            this.disconnectionTimer = setInterval(updateCountdown, 1000);
        }
    }

    handleSearchDisconnected() {
        // If we're searching and get disconnected, cancel search and show error
        if (this.currentScreen === 'search-game' || this.isSearchingGame) {
            this.isSearchingGame = false;
            document.getElementById('search-game-form').style.display = 'block';
            document.getElementById('searching-status').style.display = 'none';
            this.showScreen('main-menu');
            this.showError('Search cancelled due to connection issues');
        }
    }
    
    resetLobbyState() {
        this.currentLobby = null;
        this.isOwner = false;
        this.gameState = null;
        this.selectedSquare = null;
        this.possibleMoves = [];
    }
    
    initializeGameControls() {
        // Initialize any game-specific controls
        this.updateMoveHistory();
    }

    checkConnection() {
        if (!this.websocket || this.websocket.readyState !== WebSocket.OPEN) {
            this.handleDisconnection();
        }
    }

    handleDisconnection() {
        // Update connection status
        this.updateConnectionStatus(false);

        // Handle search disconnection
        if (this.currentScreen === 'search-game') {
            this.showError('Lost connection to server. Please try again.');
            this.showScreen('main-menu');
            return;
        }

        // Handle game disconnection
        if (this.currentScreen === 'game-screen') {
            const playerStatus = this.playerColor === 'white' ? 
                document.getElementById('white-connection-status') : 
                document.getElementById('black-connection-status');
            
            if (playerStatus) {
                playerStatus.className = 'player-connection-status disconnected';
                playerStatus.textContent = 'Disconnected';
            }

            // Start disconnect timer if not already started
            if (!this.disconnectTimer) {
                this.disconnectTimeLeft = 40;
                this.disconnectTimer = setInterval(() => {
                    this.disconnectTimeLeft--;
                    playerStatus.textContent = `Disconnected (Auto resign in ${this.disconnectTimeLeft}s)`;
                    
                    if (this.disconnectTimeLeft <= 0) {
                        clearInterval(this.disconnectTimer);
                        this.disconnectTimer = null;
                        this.resign();
                    }
                }, 1000);
            }
        }
    }

    reconnectPlayer() {
        if (this.currentScreen === 'game-screen') {
            const playerStatus = this.playerColor === 'white' ? 
                document.getElementById('white-connection-status') : 
                document.getElementById('black-connection-status');
            
            if (playerStatus) {
                playerStatus.className = 'player-connection-status connected';
                playerStatus.textContent = 'Connected';
            }

            if (this.disconnectTimer) {
                clearInterval(this.disconnectTimer);
                this.disconnectTimer = null;
            }
        }
    }

    updatePlayerNames(whiteName, blackName) {
        document.getElementById('white-player-name').textContent = whiteName;
        document.getElementById('black-player-name').textContent = blackName;
    }
}

// Initialize the app when the page loads
document.addEventListener('DOMContentLoaded', () => {
    new ChessApp();
});
