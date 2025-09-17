import asyncio
import datetime
import uuid
from typing import Dict, Optional
from server.core.models import Lobby, Player, BotPlayer
from server.core.enums import Color
from server.core.game import ChessGame
from server.core.state import GlobalState
import random
import string
#from server.networking.connection import ConnectionManager
class LobbyHandler:
    def __init__(self, connection_manager ):
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
        with_bot = data.get('with_bot', False)
        
        # Store player name in client state for reconstruction later
        self.state.update_client_state(client_id, 'in_lobby', player_name=player_name, lobby_code=lobby_code)
        
        players = [Player(client_id, player_name, Color.WHITE, websocket)]
        
        # Add bot if requested
        if with_bot:
            bot_id = f"bot_{lobby_code}"
            bot_player = BotPlayer(bot_id, "Chess Bot", Color.BLACK)
            players.append(bot_player)
            # Add bot to client_lobby_map
            self.state.add_player_to_lobby(bot_id, lobby_code, Color.BLACK.value)
        
        lobby = Lobby(
            code=lobby_code,
            owner_id=client_id,
            players=players,
            game_state=None,
            settings=data.get('settings', {}),
            created_at=datetime.datetime.now(),
            has_bot=with_bot
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
                'is_owner': True,
                'has_bot': with_bot
            }
        }
    
    async def leave_lobby(self, client_id: str, websocket, data: dict) -> Optional[dict]:
        """Handle a player leaving a lobby"""
        lobby = self.get_lobby_by_client(client_id)
        if not lobby:
            return None
            
        # Remove player from client_lobby_map in DB (this persists the removal)
        self.state.remove_player_from_lobby(client_id)
        
        # Remove player from client state
        self.state.remove_client_state(client_id)
        
        # Reload lobby to get updated players list
        lobby = self.state.get_lobby(lobby.code)
        
        if not lobby or not lobby.players:
            # If lobby is empty, delete it
            self.state.remove_lobby(lobby.code)
            return {
                'type': 'lobby_closed',
                'lobby_code': lobby.code
            }
            
        # If owner left, transfer ownership to next player and update in DB
        if client_id == lobby.owner_id and lobby.players:
            new_owner_id = lobby.players[0].id
            # Update owner in database
            self.state.update_lobby_owner(lobby.code, new_owner_id)
            lobby.owner_id = new_owner_id
            
        # Notify remaining players
        lobby_update = {
            'type': 'lobby_update',
            'lobby_code': lobby.code,
            'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} 
                       for p in lobby.players],
            'settings': lobby.settings
        }
        # Send lobby update to all remaining players
        for p in lobby.players:
            if hasattr(p, 'websocket') and p.websocket:
                # Add is_owner specific to each player
                player_specific_update = lobby_update.copy()
                player_specific_update['is_owner'] = p.id == lobby.owner_id
                await self.connection_manager.send_message(p.websocket, player_specific_update)
        return lobby_update


    async def join_lobby(self, client_id: str, websocket, data: dict) -> Optional[dict]:
        """Join an existing lobby"""
        lobby_code = data.get('lobby_code')
        if not self.state.lobby_exists(lobby_code):
            return None
            
        lobby = self.state.get_lobby(lobby_code)
        if len(lobby.players) >= 2:
            return None
            
        player_name = data.get('player_name', 'Player')
        
        # Store player name in client state for reconstruction later
        self.state.update_client_state(client_id, 'in_lobby', player_name=player_name, lobby_code=lobby_code)
        
        player_color = Color.BLACK if len(lobby.players) == 1 else Color.WHITE
        player = Player(client_id, player_name, player_color, websocket)
        lobby.players.append(player)
        
        # Add player to lobby mapping with their color
        self.state.add_player_to_lobby(client_id, lobby_code, player_color.value)
        
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
            'settings': lobby.settings
            # Note: is_owner will be set per-player in server.py when broadcasting
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
        lobby = self.state.get_lobby_by_client(client_id)
        if lobby:
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
                self.state.update_lobby_game_state(lobby.code, lobby.game_state)
                
                # Setup clock information based on lobby settings
                time_minutes = lobby.settings.get('time_minutes', 0)
                increment_seconds = lobby.settings.get('time_increment_seconds', 0)
                
                # Convert to milliseconds
                time_ms = time_minutes * 60 * 1000 if time_minutes else None
                increment_ms = increment_seconds * 1000 if increment_seconds else 0
                
                # Add clock information to game state
                # Use UTC time to avoid timezone issues
                now_iso = datetime.datetime.utcnow().isoformat() + 'Z'
                # print(f"[LOBBY DEBUG] Setting last_turn_start to: {now_iso} (type: {type(now_iso)})")
                lobby.game_state['clock'] = {
                    'white_ms': time_ms if time_ms is not None else 0,
                    'black_ms': time_ms if time_ms is not None else 0,
                    'increment_ms': increment_ms,
                    'last_turn_start': now_iso
                }
                # print(f"[LOBBY DEBUG] Clock data set: {lobby.game_state['clock']}")
                self.state.update_lobby_game_state(lobby.code, lobby.game_state)
                
                # Add game state info
                lobby.game_state['valid_moves'] = client_moves
                lobby.game_state['current_turn'] = 'white'
                lobby.game_state['game_over'] = False
                lobby.game_state['winner'] = None
                self.state.update_lobby_game_state(lobby.code, lobby.game_state)
                
                # Send game started message to both players
                for player in lobby.players:
                    # Create player-specific response
                    # print(f"[LOBBY DEBUG] Sending game_state with clock: {lobby.game_state.get('clock')}")
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
                    if hasattr(player, 'websocket') and player.websocket:  # Check for bot players
                        await self.connection_manager.send_message(player.websocket, response)
                
                # Check if bot should make the first move (white always goes first)
                if lobby.has_bot:
                    for player in lobby.players:
                        if isinstance(player, BotPlayer) and player.color.value == 'white':
                            # Schedule bot move after a short delay
                            import asyncio
                            asyncio.create_task(self._schedule_bot_first_move(lobby.code))
                            break
                
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
                
    async def swap_colors(self, client_id: str, websocket, data: dict) -> Optional[dict]:
        """Swap colors of players in lobby (owner only)"""
        lobby = self.get_lobby_by_client(client_id)
        if not lobby or lobby.owner_id != client_id or len(lobby.players) != 2:
            return None
            
        # Swap colors
        lobby.players[0].color, lobby.players[1].color = lobby.players[1].color, lobby.players[0].color
        
        # Update player colors in database
        color_mapping = {
            lobby.players[0].id: lobby.players[0].color.value,
            lobby.players[1].id: lobby.players[1].color.value
        }
        self.state.update_lobby_player_colors(lobby.code, color_mapping)
        
        # Notify all players
        return {
            'type': 'lobby_update',
            'lobby_code': lobby.code,
            'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} for p in lobby.players],
            'settings': lobby.settings
        }
        
    async def randomize_colors(self, client_id: str, websocket, data: dict) -> Optional[dict]:
        """Randomly assign colors to players in lobby (owner only)"""
        lobby = self.get_lobby_by_client(client_id)
        if not lobby or lobby.owner_id != client_id or len(lobby.players) != 2:
            return None
            
        # Randomly assign colors
        import random
        colors = [Color.WHITE, Color.BLACK]
        random.shuffle(colors)
        
        lobby.players[0].color = colors[0]
        lobby.players[1].color = colors[1]
        
        # Update player colors in database
        color_mapping = {
            lobby.players[0].id: lobby.players[0].color.value,
            lobby.players[1].id: lobby.players[1].color.value
        }
        self.state.update_lobby_player_colors(lobby.code, color_mapping)
        
        # Notify all players
        return {
            'type': 'lobby_update',
            'lobby_code': lobby.code,
            'players': [{'id': p.id, 'name': p.name, 'color': p.color.value} for p in lobby.players],
            'settings': lobby.settings
        }
    
    async def _schedule_bot_first_move(self, lobby_code: str):
        """Schedule the first bot move after game start"""
        await asyncio.sleep(2)  # 2 seconds delay to let UI settle
        # Import here to avoid circular imports
        from server.handlers.game_handler import GameHandler
        game_handler = GameHandler()
        move_result = await game_handler.handle_bot_move(lobby_code)
        
        # If bot made a move, broadcast it to all players
        if move_result:
            lobby = self.state.get_lobby(lobby_code)
            if lobby:
                for player in lobby.players:
                    if hasattr(player, 'websocket') and player.websocket:  # Check for bot players
                        await self.connection_manager.send_message(player.websocket, move_result)