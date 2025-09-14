import asyncio
import websockets
import json
from typing import Dict, Optional

from server.networking.connection import ConnectionManager
from server.handlers.lobby_handler import LobbyHandler
from server.handlers.game_handler import GameHandler
from server.handlers.search_handler import SearchHandler
from server.core.state import GlobalState
from server.core.game import ChessGame
from server.checkers.timer_manager import TimerManager

class GameServer:
    def __init__(self, host: str = "0.0.0.0", port: int = 8765):
        self.host = host
        self.port = port
        self.connection_manager = ConnectionManager(message_handler=self.handle_message)
        self.lobby_handler = LobbyHandler(self.connection_manager)
        self.game_handler = GameHandler()
        self.search_handler = SearchHandler(self.connection_manager, self.lobby_handler)
        self.state = GlobalState.get_instance()
        self.timer_manager = TimerManager(self.handle_timeout)
        
    async def handle_timeout(self, lobby_code: str, timed_out_player: str, winner: str):
        """Handle automatic timeout from timer manager"""
        lobby = self.state.get_lobby(lobby_code)
        if not lobby:
            return
            
        # Create timeout message
        timeout_message = {
            'type': 'game_over',
            'reason': 'timeout',
            'game_state': lobby.game_state,
            'timed_out_player': timed_out_player
        }
        
        # Send to all players in the lobby
        for player in lobby.players:
            await self.connection_manager.send_message(player.websocket, timeout_message)
        
    async def handle_message(self, client_id: str, websocket, data: dict):
        """Route messages to appropriate handlers"""
        message_type = data.get('type')
        
        if message_type == 'validate_server':
            await self.connection_manager.send_message(websocket, {
                'type': 'validate_server_response',
                'isChessServer': True
            })
            
        elif message_type == 'search_game':
            await self.search_handler.handle_search_game(client_id, websocket, data)
            
        elif message_type == 'cancel_search':
            await self.search_handler.handle_cancel_search(client_id, websocket)
            
        elif message_type == 'create_lobby':
            response = await self.lobby_handler.create_lobby(client_id, websocket, data)
            await self.connection_manager.send_message(websocket, response)
            
        elif message_type == 'join_lobby':
            response = await self.lobby_handler.join_lobby(client_id, websocket, data)
            if response:
                # Get the lobby to access other players
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    # Send join message to the new player
                    await self.connection_manager.send_message(websocket, response['join'])
                    
                    # Send broadcast message to existing players (excluding the new player)
                    for player in lobby.players:
                        if player.id != client_id:  # Don't send broadcast to joining player
                            broadcast_msg = response['broadcast'].copy()
                            broadcast_msg['is_owner'] = player.id == lobby.owner_id  # Set correct owner status
                            await self.connection_manager.send_message(player.websocket, broadcast_msg)
            else:
                await self.connection_manager.send_message(websocket, {
                    'type': 'error',
                    'message': 'Could not join the lobby, might due to invalid code or the lobby is full'
                })
                
        elif message_type == 'start_game':
            await self.lobby_handler.start_game(client_id, websocket, data)
            # start_game method handles sending messages internally
                        
        elif message_type == 'move_piece':
            lobby = self.lobby_handler.get_lobby_by_client(client_id)
            if lobby and lobby.game_state:
                response = await self.game_handler.handle_move(client_id, websocket, lobby.game_state, data)
                
                # Update lobby game state if move was successful
                if response.get('type') == 'move_made' and response.get('game_state'):
                    lobby.game_state = response['game_state']
                
                # Special handling for promotion_pending - only send to the player whose turn it is
                if response.get('type') == 'promotion_pending':
                    # Update lobby state for promotion_pending too
                    if response.get('game_state'):
                        lobby.game_state = response['game_state']
                    current_turn = response.get('game_state', {}).get('current_turn')
                    for player in lobby.players:
                        # Only send to the player whose turn it is to choose promotion
                        # Compare player.color.value (string) with current_turn (string)
                        if player.color.value == current_turn:
                            await self.connection_manager.send_message(player.websocket, response)
                            break
                else:
                    # For all other move responses, send to all players
                    for player in lobby.players:
                        await self.connection_manager.send_message(player.websocket, response)
                    
        elif message_type == 'get_valid_moves':
            lobby = self.lobby_handler.get_lobby_by_client(client_id)
            if lobby and lobby.game_state:
                game_obj = ChessGame()
                game_obj.load_from_state(lobby.game_state)
                response = await self.game_handler.handle_valid_moves_request(client_id, websocket, game_obj)
                await self.connection_manager.send_message(websocket, response)
                
        elif message_type == 'resign':
            response = await self.game_handler.handle_resign(client_id, websocket)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    lobby.game_state = response['game_state']
                    for player in lobby.players:
                        await self.connection_manager.send_message(player.websocket, response)
                        
        elif message_type == 'offer_draw':
            response = await self.game_handler.handle_offer_draw(client_id, websocket)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    # Send response to appropriate players
                    if response.get('type') == 'draw_offered':
                        # Send to opponent
                        for player in lobby.players:
                            if player.id != client_id:
                                await self.connection_manager.send_message(player.websocket, response)
                        # Send acknowledgment to offerer
                        await self.connection_manager.send_message(websocket, {'type': 'draw_offer_ack'})
                    else:
                        # Rate limited or other response
                        await self.connection_manager.send_message(websocket, response)
                        
        elif message_type == 'accept_draw':
            response = await self.game_handler.handle_accept_draw(client_id, websocket)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    lobby.game_state = response['game_state']
                    for player in lobby.players:
                        await self.connection_manager.send_message(player.websocket, response)
                        
        elif message_type == 'decline_draw':
            response = await self.game_handler.handle_decline_draw(client_id, websocket, data)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    # Send to offerer
                    for player in lobby.players:
                        if player.id != client_id:
                            await self.connection_manager.send_message(player.websocket, response)
                            
        elif message_type == 'promotion_choice':
            response = await self.game_handler.handle_promotion_choice(client_id, websocket, data)
            if response:
                lobby = self.lobby_handler.get_lobby_by_client(client_id)
                if lobby:
                    lobby.game_state = response['game_state']
                    for player in lobby.players:
                        await self.connection_manager.send_message(player.websocket, response)
    
    async def client_handler(self, websocket):
        """Handle new client connections"""
        client_id = await self.connection_manager.register_client(websocket)
        try:
            async for message in websocket:
                try:
                    data = json.loads(message)
                    await self.handle_message(client_id, websocket, data)
                except json.JSONDecodeError:
                    await self.connection_manager.send_message(websocket, {
                        'type': 'error',
                        'message': 'Invalid JSON'
                    })
        except websockets.exceptions.ConnectionClosed:
            pass
        finally:
            await self.connection_manager.unregister_client(client_id)
    
    async def start(self):
        """Start the game server"""
        # Start the timer manager
        timer_task = asyncio.create_task(self.timer_manager.start())
        
        server = await websockets.serve(self.client_handler, self.host, self.port)
        print(f"Game server started on ws://{self.host}:{self.port}")
        print("Timer manager started for automatic timeout detection")
        
        try:
            await server.wait_closed()
        finally:
            self.timer_manager.stop()
            timer_task.cancel()
            try:
                await timer_task
            except asyncio.CancelledError:
                pass

if __name__ == "__main__":
    server = GameServer()
    asyncio.run(server.start())