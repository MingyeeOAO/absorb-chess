import datetime
import uuid
from typing import Dict, Optional
from server.core.models import Lobby, Player
from server.core.enums import Color
from server.core.game import ChessGame
from server.core.state import GlobalState
import random
import string

class LobbyHandler:
    def __init__(self, connection_manager):
        self.connection_manager = connection_manager
        self.state = GlobalState.get_instance()
    
    def generate_lobby_code(self) -> str:
        """Generate a unique 6-character lobby code using letters and numbers"""
        
        # Define the characters to use (uppercase letters and numbers)
        chars = string.ascii_uppercase + string.digits 
        
        while True:
            # Generate a 6-character code
            code = ''.join(random.choices(chars, k=6))
            if not self.state.lobby_exists(code):
                return code

    async def create_lobby(self, client_id: str, websocket, data: dict) -> dict:
        """Create a new lobby"""
        lobby_code = self.generate_lobby_code()
        player_name = data.get('player_name', 'Player')
        
        lobby = Lobby(
            code=lobby_code,
            owner_id=client_id,
            players=[Player(client_id, player_name, Color.WHITE, websocket)],
            game_state=None,
            settings=data.get('settings', {}),
            created_at=datetime.datetime.now()
        )
        
        self.state.add_lobby(lobby_code, lobby)
        
        return {
            'type': 'lobby_created',
            'lobby_code': lobby_code,
            'player_id': client_id,
            'is_owner': True,
            'lobby_data': {
                'lobby_code': lobby_code,
                'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} 
                          for p in lobby.players],
                'settings': lobby.settings,
                'is_owner': True
            }
        }
    
    async def leave_lobby(self, client_id: str, websocket, data: dict) -> Optional[dict]:
        """Handle a player leaving a lobby"""
        lobby = self.get_lobby_by_client(client_id)
        if not lobby:
            return None
            
        # Remove player from lobby
        lobby.players = [p for p in lobby.players if p.id != client_id]
        
        if not lobby.players:
            # If lobby is empty, delete it
            self.state.remove_lobby(lobby.code)
            return {
                'type': 'lobby_closed',
                'lobby_code': lobby.code
            }
            
        # If owner left, transfer ownership to next player
        if client_id == lobby.owner_id and lobby.players:
            lobby.owner_id = lobby.players[0].id
            
        # Notify remaining players
        return {
            'type': 'lobby_update',
            'lobby_code': lobby.code,
            'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} 
                       for p in lobby.players],
            'settings': lobby.settings
        }
            
    async def handle_disconnect(self, client_id: str) -> Optional[dict]:
        """Handle a client disconnection"""
        return await self.leave_lobby(client_id, None, {})

    async def join_lobby(self, client_id: str, websocket, data: dict) -> Optional[dict]:
        """Join an existing lobby"""
        lobby_code = data.get('lobby_code')
        if not self.state.lobby_exists(lobby_code):
            return None
            
        lobby = self.state.get_lobby(lobby_code)
        if len(lobby.players) >= 2:
            return None
            
        player_name = data.get('player_name', 'Player')
        player_color = Color.BLACK if len(lobby.players) == 1 else Color.WHITE
        player = Player(client_id, player_name, player_color, websocket)
        lobby.players.append(player)
        
        # Add player to lobby mapping
        self.state.add_player_to_lobby(client_id, lobby_code)
        
        # Create the common lobby data structure
        lobby_data = {
            'lobby_code': lobby_code,
            'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} 
                       for p in lobby.players],
            'settings': lobby.settings
        }

        # Message for broadcasting to existing players (excluding the new player)
        broadcast_message = {
            'type': 'lobby_update',
            'lobby_code': lobby_code,
            'players': lobby_data['players'],
            'settings': lobby.settings,
            'is_owner': False  # Existing players keep their owner status unchanged
        }

        # Message specifically for the joining player
        join_message = {
            'type': 'lobby_joined',
            'lobby_code': lobby_code,
            'player_id': client_id,
            'is_owner': client_id == lobby.owner_id,  # Set owner status based on lobby.owner_id
            'lobby_data': {
                'lobby_code': lobby_code,
                'players': lobby_data['players'],
                'settings': lobby.settings,
                'is_owner': client_id == lobby.owner_id  # Same owner status in lobby_data
            }
        }

        return {
            'broadcast': broadcast_message,
            'join': join_message
        }

    async def start_game(self, client_id: str, websocket, data: dict) -> Optional[dict]:
        """Start a game in a lobby"""
        for lobby in self.state.get_all_lobbies().values():
            if lobby.owner_id == client_id and len(lobby.players) == 2:
                game = ChessGame()
                
                # Calculate initial valid moves
                valid_moves = game.calculate_moves()
                client_moves = {
                    f"{row},{col}": [(to_row, to_col) for to_row, to_col in moves]
                    for (row, col), moves in valid_moves.items()
                }
                
                # Get game state with clocks
                lobby.game_state = game.get_board_state()
                
                # Setup clock information based on lobby settings
                time_minutes = lobby.settings.get('time_minutes', 0)
                increment_seconds = lobby.settings.get('time_increment_seconds', 0)
                
                # Convert to milliseconds
                time_ms = time_minutes * 60 * 1000 if time_minutes else None
                increment_ms = increment_seconds * 1000 if increment_seconds else 0
                
                # Add clock information to game state
                import time
                now_ms = int(time.time() * 1000)
                lobby.game_state['clock'] = {
                    'white_ms': time_ms if time_ms is not None else 0,
                    'black_ms': time_ms if time_ms is not None else 0,
                    'increment_ms': increment_ms,
                    'last_turn_start': now_ms
                }
                
                # Add game state info
                lobby.game_state['valid_moves'] = client_moves
                lobby.game_state['current_turn'] = 'white'
                lobby.game_state['game_over'] = False
                lobby.game_state['winner'] = None
                
                # Send game started message to both players
                for player in lobby.players:
                    # Create player-specific response
                    response = {
                        'type': 'game_started',
                        'game_state': lobby.game_state,
                        'player_color': player.color.value,
                        'lobby_data': {
                            'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} 
                                       for p in lobby.players],
                            'settings': lobby.settings,
                            'lobby_code': lobby.code
                        }
                    }
                    await self.connection_manager.send_message(player.websocket, response)
                
                # Return None since we've handled the messaging
                return None
        return None

    def get_lobby_by_client(self, client_id: str) -> Optional[Lobby]:
        """Find a lobby containing a specific client"""
        return self.state.get_lobby_by_client(client_id)
    
    
    async def leave_lobby(self, client_id: str):
        # Find lobby containing this client
        lobby = self.state.get_lobby_by_client(client_id)
        if lobby:
            # Remove player from lobby
            lobby.players = [p for p in lobby.players if p.id != client_id]
            
            # Remove player from lobby mapping
            self.state.remove_player_from_lobby(client_id)
            
            # Notify remaining players
            if lobby.players:  # If there are still players left
                for player in lobby.players:
                    await self.connection_manager.send_message(player.websocket, {
                        'type': 'lobby_update',
                        'lobby_code': lobby.code,
                        'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} for p in lobby.players],
                        'settings': lobby.settings,
                        'is_owner': player.id == lobby.owner_id
                    })
            else:
                # Clean up empty lobby
                self.state.remove_lobby(lobby.code)