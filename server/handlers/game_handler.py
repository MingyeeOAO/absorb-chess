import asyncio
import datetime
from dataclasses import asdict
from server.core.game import ChessGame
from server.core.enums import Color, PieceType
from server.core.state import GlobalState

class GameHandler:
    def __init__(self):
        self.state = GlobalState.get_instance()
        self.draw_offer_history = {}  # Track draw offer rate limiting

    async def handle_move(self, client_id: str, websocket, game_state: dict, data: dict):
        """Handle a move request"""
        from_pos = tuple(data['from'])
        to_pos = tuple(data['to'])
        
        # Create a game instance from the game state
        game = ChessGame()
        game.load_from_state(game_state)  # We need to add this method to ChessGame
        
        if game_state.get('game_over'):
            return {'type': 'invalid_move', 'reason': 'Game is over'}
            
        # Update clock for the last turn if it exists
        if game_state.get('clock'):
            now = datetime.datetime.now().isoformat()
            clock = game_state['clock']
            last_turn = clock.get('last_turn_start')
            
            if last_turn:
                # Support both ISO string and float timestamps
                now_dt = datetime.datetime.now()
                if isinstance(last_turn, str):
                    try:
                        last_dt = datetime.datetime.fromisoformat(last_turn)
                        elapsed = (now_dt - last_dt).total_seconds() * 1000
                    except Exception:
                        # fallback: treat as float ms
                        elapsed = now_dt.timestamp() * 1000 - float(last_turn)
                else:
                    # treat as float ms
                    elapsed = now_dt.timestamp() * 1000 - float(last_turn)
                # Update the time for the player who just completed their turn
                if game_state['current_turn'] == 'white':
                    clock['black_ms'] = max(0, clock['black_ms'] - elapsed)
                    if clock['black_ms'] == 0:
                        game_state['game_over'] = True
                        game_state['winner'] = 'white'
                else:
                    clock['white_ms'] = max(0, clock['white_ms'] - elapsed)
                    if clock['white_ms'] == 0:
                        game_state['game_over'] = True
                        game_state['winner'] = 'black'
                # Add increment
                if clock['increment_ms']:
                    if game_state['current_turn'] == 'white':
                        clock['black_ms'] += clock['increment_ms']
                    else:
                        clock['white_ms'] += clock['increment_ms']
            
            # Update last turn start time
            clock['last_turn_start'] = now
        
        if game.move_piece(from_pos, to_pos):
            print(f"Move successful! Turn after move: {game.current_turn.value}")
            
            # Get updated game state
            new_state = game.get_board_state()
            print(f"New state current_turn: {new_state['current_turn']}")
            
            # Merge clock information
            if game_state.get('clock'):
                new_state['clock'] = game_state['clock']
            
            # Check for promotion first
            if game.promotion_pending:
                # Send promotion pending message
                return {
                    'type': 'promotion_pending',
                    'game_state': new_state
                }
            
            # Calculate valid moves for the new state
            valid_moves = game.calculate_moves()
            print(f"Calculated {len(valid_moves)} valid move groups for {game.current_turn.value}")
            
            # Convert moves for client
            client_moves = {
                f"{row},{col}": [(to_row, to_col) for to_row, to_col in moves]
                for (row, col), moves in valid_moves.items()
            }
            
            # Always add valid moves to game state for frontend
            new_state['valid_moves'] = client_moves
            
            # Check for game over (checkmate/stalemate)
            if not valid_moves:
                reason = None
                # Check if current turn king is in check
                if ((new_state.get('current_turn') == 'white' and new_state.get('white_king_in_check')) or 
                    (new_state.get('current_turn') == 'black' and new_state.get('black_king_in_check'))):
                    reason = 'checkmate'
                    new_state['winner'] = 'black' if new_state.get('current_turn') == 'white' else 'white'
                else:
                    reason = 'stalemate'
                    new_state['winner'] = None  
                return {
                    'type': 'game_over',
                    'reason': reason,
                    'game_state': new_state
                }
            
            # Send updated game state and new valid moves
            return {
                'type': 'move_made',
                'game_state': new_state,
                'valid_moves': client_moves,
                'last_move': {'from': from_pos, 'to': to_pos}
            }
        
        # Move failed - provide detailed debugging information
        from_row, from_col = from_pos
        to_row, to_col = to_pos
        piece = game.get_piece_at(from_row, from_col)
        target_piece = game.get_piece_at(to_row, to_col)
        
        print(f"Move failed! Current turn in game: {game.current_turn.value}")
        print(f"Attempting to move {piece.color.value if piece else 'None'} piece")
        
        # Build detailed error message
        error_details = []
        
        if not piece:
            error_details.append(f"No piece at source position ({from_row},{from_col})")
        else:
            error_details.append(f"Piece at source: {piece.color.value} {piece.type.value}")
            error_details.append(f"Piece abilities: {[a.value for a in piece.abilities]}")
            
            if piece.color != game.current_turn:
                error_details.append(f"Wrong turn - current turn: {game.current_turn.value}, piece color: {piece.color.value}")
        
        if target_piece:
            error_details.append(f"Target piece: {target_piece.color.value} {target_piece.type.value}")
            if piece and target_piece.color == piece.color:
                error_details.append("Cannot capture own piece")
        
        # Check bounds
        if not (0 <= to_row < 8 and 0 <= to_col < 8):
            error_details.append(f"Target position out of bounds: ({to_row},{to_col})")
        
        if from_pos == to_pos:
            error_details.append("Source and target positions are the same")
        
        # Check if move follows piece abilities
        if piece:
            valid_for_abilities = []
            for ability in piece.abilities:
                if game._is_valid_move_for_ability(piece, from_pos, to_pos, ability):
                    valid_for_abilities.append(ability.value)
            
            if not valid_for_abilities:
                error_details.append(f"Move {from_pos} -> {to_pos} not valid for any abilities: {[a.value for a in piece.abilities]}")
            else:
                error_details.append(f"Move valid for abilities: {valid_for_abilities}")
                
                # Check if king would be in check
                # Simulate the move
                captured_piece = game.get_piece_at(to_row, to_col)
                game.board[to_row][to_col] = piece
                game.board[from_row][from_col] = None
                original_position = piece.position
                piece.position = (to_row, to_col)
                king_in_check = game._is_king_in_check(piece.color)
                # Revert
                game.board[from_row][from_col] = piece
                game.board[to_row][to_col] = captured_piece
                piece.position = original_position
                
                if king_in_check:
                    error_details.append("Move would put own king in check")
        
        return {
            'type': 'invalid_move',
            'reason': 'Move validation failed',
            'details': error_details,
            'from': from_pos,
            'to': to_pos,
            'current_turn': game.current_turn.value
        }
    
    async def handle_valid_moves_request(self, client_id: str, websocket, game: ChessGame):
        """Handle request for valid moves"""
        valid_moves = game.calculate_moves()
        # Convert moves for client
        client_moves = {
            f"{row},{col}": [(to_row, to_col) for to_row, to_col in moves]
            for (row, col), moves in valid_moves.items()
        }
        return {
            'type': 'valid_moves',
            'moves': client_moves
        }

    async def handle_resign(self, client_id: str, websocket):
        """Handle player resignation"""
        lobby = self.state.get_lobby_by_client(client_id)
        if not lobby or not lobby.game_state or lobby.game_state.get('game_over'):
            return None
            
        # Find the resigning player
        resigning_player = None
        for player in lobby.players:
            if player.id == client_id:
                resigning_player = player
                break
                
        if not resigning_player:
            return None
            
        # Determine winner as the opponent
        winner_color = 'black' if resigning_player.color.value == 'white' else 'white'
        
        # Update game state
        lobby.game_state['game_over'] = True
        lobby.game_state['winner'] = winner_color
        
        return {
            'type': 'game_over',
            'reason': 'resign',
            'game_state': lobby.game_state
        }

    async def handle_timeout(self, client_id: str, websocket):
        """Handle player timeout"""
        lobby = self.state.get_lobby_by_client(client_id)
        if not lobby or not lobby.game_state or lobby.game_state.get('game_over'):
            return None
            
        # Winner is opponent of current turn
        current_turn = lobby.game_state.get('current_turn')
        winner = 'black' if current_turn == 'white' else 'white'
        
        # Update game state
        lobby.game_state['game_over'] = True
        lobby.game_state['winner'] = winner
        
        return {
            'type': 'game_over',
            'reason': 'timeout',
            'game_state': lobby.game_state
        }

    async def handle_offer_draw(self, client_id: str, websocket):
        """Handle draw offer with rate limiting"""
        now = datetime.datetime.utcnow()
        
        # Rate limit: max 3 offers per minute per player
        history = self.draw_offer_history.setdefault(client_id, [])
        history = [t for t in history if (now - t).total_seconds() <= 60]
        self.draw_offer_history[client_id] = history
        
        if len(history) >= 3:
            return {
                'type': 'draw_offer_rate_limited',
                'retry_after': 60 - int((now - history[0]).total_seconds())
            }
            
        # Record this offer time
        history.append(now)
        self.draw_offer_history[client_id] = history
        
        lobby = self.state.get_lobby_by_client(client_id)
        if not lobby or not lobby.game_state or lobby.game_state.get('game_over'):
            return None
            
        return {
            'type': 'draw_offered',
            'from': client_id
        }

    async def handle_accept_draw(self, client_id: str, websocket):
        """Handle draw acceptance"""
        lobby = self.state.get_lobby_by_client(client_id)
        if not lobby or not lobby.game_state or lobby.game_state.get('game_over'):
            return None
            
        # Update game state
        lobby.game_state['game_over'] = True
        lobby.game_state['winner'] = None
        
        return {
            'type': 'game_over',
            'reason': 'draw',
            'game_state': lobby.game_state
        }

    async def handle_decline_draw(self, client_id: str, websocket, data: dict):
        """Handle draw decline"""
        return {
            'type': 'draw_declined',
            'from': client_id
        }

    async def handle_promotion_choice(self, client_id: str, websocket, data: dict):
        """Handle promotion choice"""
        lobby = self.state.get_lobby_by_client(client_id)
        if not lobby or not lobby.game_state:
            return None
            
        choice = data.get('choice')
        if not choice:
            return None
            
        # Reconstruct game from state
        game = ChessGame()
        game.load_from_state(lobby.game_state)
        
        if not game.promotion_pending:
            return None
            
        if choice.lower() == 'cancel':
            if lobby.game_state.get('promotion_cancel_allowed'):
                if game.cancel_promotion():
                    new_state = game.get_board_state()
                    # Merge clock information
                    if lobby.game_state.get('clock'):
                        new_state['clock'] = lobby.game_state['clock']
                    
                    # Update lobby state
                    lobby.game_state = new_state
                    
                    return {
                        'type': 'promotion_canceled',
                        'game_state': new_state
                    }
            return None
        
        # Apply promotion
        mapping = {
            'queen': PieceType.QUEEN,
            'rook': PieceType.ROOK,
            'bishop': PieceType.BISHOP,
            'knight': PieceType.KNIGHT
        }
        
        new_type = mapping.get(choice.lower())
        if not new_type:
            return None
            
        row = game.promotion_pending['row']
        col = game.promotion_pending['col']
        from_pos = game.promotion_pending.get('from')
        
        if game.apply_promotion(row, col, new_type):
            # apply_promotion already switches turns, no need to do it again
            
            # Calculate valid moves for the new state
            valid_moves = game.calculate_moves()
            client_moves = {
                f"{r},{c}": [(to_row, to_col) for to_row, to_col in moves]
                for (r, c), moves in valid_moves.items()
            }
            
            new_state = game.get_board_state()
            new_state['valid_moves'] = client_moves
            
            # Merge clock information
            if lobby.game_state.get('clock'):
                new_state['clock'] = lobby.game_state['clock']
            
            # Update lobby state
            lobby.game_state = new_state
            
            # Check for game over after promotion
            if not valid_moves:
                reason = None
                if ((new_state.get('current_turn') == 'white' and new_state.get('white_king_in_check')) or 
                    (new_state.get('current_turn') == 'black' and new_state.get('black_king_in_check'))):
                    reason = 'checkmate'
                    new_state['winner'] = 'black' if new_state.get('current_turn') == 'white' else 'white'
                else:
                    reason = 'stalemate'
                    new_state['winner'] = None
                
                return {
                    'type': 'game_over',
                    'reason': reason,
                    'game_state': new_state
                }
                
            return {
                'type': 'promotion_applied',
                'game_state': new_state,
                'from': from_pos,
                'to': (row, col)
            }
        
        return None