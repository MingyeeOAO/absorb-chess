import asyncio
from typing import Dict, Optional
from server.core.state import GlobalState
from server.core.models import Player
from server.core.enums import Color
from server.handlers.lobby_handler import LobbyHandler
from server.networking.connection import ConnectionManager


class SearchHandler:
    def __init__(self, connection_manager: ConnectionManager, lobby_handler: LobbyHandler):
        self.connection_manager = connection_manager
        self.lobby_handler = lobby_handler
        self.state = GlobalState.get_instance()

    async def handle_search_game(self, client_id: str, websocket, data: dict):
        """Handle a player searching for a game"""
        player_name = data.get('player_name', 'Player')

        # Check if player is already in a lobby
        existing_lobby = self.state.get_lobby_by_client(client_id)
        if existing_lobby:
            # If the game is over, remove the player from the old lobby to allow new search
            if existing_lobby.game_state and existing_lobby.game_state.get('game_over'):
                # Leave the finished lobby
                await self.lobby_handler.leave_lobby(client_id, websocket)
            else:
                # Still in an active game
                await self.connection_manager.send_message(websocket, {
                    'type': 'error',
                    'message': 'Already in a lobby'
                })
                return

        # Add player to searching list
        self.state.add_searching_player(client_id, websocket, player_name)

        # Notify player they're now searching
        await self.connection_manager.send_message(websocket, {
            'type': 'search_started'
        })

        # Check for other searching players
        searching = self.state.get_searching_players()
        opponent_id = None

        for other_id, (other_ws, other_name) in searching.items():
            if other_id != client_id:
                opponent_id = other_id
                break

        if opponent_id:
            # Create a lobby for these players
            opponent_ws, opponent_name = searching[opponent_id]

            # Remove both players from searching
            self.state.remove_searching_player(client_id)
            self.state.remove_searching_player(opponent_id)

            # Create and set up the lobby
            lobby_response = await self.lobby_handler.create_lobby(client_id, websocket, {
                'player_name': player_name,
                'settings': {
                    'time_minutes': 10,  # Default 10 minutes
                    'time_increment_seconds': 0  # Default 0 seconds increment
                }
            })
            lobby_code = lobby_response['lobby_code'] if lobby_response else None

            # Add the second player
            join_response = await self.lobby_handler.join_lobby(opponent_id, opponent_ws, {
                'lobby_code': lobby_code,
                'player_name': opponent_name
            })

            if join_response:
                # Notify both players
                    await self.connection_manager.send_message(websocket, {
                        'type': 'search_game_found',
                        'opponent_name': opponent_name,
                        'lobby_code': lobby_code,
                        'player_color': 'white',
                        'player_id': client_id
                    })
                    await self.connection_manager.send_message(opponent_ws, {
                        'type': 'search_game_found',
                        'opponent_name': player_name,
                        'lobby_code': lobby_code,
                        'player_color': 'black',
                        'player_id': opponent_id
                    })
                    # Automatically start the game for both players
                    lobby = self.state.get_lobby(lobby_code)
                    if lobby:
                        await self.lobby_handler.start_game(client_id, websocket, {})
                        # start_game method handles sending messages internally, no need for additional messaging

    async def handle_cancel_search(self, client_id: str, websocket):
        """Handle a player canceling their search"""
        self.state.remove_searching_player(client_id)
        await self.connection_manager.send_message(websocket, {
            'type': 'search_game_cancelled'
        })

    def get_searching_count(self) -> int:
        """Get the number of players currently searching"""
        return len(self.state.get_searching_players())