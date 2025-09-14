import asyncio
import datetime
from dataclasses import asdict
from server.core.game import ChessGame
from server.core.enums import Color
from server.core.state import GlobalState

class GameHandler:
    def __init__(self):
        self.state = GlobalState.get_instance()

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
            
            # Calculate valid moves for the new state
            valid_moves = game.calculate_moves()
            print(f"Calculated {len(valid_moves)} valid move groups for {game.current_turn.value}")
            
            # Convert moves for client
            client_moves = {
                f"{row},{col}": [(to_row, to_col) for to_row, to_col in moves]
                for (row, col), moves in valid_moves.items()
            }
            
            # Get updated game state
            new_state = game.get_board_state()
            print(f"New state current_turn: {new_state['current_turn']}")
            # Always add valid moves to game state for frontend
            new_state['valid_moves'] = client_moves
            # Merge clock information
            if game_state.get('clock'):
                new_state['clock'] = game_state['clock']
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